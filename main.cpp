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
// V4L2Buf — one mmap'd plane
// ─────────────────────────────────────────────────────────────────────────────

struct V4L2Buf {
  void *ptr = nullptr;
  size_t length = 0;
  uint32_t index = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// EncoderSession — owns the fd and performs teardown in the correct order:
//
//   VIDIOC_STREAMOFF  (stop DMA transfers)
//   VIDIOC_REQBUFS=0  (release kernel-side DMA buffer pool — the critical step
//                      that was missing; without this the msm_vidc firmware
//                      holds onto its fixed pool of DMA regions across runs
//                      until ~8 sessions exhaust the pool)
//   munmap            (release userspace mappings)
//   close             (release the session itself)
//
// MmapBufs is held inside here so we control the order explicitly rather than
// relying on C++ reverse-construction-order destruction, which previously
// munmap'd before STREAMOFF/REQBUFS(0).
// ─────────────────────────────────────────────────────────────────────────────

struct EncoderSession {
  int fd = -1;
  std::vector<V4L2Buf> out_bufs;
  std::vector<V4L2Buf> cap_bufs;

  explicit EncoderSession(const char *path) {
    fd = open(path, O_RDWR | O_NONBLOCK);
    if (fd < 0)
      throw std::runtime_error(std::string("open ") + path + ": " +
                               strerror(errno));
  }

  // Non-copyable, non-movable — owns raw resources
  EncoderSession(const EncoderSession &) = delete;
  EncoderSession &operator=(const EncoderSession &) = delete;

