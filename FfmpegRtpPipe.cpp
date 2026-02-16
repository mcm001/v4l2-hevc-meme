// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include "FfmpegRtpPipe.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>

static std::string averr(int ret) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(ret, buf, sizeof(buf));
  return {buf};
}

FfmpegRtpPipeline::FfmpegRtpPipeline(int width, int height, const char *url)
    : width_(width), height_(height), url_(url) {

  // ── 1. Set up the extract_extradata BSF ──────────────────────────────────
  // This BSF scans Annex-B HEVC packets, pulls out VPS/SPS/PPS into
  // codecpar->extradata, and passes the full packet through unmodified.
  // We use it to bootstrap the muxer before writing the header.
  const AVBitStreamFilter *bsf_def = av_bsf_get_by_name("extract_extradata");
  if (!bsf_def)
    throw std::runtime_error("extract_extradata BSF not found in libavcodec");

  if (av_bsf_alloc(bsf_def, &bsf_) < 0)
    throw std::runtime_error("av_bsf_alloc failed");

  // Tell the BSF what codec it is processing
  bsf_->par_in->codec_id = AV_CODEC_ID_HEVC;
  bsf_->par_in->codec_type = AVMEDIA_TYPE_VIDEO;
  bsf_->par_in->width = width_;
  bsf_->par_in->height = height_;
  bsf_->time_base_in = {1, 90000};

  if (av_bsf_init(bsf_) < 0)
    throw std::runtime_error("av_bsf_init failed");

  // Allocate a reusable packet for BSF I/O
  bsf_pkt_ = av_packet_alloc();
  if (!bsf_pkt_)
    throw std::runtime_error("av_packet_alloc (bsf) failed");
}

void FfmpegRtpPipeline::handle_frame(std::span<const uint8_t> annexb_nal) {
  AVPacket *pkt = av_packet_alloc();
  if (!pkt)
    throw std::runtime_error("av_packet_alloc failed");

  pkt->buf = nullptr;
  pkt->data = const_cast<uint8_t *>(annexb_nal.data());
  pkt->size = static_cast<int>(annexb_nal.size());
  pkt->stream_index = 0; // must be set BEFORE BSF or it drops timestamps

  if (!header_written_) {
    if (av_bsf_send_packet(bsf_, pkt) < 0)
      throw std::runtime_error("av_bsf_send_packet failed");

    if (av_bsf_receive_packet(bsf_, bsf_pkt_) < 0)
      throw std::runtime_error("av_bsf_receive_packet failed on first frame "
                               "— is prepend_sps_pps_to_idr enabled?");

    bsf_pkt_->pts = bsf_pkt_->dts = next_pts_;
    bsf_pkt_->duration = frame_duration_;
    bsf_pkt_->stream_index = 0;

    init_muxer(bsf_->par_out);
    stream_start_wall_ = Clock::now(); // anchor wall clock to pts=0
    write_packet(bsf_pkt_);
    av_packet_unref(bsf_pkt_);
    header_written_ = true;
  } else {
    if (av_bsf_send_packet(bsf_, pkt) < 0) {
      av_packet_free(&pkt);
      return;
    }
    while (av_bsf_receive_packet(bsf_, bsf_pkt_) == 0) {
      bsf_pkt_->pts = bsf_pkt_->dts = next_pts_;
      bsf_pkt_->duration = frame_duration_;
      bsf_pkt_->stream_index = 0;
      write_packet(bsf_pkt_);
      av_packet_unref(bsf_pkt_);
    }
  }

  av_packet_free(&pkt);
  next_pts_ += frame_duration_;
}

