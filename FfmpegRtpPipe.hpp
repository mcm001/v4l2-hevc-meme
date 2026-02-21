// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
} // extern "C"

#include <chrono>
#include <opencv2/core.hpp>
#include <span>
#include <string>
#include <vector>

class FfmpegRtpPipeline {
private:
  int width_, height_;
  std::string url_;
  AVFormatContext *oc_ = nullptr;
  AVStream *st_ = nullptr;

  AVCodecContext *enc_ctx_ = nullptr; // Hardware encoder context
  AVFrame *enc_frame_ = nullptr;      // Frame buffer for BGR24 input
  AVPacket *enc_pkt_ = nullptr;       // Packet buffer for encoded output

  bool header_written_ = false;
  int next_pts_ = 3000; // start at frame 1
  int frame_duration_ = 90000 / 30;
  using Clock = std::chrono::steady_clock;
  std::chrono::time_point<Clock> stream_start_wall_;

  std::optional<Clock::time_point> first_frame_time_;

public:
  FfmpegRtpPipeline(int width, int height, const char *url);
  ~FfmpegRtpPipeline();
  FfmpegRtpPipeline(const FfmpegRtpPipeline &) = delete;
  FfmpegRtpPipeline &operator=(const FfmpegRtpPipeline &) = delete;
  void write_packet(AVPacket *pkt);
  void init_muxer(AVCodecContext *enc_ctx);
  void handle_frame(const cv::Mat &frame);
};
