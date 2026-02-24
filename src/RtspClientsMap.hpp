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

// All camera streams we know about, keyed by unique name
std::map<std::string, CameraStreamInfo> all_camera_streams;

// All streams that have an active RTSP client connection, keyed by unique name.
std::map<std::string, std::shared_ptr<RtspServerConnectionHandler>>
    active_rtsp_clients;

// Global loop. Never stops. Make sure to use ExecAsync to run things on it
wpi::EventLoopRunner loop;

/**
 * Called once by Java to bind to our socket and start the server. Happens on
 * some sort of global thread
 */
void StartRtspServerLoop() {
  using namespace wpi;

  // Block until the TCP socket is ready to go
  loop.ExecSync([](uv::Loop &loop) {
    auto tcp = uv::Tcp::Create(loop);
    tcp->Bind("", 5801);

    tcp->connection.connect([srv = tcp.get()] {
      auto stream = srv->Accept();
      if (!stream)
        return;

      std::fputs("Got a connection\n", stderr);
      auto conn = std::make_shared<RtspServerConnectionHandler>(stream);
      stream->SetData(conn);
      conn->Start();

      // TODO -- RtspServerConnectionHandler should add itself to
      // active_rtsp_clients when it gets a valid SETUP, and remove itself on
      // TEARDOWN or disconnect It feels really odd to me to have it do that
      // part of lifecycle management, though. I want better ideas This also
      // doesn't have any mechanism for handling multiple clients connecting to
      // the same stream. I do want to support this We also have to trust that
      // ~RtspServerConnectionHandler will be called on TCP disconnect
      // (currently no timeout), and will clean up its FfmpegRtpPipeline, and
      // remove itself from the registry here
    });

    tcp->Listen();
    std::fputs("Listening on port 5801\n", stderr);
  });
}

void NotifyPotentialStream(const std::string &stream_name, int width,
                           int height, int fps) {
  // Write down for later
  all_camera_streams[stream_name] = CameraStreamInfo{
      .unique_name = stream_name,
      .width = width,
      .height = height,
      .fps = 30, // TODO
  };
}

bool PublishCameraFrame(const std::string &stream_name, const cv::Mat &frame) {
  if (!all_camera_streams.contains(stream_name)) {
    // unknown stream
    return false;
  }

  active_rtsp_clients[stream_name]->PushFrame(frame);

  return true;
}

// TODO once a camera is registered there's currently no way to
