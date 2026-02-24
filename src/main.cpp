// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include "rtsp_server.hpp"
#include <wpinet/EventLoopRunner.h>

int main() {
  using namespace wpi;

  EventLoopRunner loop;
  loop.ExecAsync([](uv::Loop &loop) {
    auto tcp = uv::Tcp::Create(loop);
    tcp->Bind("", 5801);

    tcp->connection.connect([srv = tcp.get()] {
      auto stream = srv->Accept();
      if (!stream)
        return;

      std::fputs("Got a connection\n", stderr);
      auto conn = std::make_shared<RtspServerConnection>(stream);
      stream->SetData(conn);
      conn->Start(); // wire up signals now that shared_ptr exists
    });

    tcp->Listen();
    std::fputs("Listening on port 5801\n", stderr);
  });

  static_cast<void>(std::getchar());
}