FfmpegRtpPipeline::~FfmpegRtpPipeline() {
  // Flush the BSF
  if (bsf_) {
    av_bsf_send_packet(bsf_, nullptr);
    while (av_bsf_receive_packet(bsf_, bsf_pkt_) == 0) {
      if (header_written_)
        write_packet(bsf_pkt_);
      av_packet_unref(bsf_pkt_);
    }
    av_bsf_free(&bsf_);
  }
  av_packet_free(&bsf_pkt_);

  if (oc_) {
    if (header_written_)
      av_write_trailer(oc_);
    if (!(oc_->oformat->flags & AVFMT_NOFILE))
      avio_closep(&oc_->pb);
    avformat_free_context(oc_);
  }
}

void FfmpegRtpPipeline::init_muxer(AVCodecParameters *par_out) {
  // ── 2. Allocate output context ───────────────────────────────────────────
  int ret = avformat_alloc_output_context2(&oc_, nullptr, "rtp", url_.c_str());
  if (ret < 0 || !oc_)
    throw std::runtime_error("avformat_alloc_output_context2: " + averr(ret));

  // ── 3. Create the video stream and copy codec params from the BSF ────────
  st_ = avformat_new_stream(oc_, nullptr);
  if (!st_)
    throw std::runtime_error("avformat_new_stream failed");

  // This copies extradata (VPS/SPS/PPS) that extract_extradata populated
  ret = avcodec_parameters_copy(st_->codecpar, par_out);
  if (ret < 0)
    throw std::runtime_error("avcodec_parameters_copy: " + averr(ret));

  st_->time_base = {1, 90000};

  // ── 4. Open the UDP socket ───────────────────────────────────────────────
  ret = avio_open(&oc_->pb, url_.c_str(), AVIO_FLAG_WRITE);
  if (ret < 0)
    throw std::runtime_error("avio_open(" + url_ + "): " + averr(ret));

  // ── 5. Write header ──────────────────────────────────────────────────────
  oc_->start_time_realtime = av_gettime();
  oc_->flags |= AVFMT_FLAG_FLUSH_PACKETS;

  AVDictionary *opts = nullptr;
  av_dict_set_int(&opts, "pkt_size", 1472, 0);
  av_dict_set(&opts, "rtpflags", "send_bye", 0);
  av_dict_set_int(&opts, "buffer_size", 65536, 0);
  // Set the RTP clock rate so RTCP SR timestamps are correct
  av_dict_set_int(&opts, "payload_type", 96, 0);
  ret = avformat_write_header(oc_, &opts);
  av_dict_free(&opts);
  if (ret < 0)
    throw std::runtime_error("avformat_write_header: " + averr(ret));

  // Print SDP for the receiver
  {
    char sdp[4096] = {};
    av_sdp_create(&oc_, 1, sdp, sizeof(sdp));
    std::ofstream output_file("stream_sdp.txt");
    output_file << sdp;
  }
}

void FfmpegRtpPipeline::write_packet(AVPacket *pkt) {
  // Convert this packet's PTS (in 90 kHz ticks) to a wall-clock deadline
  // and sleep until we reach it. This prevents the RTP sender from blasting
  // all packets instantly and overflowing the receiver's jitter buffer.
  {
    // pts ticks → microseconds: pts * 1_000_000 / 90_000
    const int64_t pts_us = pkt->pts * 1'000'000LL / 90'000LL;
    const auto deadline =
        stream_start_wall_ + std::chrono::microseconds(pts_us);
    const auto now = Clock::now();
    if (deadline > now)
      std::this_thread::sleep_until(deadline);
  }

  // NAL type check for key-frame flag (skip start code)
  if (pkt->size >= 5) {
    int off = (pkt->data[2] == 1) ? 3 : 4;
    int nal_type = (pkt->data[off] >> 1) & 0x3F;
    if (nal_type == 19 || nal_type == 20)
      pkt->flags |= AV_PKT_FLAG_KEY;
  }

  // Use av_write_frame (not interleaved) — we are the only stream and we
  // are already delivering packets in order, so interleaving just adds
  // an internal reorder queue that introduces extra latency.
  int ret = av_write_frame(oc_, pkt);
  if (ret < 0)
    std::fprintf(stderr, "WARN: av_write_frame: %s\n", averr(ret).c_str());
}
