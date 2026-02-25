// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#pragma once

#include "rtsp_server.hpp"
#include <map>
#include <opencv2/core/mat.hpp>
#include <string>
#include <wpinet/EventLoopRunner.h>

struct CameraStreamInfo {
  // globally unique name for this stream, used in RTSP URL. Should
  // differentiate between input and output
  std::string unique_name;

  int width;
  int height;
  int fps;

  // TODO should we specify bitrate
};

/**
 * Called once by Java to bind to our socket and start the server. Happens on
 * some sort of global thread
 */
void StartRtspServerLoop();

bool PublishCameraFrame(const std::string &stream_name, const cv::Mat &frame);

std::optional<CameraStreamInfo>
GetCameraStreamInfo(const std::string &stream_name);
