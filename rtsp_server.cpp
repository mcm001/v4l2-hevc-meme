// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

// Adapted from
// https://github.com/wpilibsuite/allwpilib/blob/9cd933fa1494a4e486102b17040d9cf9201b75cd/wpinet/examples/webserver/webserver.cpp#L68

// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include <cstdio>
#include <memory>

#include <wpi/print.h>

#include "wpinet/EventLoopRunner.h"
#include "wpinet/HttpServerConnection.h"
#include "wpinet/UrlParser.h"
#include "wpinet/uv/Loop.h"
#include "wpinet/uv/Tcp.h"

namespace uv = wpi::uv;

class RtspServerConnection
    : public std::enable_shared_from_this<RtspServerConnection> {
private:
  std::shared_ptr<uv::Stream> m_stream;
  std::string m_buf;

  void HandleRequest(const std::string_view request) {
    wpi::print(stderr, "Got request:====={}====", request);
  }

public:
  explicit RtspServerConnection(std::shared_ptr<uv::Stream> stream)
      : m_stream(stream) {}

  void Start() {
    // Keep ourselves alive as long as the stream is alive.
    auto self = shared_from_this();

    m_stream->data.connect([self](uv::Buffer &buf, size_t len) {
      // Append the new chunk into our accumulation buffer.
      self->m_buf.append(buf.base, len);

      for (;;) {
        auto pos = self->m_buf.find("\r\n\r\n");
        if (pos == std::string::npos) {
          break; // Haven't seen the terminator yet — wait for more data.
        }

        std::string request = self->m_buf.substr(0, pos + 4);
        self->m_buf.erase(0, pos + 4);
        self->HandleRequest(request);
      }
    });

    m_stream->end.connect([self]() { self->m_stream->Close(); });

    m_stream->StartRead();
  }
};

int main() {
  wpi::EventLoopRunner loop;
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