  ~EncoderSession() {
    if (fd < 0)
      return;

    printf("EncoderSession: tearing down fd=%d\n", fd);

    // 1. Stop DMA on both queues
    uint32_t t_out = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    uint32_t t_cap = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctl(fd, VIDIOC_STREAMOFF, &t_out);
    ioctl(fd, VIDIOC_STREAMOFF, &t_cap);

    // 2. Release kernel-side buffer allocations (DMA pool) — CRITICAL.
    //    count=0 tells the driver to free all internal DMA buffers immediately
    //    rather than waiting for the session to be garbage-collected by
    //    firmware. Without this, msm_vidc leaks DMA regions across runs even
    //    after close().
    auto free_reqbufs = [&](uint32_t type) {
      v4l2_requestbuffers req{};
      req.type = type;
      req.memory = V4L2_MEMORY_MMAP;
      req.count = 0; // free all
      if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0)
        perror("  WARN: VIDIOC_REQBUFS(0)");
    };
    free_reqbufs(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
    free_reqbufs(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

    // 3. Now safe to unmap — kernel is no longer touching these pages
    for (auto &b : out_bufs)
      if (b.ptr)
        munmap(b.ptr, b.length);
    for (auto &b : cap_bufs)
      if (b.ptr)
        munmap(b.ptr, b.length);

    // 4. Release the session
    close(fd);
    fd = -1;

    printf("EncoderSession: teardown complete\n");
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;
using Ms = std::chrono::duration<double, std::milli>;

static double ms_since(TimePoint t0) { return Ms(Clock::now() - t0).count(); }

static void xioctl(int fd, unsigned long req, void *arg, const char *label) {
  if (ioctl(fd, req, arg) < 0)
    throw std::runtime_error(std::string(label) + ": " + strerror(errno));
}

// ─────────────────────────────────────────────────────────────────────────────
// BGR8 → NV12, written directly into a mmap'd encoder buffer.
// stride = bytesperline as returned by the driver after VIDIOC_S_FMT.
// ─────────────────────────────────────────────────────────────────────────────

static void bgr_to_nv12(const cv::Mat &bgr, uint8_t *dst, int stride,
                        int aligned_height) {
  const int w = bgr.cols;
  const int h = bgr.rows;

  static std::vector<uint8_t> i420;
  i420.resize(w * h * 3 / 2);
  uint8_t *y = i420.data();
  uint8_t *u = y + w * h;
  uint8_t *v = u + (w / 2) * (h / 2);

  libyuv::RGB24ToI420(bgr.data, static_cast<int>(bgr.step[0]), y, w, u, w / 2,
                      v, w / 2, w, h);

  libyuv::I420ToNV12(y, w, u, w / 2, v, w / 2, dst, stride, // Y plane
                     dst + stride * aligned_height,
                     stride, // UV plane — aligned offset
                     w, h);
}

// ─────────────────────────────────────────────────────────────────────────────
// Allocate and mmap V4L2 buffers. Buffers are stored directly into session
// so the destructor can free them in the right order.
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<V4L2Buf> alloc_buffers(int fd, uint32_t type,
                                          uint32_t count) {
  v4l2_requestbuffers req{};
  req.type = type;
  req.memory = V4L2_MEMORY_MMAP;
  req.count = count;
  xioctl(fd, VIDIOC_REQBUFS, &req, "VIDIOC_REQBUFS");

  std::vector<V4L2Buf> bufs(req.count);
  for (uint32_t i = 0; i < req.count; ++i) {
    v4l2_buffer qbuf{};
    v4l2_plane plane{};
    qbuf.type = type;
    qbuf.memory = V4L2_MEMORY_MMAP;
    qbuf.index = i;
    qbuf.m.planes = &plane;
    qbuf.length = 1;
    xioctl(fd, VIDIOC_QUERYBUF, &qbuf, "VIDIOC_QUERYBUF");

    bufs[i].index = i;
    bufs[i].length = plane.length;
    bufs[i].ptr = mmap(nullptr, plane.length, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, plane.m.mem_offset);
    if (bufs[i].ptr == MAP_FAILED)
      throw std::runtime_error("mmap failed for buffer " + std::to_string(i));
  }
  return bufs;
}

std::atomic_bool run{true};
void stop_main(int s) {
  printf("Caught signal %d\n", s);
  run = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
  // if (argc != 2)
  // {
  //   fprintf(stderr, "Usage: %s <output.h265>\n", argv[0]);
  //   return -1;
  // }

  const std::string INPUT_VIDEO = "Img_3733.mp4";
  const std::string OUTPUT_FILE = "output.h265";
  const int N_FRAMES = 3000;
  const int N_LOAD = 300;

  // signal handler
  signal(SIGINT, stop_main);
  signal(SIGTERM, stop_main);

  namespace fs = std::filesystem;

  try {
    // ── 1. Open video source
    // ──────────────────────────────────────────────────

    std::cout << cv::getBuildInformation() << std::endl;
    std::vector<cv::Mat> frames;
    std::set<fs::path> sorted_by_name;

    for (auto &entry : fs::directory_iterator("frames"))
      sorted_by_name.insert(entry.path());

    for (const auto &path : sorted_by_name) {
      if (!run || frames.size() >= N_LOAD)
        break;

      printf("loading %s...\n", path.c_str());
      auto f = cv::imread(path, cv::IMREAD_COLOR);
      if (!f.empty()) {
        frames.push_back(f);
        printf("loaded %lu (%lux%lu)\n", frames.size(), f.cols, f.rows);
      }
    }
    printf("loaded %lu frames\n", frames.size());

    if (frames.empty()) {
      throw std::runtime_error("No frames");
    }

    // Peek at the first frame to get dimensions before configuring the encoder.
    const int width = frames[0].cols;
    const int height = frames[0].rows;
    std::cout << "Source: " << width << "x" << height << "\n\n";

    // ── 2. Open encoder (owns fd + bufs + teardown order)
    // ─────────────────────

    EncoderSession session("/dev/video33");
    int videoEncFd = session.fd;

    // ── 3. CAPTURE format: HEVC bitstream output
    // ──────────────────────────────

    v4l2_format fmt_cap{};
    fmt_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt_cap.fmt.pix_mp.width = width;
    fmt_cap.fmt.pix_mp.height = height;
    fmt_cap.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_HEVC;
    fmt_cap.fmt.pix_mp.num_planes = 1;
    fmt_cap.fmt.pix_mp.plane_fmt[0].sizeimage =
        static_cast<uint32_t>(width * height * 3 / 2);
    xioctl(videoEncFd, VIDIOC_S_FMT, &fmt_cap, "VIDIOC_S_FMT capture");
    const uint32_t cap_buf_size = fmt_cap.fmt.pix_mp.plane_fmt[0].sizeimage;

    // ── 4. OUTPUT format: NV12 raw input
    // ─────────────────────────────────────

    v4l2_format fmt_out{};
    fmt_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt_out.fmt.pix_mp.width = width;
    fmt_out.fmt.pix_mp.height = height;
    fmt_out.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt_out.fmt.pix_mp.num_planes = 1;
    xioctl(videoEncFd, VIDIOC_S_FMT, &fmt_out, "VIDIOC_S_FMT output");

    const int stride =
        static_cast<int>(fmt_out.fmt.pix_mp.plane_fmt[0].bytesperline);
    const uint32_t out_buf_size = fmt_out.fmt.pix_mp.plane_fmt[0].sizeimage;
    assert(stride >= width);
    // sizeimage = stride * aligned_height * 3/2
    // So aligned_height = sizeimage / stride / 3 * 2
    const int aligned_height = (out_buf_size / stride) * 2 / 3;

    std::cout << "height=" << height << " aligned_height=" << aligned_height
              << " stride=" << stride << "\n";
    std::cout << "OUTPUT  stride=" << stride << "  sizeimage=" << out_buf_size
              << "\n";
    std::cout << "CAPTURE sizeimage=" << cap_buf_size << "\n\n";

    // ── 5. Encoder controls
    // ───────────────────────────────────────────────────

    auto set_ctrl = [&](uint32_t id, int32_t val, const char *name) {
      v4l2_control c{id, val};
      if (ioctl(videoEncFd, VIDIOC_S_CTRL, &c) < 0)
        std::cerr << "  WARN: " << name << ": " << strerror(errno) << "\n";
    };

    set_ctrl(V4L2_CID_MPEG_VIDEO_BITRATE_MODE, 1, "bitrate_mode CBR");
    set_ctrl(V4L2_CID_MPEG_VIDEO_BITRATE, 1'000'000, "bitrate target");
    set_ctrl(V4L2_CID_MPEG_VIDEO_GOP_SIZE, 30, "gop_size");
    set_ctrl(0x00990b84 /* prepend_sps_pps */, 1, "prepend_sps_pps_to_idr");
    set_ctrl(0x00992003 /* lowlatency_mode */, 1, "lowlatency_mode");

    // ── 6. Allocate & mmap buffers (stored in session for ordered teardown)
    // ───────

    session.out_bufs =
        alloc_buffers(videoEncFd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, 4);
    session.cap_bufs =
        alloc_buffers(videoEncFd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 4);
    auto &out_bufs = session.out_bufs;
    auto &cap_bufs = session.cap_bufs;

    // Pre-queue all capture buffers
    for (auto &b : cap_bufs) {
      v4l2_buffer qbuf{};
      v4l2_plane plane{};
      qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      qbuf.memory = V4L2_MEMORY_MMAP;
      qbuf.index = b.index;
      qbuf.m.planes = &plane;
      qbuf.length = 1;
      plane.length = static_cast<uint32_t>(b.length);
      xioctl(videoEncFd, VIDIOC_QBUF, &qbuf, "VIDIOC_QBUF capture pre-queue");
    }

    // ── 7. STREAMON
    // ───────────────────────────────────────────────────────────

    uint32_t type_out = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    uint32_t type_cap = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    xioctl(videoEncFd, VIDIOC_STREAMON, &type_out, "VIDIOC_STREAMON output");
    xioctl(videoEncFd, VIDIOC_STREAMON, &type_cap, "VIDIOC_STREAMON capture");

    // ── 8. Open output file
    // ───────────────────────────────────────────────────

    FfmpegRtpPipeline rtpOutput{width, height, "rtp://10.0.0.4:18888"};
    std::ofstream outfile(OUTPUT_FILE, std::ios::binary);
    if (!outfile)
      throw std::runtime_error("Cannot open " + OUTPUT_FILE);

    // ── 9. Main pipeline loop
    // ─────────────────────────────────────────────────

    std::cout << "=== Pipeline ===\n";

    size_t total_bytes = 0;
    int frame_idx = 0;
    uint32_t out_buf_idx = 0;

    while (frame_idx < N_FRAMES && run) {
      // ── a. BGR → NV12
      auto t_conv = Clock::now();
      auto frame = frames[frame_idx % frames.size()];
      bgr_to_nv12(frame, static_cast<uint8_t *>(out_bufs[out_buf_idx].ptr),
                  stride, aligned_height);
      const double conv_ms = ms_since(t_conv);

      // ── b. Queue NV12 frame to encoder
      v4l2_buffer qbuf{};
      v4l2_plane qplane{};
      qbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
      qbuf.memory = V4L2_MEMORY_MMAP;
      qbuf.index = out_buf_idx;
      qbuf.m.planes = &qplane;
      qbuf.length = 1;
      qplane.bytesused = static_cast<uint32_t>(stride * aligned_height * 3 / 2);
      qplane.length = static_cast<uint32_t>(out_bufs[out_buf_idx].length);
      qbuf.timestamp.tv_sec = static_cast<long>(frame_idx / 30);
      qbuf.timestamp.tv_usec =
          static_cast<long>((frame_idx % 30) * (1'000'000 / 30));
      xioctl(videoEncFd, VIDIOC_QBUF, &qbuf, "VIDIOC_QBUF output");

      // ── c. Poll for encoded output
      auto t_encode = Clock::now();
      pollfd pfd{videoEncFd, POLLIN, 0};
      if (poll(&pfd, 1, 2000) <= 0)
        throw std::runtime_error("poll timeout on frame " +
                                 std::to_string(frame_idx));

      // ── d. Dequeue encoded NAL, write to disk
      v4l2_buffer dqbuf{};
      v4l2_plane dqplane{};
      dqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      dqbuf.memory = V4L2_MEMORY_MMAP;
      dqbuf.m.planes = &dqplane;
      dqbuf.length = 1;
      xioctl(videoEncFd, VIDIOC_DQBUF, &dqbuf, "VIDIOC_DQBUF capture");
      const double encode_ms = ms_since(t_encode);

      const size_t nal_size = dqplane.bytesused;
      const std::span<const uint8_t> hevc_nal{
          static_cast<const uint8_t *>(cap_bufs[dqbuf.index].ptr),
          static_cast<std::streamsize>(nal_size)};
      rtpOutput.handle_frame(hevc_nal);
      outfile.write((const char *)hevc_nal.data(), hevc_nal.size());
      total_bytes += nal_size;

      // ── e. Requeue capture buf; reclaim output buf
      {
        v4l2_buffer rqbuf{};
        v4l2_plane rqplane{};
        rqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        rqbuf.memory = V4L2_MEMORY_MMAP;
        rqbuf.index = dqbuf.index;
        rqbuf.m.planes = &rqplane;
        rqbuf.length = 1;
        rqplane.length = static_cast<uint32_t>(cap_bufs[dqbuf.index].length);
        xioctl(videoEncFd, VIDIOC_QBUF, &rqbuf, "VIDIOC_QBUF capture requeue");
      }
      {
        v4l2_buffer dqout{};
        v4l2_plane dqout_plane{};
        dqout.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        dqout.memory = V4L2_MEMORY_MMAP;
        dqout.m.planes = &dqout_plane;
        dqout.length = 1;
        pollfd pfd2{videoEncFd, POLLOUT, 0};
        poll(&pfd2, 1, 500);
        ioctl(videoEncFd, VIDIOC_DQBUF, &dqout); // best-effort
      }
      out_buf_idx = (out_buf_idx + 1) % static_cast<uint32_t>(out_bufs.size());

      std::cout << "  Frame " << frame_idx << "  conv=" << conv_ms << " ms"
                << "  encode=" << encode_ms << " ms"
                << "  NAL=" << nal_size << " B\n";

      // // pace ourselves
      // auto total_time = ms_since(t_conv);
      // if (total_time < (1000 / 30)) {
      //   std::this_thread::sleep_for(
      //       std::chrono::milliseconds((int)((1000 / 30) - total_time)));
      // }

      ++frame_idx;
    }

    // ── 10. EOS / Drain
    {
      // https://docs.kernel.org/userspace-api/media/v4l/dev-encoder.html
      // We need to (as long as both OUTPUT and CAPTURE queues are streaming):

      // 1. Begin the drain sequence
      v4l2_encoder_cmd cmd{};
      cmd.cmd = V4L2_ENC_CMD_STOP;
      if (ioctl(videoEncFd, VIDIOC_ENCODER_CMD, &cmd) < 0)
        perror("WARN: V4L2_ENC_CMD_STOP");

      // 2. Continue to handle de/requeueing CAPTURE buffs until we get one with
      // V4L2_BUF_FLAG_LAST
      pollfd pfd{videoEncFd, POLLIN, 0};
      while (poll(&pfd, 1, 1000) > 0) {
        v4l2_buffer dqbuf{};
        v4l2_plane dqplane{};
        dqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        dqbuf.memory = V4L2_MEMORY_MMAP;
        dqbuf.m.planes = &dqplane;
        dqbuf.length = 1;
        if (ioctl(videoEncFd, VIDIOC_DQBUF, &dqbuf) < 0)
          break;

        const size_t nal_size = dqplane.bytesused;
        // Size 0 must be ignored
        if (nal_size > 0) {
          const std::span<const uint8_t> hevc_nal{
              static_cast<const uint8_t *>(cap_bufs[dqbuf.index].ptr),
              static_cast<std::streamsize>(nal_size)};
          rtpOutput.handle_frame(hevc_nal);
          total_bytes += nal_size;
        }

        if (dqbuf.flags & V4L2_BUF_FLAG_LAST) {
          // this is the last one
        }

        // Requeue the capture buffer
        v4l2_buffer rqbuf{};
        v4l2_plane rqplane{};
        rqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        rqbuf.memory = V4L2_MEMORY_MMAP;
        rqbuf.index = dqbuf.index;
        rqbuf.m.planes = &rqplane;
        rqbuf.length = 1;
        rqplane.length = static_cast<uint32_t>(cap_bufs[dqbuf.index].length);
        ioctl(videoEncFd, VIDIOC_QBUF, &rqbuf);
      }
    }

    std::cout << "\nWrote " << total_bytes << " bytes"
              << "\n";

    // EncoderSession destructor runs here:
    // STREAMOFF → REQBUFS(0) → munmap → close
  } catch (const std::exception &e) {
    std::cerr << "FATAL: " << e.what() << std::endl;
    // EncoderSession destructor still runs here on exception unwind
    // this doesn't handle sending an EOS though
    return 1;
  }

  return 0;
}
