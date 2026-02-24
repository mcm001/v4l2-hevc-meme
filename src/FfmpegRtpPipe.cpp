// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include "FfmpegRtpPipe.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
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

  const int BITRATE = 2'000'000; // bps

  // ── 1. Find and allocate the hevc_rkmpp encoder ──────────────────────────
  const AVCodec *codec = avcodec_find_encoder_by_name("hevc_rkmpp");
  if (!codec)
    throw std::runtime_error("hevc_rkmpp encoder not found");

  enc_ctx_ = avcodec_alloc_context3(codec);
  if (!enc_ctx_)
    throw std::runtime_error("avcodec_alloc_context3 failed");

  // ── 2. Configure encoder parameters ──────────────────────────────────────
  enc_ctx_->width = width_;
  enc_ctx_->height = height_;
  enc_ctx_->time_base = {1, 90000};
  enc_ctx_->framerate = {30, 1};        // 30 FPS
  enc_ctx_->pix_fmt = AV_PIX_FMT_BGR24; // hevc_rkmpp supports BGR24 directly
  enc_ctx_->bit_rate = BITRATE;
  enc_ctx_->gop_size = 30; // Keyframe every 1 second at 30fps

  // Try to reduce internal buffering
  AVDictionary *opts = nullptr;
  av_dict_set(&opts, "preset", "ultrafast", 0);
  av_dict_set_int(&opts, "refs", 1, 0);

  // ── 3. Open the encoder ───────────────────────────────────────────────────
  int ret = avcodec_open2(enc_ctx_, codec, nullptr);
  av_dict_free(&opts);
  if (ret < 0)
    throw std::runtime_error("avcodec_open2: " + averr(ret));

  // ── 4. Allocate frame for encoder input ──────────────────────────────────
  enc_frame_ = av_frame_alloc();
  if (!enc_frame_)
    throw std::runtime_error("av_frame_alloc failed");

  enc_frame_->format = enc_ctx_->pix_fmt;
  enc_frame_->width = width_;
  enc_frame_->height = height_;

  enc_frame_->format = enc_ctx_->pix_fmt;
  enc_frame_->width = width_;
  enc_frame_->height = height_;

  // ── 5. Allocate packet for encoder output ────────────────────────────────
  enc_pkt_ = av_packet_alloc();
  if (!enc_pkt_)
    throw std::runtime_error("av_packet_alloc (encoder) failed");

  // Probe encoder to generate a SDP
  probe_and_generate_sdp();
}

void FfmpegRtpPipeline::handle_frame(const cv::Mat &bgr_image) {
  if (bgr_image.cols != width_ || bgr_image.rows != height_)
    throw std::runtime_error(
        "Image dimensions do not match pipeline configuration");
  if (bgr_image.type() != CV_8UC3)
    throw std::runtime_error("Image must be CV_8UC3 (BGR)");
  if (!bgr_image.isContinuous())
    throw std::runtime_error("Image must be continuous");

  // ── Use actual wall-clock time for PTS ───────────────────────────────────
  auto now_us = av_gettime();
  if (first_frame_time_us < 0) {
    first_frame_time_us = now_us;
  }
  auto elapsed_us = now_us - first_frame_time_us;
  int64_t pts =
      elapsed_us * 90 / 1'000'000; // Convert microseconds to 90kHz clock

  // ── 1. Point AVFrame directly at cv::Mat data (zero-copy) ────────────────
  enc_frame_->data[0] = bgr_image.data;
  enc_frame_->linesize[0] = width_ * 3; // BGR24 stride
  enc_frame_->pts = pts;

  // ── 2. Send frame to encoder ──────────────────────────────────────────────
  int ret = avcodec_send_frame(enc_ctx_, enc_frame_);
  if (ret < 0)
    throw std::runtime_error("avcodec_send_frame: " + averr(ret));
  // ── 3. Receive encoded packets ───────────────────────────────────────────
  while (ret >= 0) {
    ret = avcodec_receive_packet(enc_ctx_, enc_pkt_);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      break;
    if (ret < 0)
      throw std::runtime_error("avcodec_receive_packet: " + averr(ret));

    enc_pkt_->stream_index = 0;

    write_packet(enc_pkt_);
    av_packet_unref(enc_pkt_);
  }
}

