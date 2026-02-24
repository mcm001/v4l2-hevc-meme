// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include <cerrno>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "FfmpegRtpPipe.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <set>
#include <signal.h>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace fs = std::filesystem;

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;
using Ms = std::chrono::duration<double, std::milli>;

static double ms_since(TimePoint t0) { return Ms(Clock::now() - t0).count(); }

std::atomic_bool run{true};
void stop_main(int s) {
  printf("Caught signal %d\n", s);
  run = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {

  // signal handler
  signal(SIGINT, stop_main);
  signal(SIGTERM, stop_main);

  cv::VideoCapture cap("/dev/v4l/by-id/"
                       "usb-Arducam_Technology_Co.__Ltd._Arducam_OV2311_USB_"
                       "Camera_UC621-video-index0");

  try {
    cv::Mat frame;

    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
    cap.set(cv::CAP_PROP_FPS, 30);
    cap.set(cv::CAP_PROP_FPS, 30);

    cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 3);
    cap.set(cv::CAP_PROP_BRIGHTNESS, 0); // balanced

    while (!cap.isOpened()) {
      std::cout << "Waiting for camera to open..." << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(1));

      if (!run) {
        return 0;
      }
    }

    const int width = cap.get(cv::CAP_PROP_FRAME_WIDTH);
    const int height = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    std::cout << "Source: " << width << "x" << height << "\n\n";

    std::cout << "=== Pipeline ===\n";

    size_t total_bytes = 0;
    int frame_idx = 0;
    uint32_t out_buf_idx = 0;

    FfmpegRtpPipeline rtpOutput{width, height, "rtp://192.168.0.3:18888"};

    while (run) {
      auto t_start = Clock::now();

      cap >> frame;

      if (frame.empty()) {
        std::cerr << "Failed to grab frame" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }

      // === ADD THE TIMESTAMP CODE HERE ===
      auto now = std::chrono::system_clock::now();
      auto duration = now.time_since_epoch();
      auto millis =
          std::chrono::duration_cast<std::chrono::milliseconds>(duration)
              .count();
      auto seconds = millis / 1000;
      auto ms = millis % 1000;

      time_t time_t_now = seconds;
      struct tm *tm_info = localtime(&time_t_now);
      char timestamp_str[32];
      snprintf(timestamp_str, sizeof(timestamp_str), "%02d:%02d:%02d.%03lld",
               tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, ms);

      cv::putText(frame, timestamp_str, cv::Point(10, 30),
                  cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2,
                  cv::LINE_AA);
      // === END TIMESTAMP CODE ===

      const double grab_ms = ms_since(t_start);

      auto t_conv = Clock::now();
      rtpOutput.handle_frame(frame);
      const double conv_ms = ms_since(t_conv);

      std::cout << frame_idx << "," << grab_ms << "," << conv_ms << "\n";

      // // pace ourselves
      // auto total_time = ms_since(t_start);
      // if (total_time < (1000 / 30)) {
      //   std::this_thread::sleep_for(
      //       std::chrono::milliseconds((int)((1000 / 30) - total_time)));
      // }

      ++frame_idx;
    }
  } catch (const std::exception &e) {
    std::cerr << "FATAL: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
