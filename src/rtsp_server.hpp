// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#pragma once

#include "FfmpegRtpPipe.hpp"
#include <memory>
#include <optional>
#include <wpinet/uv/Loop.h>
#include <wpinet/uv/Tcp.h>

enum class RtspState {
  OPTIONS,
  DESCRIBE,
  SETUP,
  PLAY,
  TEARDOWN,
};

class RtspServerConnectionHandler
    : public std::enable_shared_from_this<RtspServerConnectionHandler> {
public:
  explicit RtspServerConnectionHandler(std::shared_ptr<wpi::uv::Tcp> stream);

  void Start();

private:
  void SendData(std::span<const wpi::uv::Buffer> bufs, bool closeAfter);
  void SendResponse(
      int code, const std::string &reason, const std::string &cseq,
      std::initializer_list<std::pair<std::string, std::string>> headers,
      const std::string &body = "");
  void SendError(int code, const std::string &reason, const std::string &cseq);
  RtspState requestTypeFromRequest(const std::string_view request);
  std::string cseqFromRequest(const std::string_view request);
  void HandleRequest(const std::string_view request);
  bool ExtractSetupDest(const std::string_view request);

  std::shared_ptr<wpi::uv::Tcp> m_stream;
  std::string m_buf{};

  RtspState state = RtspState::OPTIONS;
  std::string m_session;

  std::string m_destIp;
  int m_destPort;

  // TODO for now this is one pipeline per RTSP connection
  std::optional<FfmpegRtpPipeline> m_ffmpegStreamer;
};
