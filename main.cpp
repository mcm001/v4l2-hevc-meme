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
#include <libyuv.h>
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

const std::string INPUT_VIDEO = "Img_3733.mp4";
const std::string OUTPUT_FILE = "output.h265";
const int N_FRAMES = 3000;
const int N_LOAD = 60;

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;
using Ms = std::chrono::duration<double, std::milli>;

static double ms_since(TimePoint t0) { return Ms(Clock::now() - t0).count(); }

std::atomic_bool run{true};
void stop_main(int s)
{
  printf("Caught signal %d\n", s);
  run = false;
}

std::vector<cv::Mat> loadFrames()
{
  std::cout << cv::getBuildInformation() << std::endl;
  std::vector<cv::Mat> frames;
  std::set<fs::path> sorted_by_name;

  for (auto &entry : fs::directory_iterator("frames"))
    sorted_by_name.insert(entry.path());

  for (const auto &path : sorted_by_name)
  {
    if (!run || frames.size() >= N_LOAD)
      break;

    printf("loading %s...\n", path.c_str());
    auto f = cv::imread(path, cv::IMREAD_COLOR);
    if (!f.empty())
    {
      frames.push_back(f);
      printf("loaded %lu (%lux%lu)\n", frames.size(), f.cols, f.rows);
    }
  }
  printf("loaded %lu frames\n", frames.size());

  if (frames.empty())
  {
    throw std::runtime_error("No frames");
  }

  return std::move(frames);
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[])
{

  // signal handler
  signal(SIGINT, stop_main);
  signal(SIGTERM, stop_main);

  try
  {
    // load from disk
    auto frames = loadFrames();

    // Peek at the first frame to get dimensions before configuring the encoder.
    const int width = frames[0].cols;
    const int height = frames[0].rows;
    std::cout << "Source: " << width << "x" << height << "\n\n";

    std::cout << "=== Pipeline ===\n";

    size_t total_bytes = 0;
    int frame_idx = 0;
    uint32_t out_buf_idx = 0;

    FfmpegRtpPipeline rtpOutput{width, height, "rtp://10.0.0.4:18888"};

    while (frame_idx < N_FRAMES && run)
    {
      auto t_start = Clock::now();

      // ── a. BGR → NV12
      auto frame = frames[frame_idx % frames.size()];

      auto t_conv = Clock::now();
      rtpOutput.handle_frame(frame);
      // outfile.write((const char *)hevc_nal.data(), hevc_nal.size());
      // total_bytes += nal_size;
      const double conv_ms = ms_since(t_conv);

      std::cout << frame_idx << "," << conv_ms << "\n";
      // std::cout << "  Frame " << frame_idx 
      //         << "  conv=" << conv_ms << " ms"
      //           << "  encode=" << encode_ms << " ms"
      //           << "  NAL=" << nal_size << " B\n";
              // << "\n";

      // pace ourselves
      auto total_time = ms_since(t_start);
      if (total_time < (1000 / 30)) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds((int)((1000 / 30) - total_time)));
      }

      ++frame_idx;
    }

    std::cout << "\nWrote " << total_bytes << " bytes"
              << "\n";

    // EncoderSession destructor runs here:
    // STREAMOFF → REQBUFS(0) → munmap → close
  }
  catch (const std::exception &e)
  {
    std::cerr << "FATAL: " << e.what() << std::endl;
    // EncoderSession destructor still runs here on exception unwind
    // this doesn't handle sending an EOS though
    return 1;
  }

  return 0;
}
