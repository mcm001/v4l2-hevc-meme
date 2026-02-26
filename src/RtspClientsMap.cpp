// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include "RtspClientsMap.hpp"

// All camera streams we know about, keyed by unique name
std::map<std::string, CameraStreamInfo> all_camera_streams;

// All streams where the TCP connection is still alive
// TODO TCP keepalives
std::vector<std::shared_ptr<RtspServerConnectionHandler>>
    rtsp_client_tcp_connections;

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

      srv->end.connect([conn](auto &&...) {
        std::fputs("Erasing this...\n", stderr);
        std::remove_if(rtsp_client_tcp_connections.begin(),
                       rtsp_client_tcp_connections.end(),
                       [conn](const auto &maybe) { return conn == maybe; });
      });

      rtsp_client_tcp_connections.push_back(conn);

      conn->Start();
    });

    tcp->Listen();
    std::fputs("Listening on port 5801\n", stderr);
  });
}

bool PublishCameraFrame(const std::string &stream_name, const cv::Mat &frame) {
  // Always record for GetCameraStreamInfo
  all_camera_streams[stream_name] = CameraStreamInfo{
      .unique_name = stream_name,
      .width = frame.size().width,
      .height = frame.size().height,
      .fps = 30, // TODO pipe FPS
  };

  for (const auto &conn : rtsp_client_tcp_connections) {
    // TODO this is really bad, we should have a map of stream name to clients
    // that are subscribed to it or something
    conn->OfferFrame(stream_name, frame);
  }

  return true;
}

std::optional<CameraStreamInfo>
GetCameraStreamInfo(const std::string &stream_name) {
  // Should always be updated by PublishCameraFrame
  auto it = all_camera_streams.find(stream_name);
  if (it == all_camera_streams.end()) {
    return std::nullopt;
  }
  return it->second;
}

// TODO once a camera is registered there's currently no way for it to time out
