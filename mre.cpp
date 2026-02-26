// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include <iostream>
#include <wpinet/EventLoopRunner.h>
#include <wpinet/uv/Tcp.h>

std::vector<std::shared_ptr<wpi::uv::Tcp>> tcp_connections;

wpi::EventLoopRunner loop;

int main() {
  loop.ExecSync([](wpi::uv::Loop &loop) {
    auto tcp = wpi::uv::Tcp::Create(loop);
    tcp->Bind("", 5801);

    tcp->connection.connect([srv = tcp.get()] {
      auto stream = srv->Accept();
      if (!stream)
        return;

      std::fputs("Got a connection\n", stderr);
      tcp_connections.push_back(stream);
    });

    tcp->Listen();
    std::fputs("Listening on port 5801\n", stderr);
  });

  // wait for keypress
  std::fputs("Press Enter to exit...\n", stderr);
  std::cin.get();
}
