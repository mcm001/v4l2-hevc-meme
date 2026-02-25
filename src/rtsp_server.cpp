// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

// Adapted from
// https://github.com/wpilibsuite/allwpilib/blob/9cd933fa1494a4e486102b17040d9cf9201b75cd/wpinet/examples/webserver/webserver.cpp#L68

// Test me with "rtsp://127.0.0.1:5801/lifecam" in VLC or similar.

// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include <cstdio>
#include <memory>

#include "RtspClientsMap.hpp"
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

#include "rtsp_server.hpp"

namespace uv = wpi::uv;

// Serve this DUMMY_SDP, a generic H265 SDP which relies on SPS/VPS/PPS
// transmitted in-band in the RTP stream
static std::string DUMMY_SDP =
    "v=0\r\n"
    "o=- 0 0 IN IP4 127.0.0.1\r\n"
    "s=No Name\r\n"
    "c=IN IP4 0.0.0.0\r\n" // overridden by SETUP/PLAY anyway
    "t=0 0\r\n"
    "m=video 0 RTP/AVP 96\r\n" // port 0 = unicast placeholder
    "a=rtpmap:96 H265/90000\r\n"
    "a=control:trackID=0\r\n"; // needed by some clients to SETUP the right
                               // track

// TODO this is a huge hack. And does not work. At all
static int TOTALLY_RANDOM_SRC_PORT = 18923;

// From HttpServerConnection.cpp
void RtspServerConnectionHandler::SendData(std::span<const uv::Buffer> bufs,
                                           bool closeAfter) {
  m_stream->Write(bufs, [closeAfter, stream = m_stream](auto bufs, uv::Error) {
    for (auto &&buf : bufs) {
      buf.Deallocate();
    }
    if (closeAfter) {
      stream->Close();
    }
  });
}

void RtspServerConnectionHandler::SendResponse(
    int code, const std::string &reason, const std::string &cseq,
    std::initializer_list<std::pair<std::string, std::string>> headers,
    const std::string &body) {
  std::string resp = "RTSP/1.0 " + std::to_string(code) + " " + reason + "\r\n";
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

void RtspServerConnectionHandler::SendError(int code, const std::string &reason,
                                            const std::string &cseq) {
  SendResponse(code, reason, cseq, {});
}

RtspState RtspServerConnectionHandler::requestTypeFromRequest(
    const std::string_view request) {
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

std::string
RtspServerConnectionHandler::cseqFromRequest(const std::string_view request) {
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

void RtspServerConnectionHandler::HandleSetup(std::string_view request,
                                              const std::string &cseq) {

  m_session = "12345678"; // TODO: generate something random

  if (!ExtractSetupDest(request)) {
    SendResponse(400, "Bad Request", cseq, {});
    return;
  }

  std::string transport = std::format(
      "RTP/AVP;unicast;client_port={}-{};server_port={}-{}", m_destPort,
      m_destPort + 1, TOTALLY_RANDOM_SRC_PORT, TOTALLY_RANDOM_SRC_PORT + 1);

  auto info = GetCameraStreamInfo(m_streamPath);
  if (!info) {
    SendResponse(404, "Not Found", cseq, {});
    return;
  }

  // Time to make our stream!
  m_ffmpegStreamer.emplace(info->width, info->height,
                           std::format("rtp://{}:{}", m_destIp, m_destPort));

  SendResponse(200, "OK", cseq,
               {{"Session", m_session}, {"Transport", transport}});
}

/**
 * Extract the destination IP and port from a SETUP request's Transport
 * header, if present.
 */
bool RtspServerConnectionHandler::ExtractSetupDest(
    const std::string_view request) {
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

  // Extract path from the first line, which is something like "SETUP
  // rtsp://127.0.0.1:5801/lifecam/ RTSP/1.0". May or may not have a trailing /
  {
    // TODO implement me
    m_streamPath = "lifecam";
  }

  return true;
}

void RtspServerConnectionHandler::HandleRequest(
    const std::string_view request) {
  wpi::print(stderr, "Got request:>>>>\n{}\n<<<<\n", request);

  auto reqType = requestTypeFromRequest(request);
  auto cseq = cseqFromRequest(request);
  // wpi::println(stderr, "Request type: {}", static_cast<int>(reqType));

  switch (reqType) {
  case RtspState::OPTIONS:
    SendResponse(200, "OK", cseq,
                 {{"Public", "OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN"}}, "");
    break;
  case RtspState::DESCRIBE:
    SendResponse(200, "OK", cseq, {{"Content-Type", "application/sdp"}},
                 DUMMY_SDP);
    break;
  case RtspState::SETUP: {
    HandleSetup(request, cseq);
    break;
  }
  case RtspState::PLAY:
    // TODO session verification
    // TODO extract Range from request
    SendResponse(200, "OK", cseq, {{"Session", m_session}, {"Range", "npt=0-"}},
                 "");
    break;
  case RtspState::TEARDOWN:
    m_ffmpegStreamer.reset();

    SendResponse(200, "OK", cseq, {{"Session", m_session}}, "");

    // and close ourself, since the client is done
    m_stream->Close();
    break;
  default:
    break;
  }
}

RtspServerConnectionHandler::RtspServerConnectionHandler(
    std::shared_ptr<uv::Tcp> stream)
    : m_stream(stream) {}

void RtspServerConnectionHandler::Start() {
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
