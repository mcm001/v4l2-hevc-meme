// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the GNU General Public License Version 3 in the root directory of this
// project.

#include "RtspClientsMap.hpp"
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <wpi/print.h>

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
  using namespace std::literals::chrono_literals;

  // Block until the TCP socket is ready to go
  loop.ExecSync([](uv::Loop &loop) {
    auto tcp = uv::Tcp::Create(loop);
    tcp->Bind("", 5801);

    tcp->connection.connect([srv = tcp.get()] {
      auto stream = srv->Accept();
      if (!stream)
        return;

      // TODO upstream converts to ms, but libuv wants seconds
      stream->SetKeepAlive(true, 1ms);

      std::fputs("Got a connection\n", stderr);
      auto conn = std::make_shared<RtspServerConnectionHandler>(stream);

      // on clised/end/error, erase from global list
      auto erase_client = [conn]() {
        wpi::print(stderr, "Client disconnected\n");

        auto it = std::find(rtsp_client_tcp_connections.begin(),
                            rtsp_client_tcp_connections.end(), conn);
        if (it != rtsp_client_tcp_connections.end()) {
          rtsp_client_tcp_connections.erase(it);
        }

        wpi::print("{} clients remaining\n",
                   rtsp_client_tcp_connections.size());
      };
      stream->closed.connect(erase_client);
      stream->end.connect(erase_client);
      stream->error.connect([erase_client](wpi::uv::Error err) {
        wpi::print(stderr, "Stream error: {}\n", err.str());
        erase_client();
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