FfmpegRtpPipeline::~FfmpegRtpPipeline() {
  // Flush encoder
  if (enc_ctx_) {
    avcodec_send_frame(enc_ctx_, nullptr);
    while (avcodec_receive_packet(enc_ctx_, enc_pkt_) == 0) {
      if (header_written_)
        write_packet(enc_pkt_);
      av_packet_unref(enc_pkt_);
    }
    avcodec_free_context(&enc_ctx_);
  }

  av_frame_free(&enc_frame_);
  av_packet_free(&enc_pkt_);

  if (oc_) {
    if (header_written_)
      av_write_trailer(oc_);
    if (!(oc_->oformat->flags & AVFMT_NOFILE))
      avio_closep(&oc_->pb);
    avformat_free_context(oc_);
  }
}

void FfmpegRtpPipeline::probe_and_generate_sdp() {
  AVFrame *probe = av_frame_alloc();
  probe->format = enc_ctx_->pix_fmt;
  probe->width = width_;
  probe->height = height_;
  av_frame_get_buffer(probe, 0);
  av_frame_make_writable(probe);
  // zero-fill = black frame
  memset(probe->data[0], 0, probe->linesize[0] * height_);
  probe->pts = 0;

  avcodec_send_frame(enc_ctx_, probe);
  av_frame_free(&probe);

  AVPacket *probe_pkt = av_packet_alloc();
  if (avcodec_receive_packet(enc_ctx_, probe_pkt) == 0) {
    init_muxer(); // extradata now populated
    header_written_ = true;
    write_packet(probe_pkt); // don't discard — it's a real keyframe
    av_packet_unref(probe_pkt);
  } else {
    throw std::runtime_error("Failed to probe encoder for SDP generation: " +
                             averr(AVERROR(EAGAIN)));
  }
  av_packet_free(&probe_pkt);
}

void FfmpegRtpPipeline::init_muxer() {
  // ── 1. Allocate output context ───────────────────────────────────────────
  int ret = avformat_alloc_output_context2(&oc_, nullptr, "rtp", url_.c_str());
  if (ret < 0 || !oc_)
    throw std::runtime_error("avformat_alloc_output_context2: " + averr(ret));

  // ── 2. Create the video stream and copy codec params from encoder ────────
  st_ = avformat_new_stream(oc_, nullptr);
  if (!st_)
    throw std::runtime_error("avformat_new_stream failed");

  ret = avcodec_parameters_from_context(st_->codecpar, enc_ctx_);
  if (ret < 0)
    throw std::runtime_error("avcodec_parameters_from_context: " + averr(ret));

  st_->time_base = {1, 90000};

  // ── 3. Open the UDP socket ───────────────────────────────────────────────
  ret = avio_open(&oc_->pb, url_.c_str(), AVIO_FLAG_WRITE);
  if (ret < 0)
    throw std::runtime_error("avio_open(" + url_ + "): " + averr(ret));

  // ── 4. Write header ──────────────────────────────────────────────────────
  oc_->start_time_realtime = av_gettime();
  oc_->flags |= AVFMT_FLAG_FLUSH_PACKETS;

  AVDictionary *opts = nullptr;
  av_dict_set_int(&opts, "pkt_size", 1472, 0);
  av_dict_set(&opts, "rtpflags", "send_bye", 0);
  av_dict_set_int(&opts, "buffer_size", 65536, 0);
  av_dict_set_int(&opts, "payload_type", 96, 0);
  av_dict_set_int(&opts, "rtcp_port", 18889, 0); // todo pass in RTCP port
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
  // NAL type check for key-frame flag (skip start code)
  if (pkt->size >= 5) {
    int off = (pkt->data[2] == 1) ? 3 : 4;
    int nal_type = (pkt->data[off] >> 1) & 0x3F;
    if (nal_type == 19 || nal_type == 20)
      pkt->flags |= AV_PKT_FLAG_KEY;
  }

  int ret = av_write_frame(oc_, pkt);
  if (ret < 0)
    std::fprintf(stderr, "WARN: av_write_frame: %s\n", averr(ret).c_str());
}
