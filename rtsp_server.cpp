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

#include "wpinet/EventLoopRunner.h"
#include "wpinet/HttpServerConnection.h"
#include "wpinet/UrlParser.h"
#include "wpinet/uv/Loop.h"
#include "wpinet/uv/Tcp.h"
#include <span>
#include <wpi/print.h>
#include <wpi/SmallVector.h>
#include <wpinet/raw_uv_ostream.h>

namespace uv = wpi::uv;

enum class RtspState {
  OPTIONS,
  DESCRIBE,
  SETUP,
  PLAY,
};

class RtspServerConnection
    : public std::enable_shared_from_this<RtspServerConnection> {
private:
  std::shared_ptr<uv::Stream> m_stream;
  std::string m_buf{};

  RtspState state = RtspState::OPTIONS;

  // From HttpServerConnection.cpp
  void SendData(std::span<const uv::Buffer> bufs, bool closeAfter) {
    m_stream->Write(bufs,
                    [closeAfter, stream = m_stream](auto bufs, uv::Error) {
                      for (auto &&buf : bufs) {
                        buf.Deallocate();
                      }
                      if (closeAfter) {
                        stream->Close();
                      }
                    });
  }

  void SendResponse(
      int code, const std::string &reason, const std::string &cseq,
      // std::initializer_list<std::pair<std::string, std::string>> headers,
      const std::string &body = "") {
    std::string resp =
        "RTSP/1.0 " + std::to_string(code) + " " + reason + "\r\n";
    resp += "CSeq: " + cseq + "\r\n";
    // for (auto &[k, v] : headers)
    //   resp += k + ": " + v + "\r\n";
    if (!body.empty())
      resp += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    resp += "\r\n";
    resp += body;

    wpi::SmallVector<uv::Buffer, 4> toSend;
    wpi::raw_uv_ostream os{toSend, 4096};
    os << resp;
    SendData(os.bufs(), false);
  }

  void SendError(int code, const std::string &reason, const std::string &cseq) {
    SendResponse(code, reason, cseq, {});
  }

  void HandleRequest(const std::string_view request) {
    wpi::print(stderr, "Got request:====={}====", request);

    SendResponse(200, "OK", "1", "Hello world!");
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

    m_stream->end.connect([self]() {
      wpi::print(stderr, "Client disconnected (state={})\n",
                 static_cast<int>(self->state));
      self->m_stream->Close();
    });

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
