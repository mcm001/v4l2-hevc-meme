// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include "RtspClientsMap.hpp"
#include "rtsp_server.hpp"
#include <wpinet/EventLoopRunner.h>

int main() {
  StartRtspServerLoop();

  static_cast<void>(std::getchar());
}
