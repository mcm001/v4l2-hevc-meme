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

#include <format>
#include <span>
#include <wpi/SmallVector.h>
#include <wpi/print.h>
#include <wpinet/EventLoopRunner.h>
#include <wpinet/HttpServerConnection.h>
#include <wpinet/UrlParser.h>
#include <wpinet/raw_uv_ostream.h>
#include <wpinet/uv/Loop.h>
#include <wpinet/uv/Tcp.h>
#include <wpinet/uv/util.h>

namespace uv = wpi::uv;

enum class RtspState {
  OPTIONS,
  DESCRIBE,
  SETUP,
  PLAY,
  TEARDOWN,
};

static std::string DUMMY_SDP = R"(v=0
o=- 0 0 IN IP4 127.0.0.1
s=No Name
c=IN IP4 10.0.0.4
t=0 0
a=tool:libavformat 60.16.100
m=video 18888 RTP/AVP 96
b=AS:2000
a=rtpmap:96 H265/90000
a=fmtp:96 sprop-vps=QAEMAf//AWAAAAMAgAAAAwAAAwB4vAk=; sprop-sps=QgEBAWAAAAMAgAAAAwAAAwB4oAPAgBDljb5JMpgEAAADAAQAAAMAeCA=; sprop-pps=RAHA88BNkAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=)";

static int TOTALLY_RANDOM_SRC_PORT = 18923;

class RtspServerConnection
    : public std::enable_shared_from_this<RtspServerConnection> {
private:
  std::shared_ptr<uv::Tcp> m_stream;
  std::string m_buf{};

  RtspState state = RtspState::OPTIONS;
  std::string m_session;

  std::string m_destIp;
  int m_destPort;

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
      std::initializer_list<std::pair<std::string, std::string>> headers,
      const std::string &body = "") {
    std::string resp =
        "RTSP/1.0 " + std::to_string(code) + " " + reason + "\r\n";
    resp += "CSeq: " + cseq + "\r\n";
    for (auto &[k, v] : headers)
      resp += k + ": " + v + "\r\n";
    resp += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    resp += "\r\n";
    resp += body;

    wpi::print(stderr, "\nSending response:>>>>\n{}\n<<<<\n", resp);

    wpi::SmallVector<uv::Buffer, 4> toSend;
    wpi::raw_uv_ostream os{toSend, 4096};
    os << resp;
    SendData(os.bufs(), false);
  }

  void SendError(int code, const std::string &reason, const std::string &cseq) {
    SendResponse(code, reason, cseq, {});
  }

  RtspState requestTypeFromRequest(const std::string_view request) {
    if (request.starts_with("OPTIONS"))
      return RtspState::OPTIONS;
    if (request.starts_with("DESCRIBE"))
      return RtspState::DESCRIBE;
    if (request.starts_with("SETUP"))
      return RtspState::SETUP;
    if (request.starts_with("PLAY"))
      return RtspState::PLAY;
    if (request.starts_with("TEARDOWN"))
      return RtspState::TEARDOWN;
    return RtspState::OPTIONS; // default to something
  }

  std::string cseqFromRequest(const std::string_view request) {
    static const std::string cseqHeader = "CSeq:";
    auto cseqPos = request.find(cseqHeader);
    if (cseqPos == std::string_view::npos)
      return "";

    cseqPos += cseqHeader.size();
    // Find start of substring
    while (cseqPos < request.size() && std::isspace(request[cseqPos]))
      ++cseqPos;
    // find end of substring (whitespace, newline)
    size_t cseqEnd = cseqPos;
    while (cseqEnd < request.size() && !std::isspace(request[cseqEnd]))
      ++cseqEnd;

    return std::string{request.substr(cseqPos, cseqEnd - cseqPos)};
  }

  /**
   * Extract the destination IP and port from a SETUP request's Transport
   * header, if present.
   */
  bool ExtractSetupDest(const std::string_view request) {
    // Request will look like
    // "Transport: RTP/AVP;unicast;client_port=18888-18889"

    // Dest port from RTSP request
    {
      static const std::string transportHeader = "Transport:";
      auto transportPos = request.find(transportHeader);
      if (transportPos == std::string_view::npos)
        return false;

      // Only accept RTP/AVP/unicast
      auto rtpPos = request.find("RTP/AVP;unicast", transportPos);
      if (rtpPos == std::string_view::npos)
        return false;

      // Find client_port=, and convert to integer
      static const std::string clientPortStr = "client_port=";
      auto clientPortPos = request.find(clientPortStr, transportPos);
      if (clientPortPos == std::string_view::npos)
        return false;
      clientPortPos += clientPortStr.size();
      size_t clientPortEnd = request.find_first_of("\r\n", clientPortPos);
      if (clientPortEnd == std::string_view::npos)
        return false;
      std::string clientPortStrVal = std::string{
          request.substr(clientPortPos, clientPortEnd - clientPortPos)};
      auto dashPos = clientPortStrVal.find('-');
      if (dashPos == std::string::npos)
        return false;
      std::string clientPortStrVal1 = clientPortStrVal.substr(0, dashPos);

      m_destPort = std::stoul(clientPortStrVal1);
    }

    // Dest IP from peer address of the connection
    {
      std::string peerAddr;
      unsigned int peerPort = 0;
      if (uv::AddrToName(m_stream->GetPeer(), &peerAddr, &peerPort) == 0) {
        m_destIp = peerAddr;
      } else {
        return false;
      }
    }

    return true;
  }

  void HandleRequest(const std::string_view request) {
    wpi::print(stderr, "Got request:>>>>\n{}\n<<<<\n", request);

    auto reqType = requestTypeFromRequest(request);
    auto cseq = cseqFromRequest(request);
    // wpi::println(stderr, "Request type: {}", static_cast<int>(reqType));

    switch (reqType) {
    case RtspState::OPTIONS:
      SendResponse(200, "OK", cseq,
                   {{"Public", "OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN"}},
                   "");
      break;
    case RtspState::DESCRIBE:
      SendResponse(200, "OK", cseq, {{"Content-Type", "application/sdp"}},
                   DUMMY_SDP);
      break;
    case RtspState::SETUP: {
      m_session = "12345678"; // TODO: generate something random

      ExtractSetupDest(request);

      std::string transport = std::format(
          "RTP/AVP;unicast;client_port={}-{};server_port={}-{}", m_destPort,
          m_destPort + 1, TOTALLY_RANDOM_SRC_PORT, TOTALLY_RANDOM_SRC_PORT + 1);

      SendResponse(200, "OK", cseq,
                   {{"Session", m_session}, {"Transport", transport}});
      break;
    }
    case RtspState::PLAY:
      // TODO session verification
      // TODO extract Range from request
      SendResponse(200, "OK", cseq,
                   {{"Session", m_session}, {"Range", "npt=0-"}}, "");
      break;
    case RtspState::TEARDOWN:
      SendResponse(200, "OK", cseq, {{"Session", m_session}}, "");
      
      // and close ourself, since the client is done
      m_stream->Close();
      break;
    default:
      break;
    }
  }

public:
  explicit RtspServerConnection(std::shared_ptr<uv::Tcp> stream)
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
