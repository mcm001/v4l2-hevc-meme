#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <iostream>

int main()
{
    int width = 1920;
    int height = 1080;

    std::string test_video{"IMG_3733.MOV"};
    cv::VideoCapture capture{test_video};

    cv::Mat frame;

    if (!capture.isOpened())
        throw "Error when reading steam_avi";

    for (;;)
    {
        capture >> frame;
        if (frame.empty())
            break;
        std::cout << frame.size() << std::endl;
    }

    int fd = open("/dev/video33", O_RDWR);

    // 1. Set CAPTURE format first (the compressed output)
    v4l2_format fmt_cap{};
    fmt_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt_cap.fmt.pix_mp.width = width;
    fmt_cap.fmt.pix_mp.height = height;
    fmt_cap.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_HEVC;
    fmt_cap.fmt.pix_mp.num_planes = 1;
    ioctl(fd, VIDIOC_S_FMT, &fmt_cap);

    // 2. Set OUTPUT format (the raw NV12 input)
    v4l2_format fmt_out{};
    fmt_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt_out.fmt.pix_mp.width = width;
    fmt_out.fmt.pix_mp.height = height;
    fmt_out.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt_out.fmt.pix_mp.num_planes = 1;
    ioctl(fd, VIDIOC_S_FMT, &fmt_out);
    // NOTE: read back fmt_out.fmt.pix_mp.plane_fmt[0].bytesperline after this —
    // the driver may round up stride to a hardware-aligned value (often 512B aligned)
    // and your NV12 buffer layout must match that stride, not just width.

    // 3. Configure controls
    auto set_ctrl = [&](uint32_t id, int32_t val)
    {
        v4l2_control c{id, val};
        ioctl(fd, VIDIOC_S_CTRL, &c);
    };
    set_ctrl(V4L2_CID_MPEG_VIDEO_BITRATE_MODE, 1);    // CBR
    set_ctrl(V4L2_CID_MPEG_VIDEO_BITRATE, 4'000'000); // 4 Mbps
    set_ctrl(V4L2_CID_MPEG_VIDEO_GOP_SIZE, 30);       // 1 keyframe/sec @ 30fps
    set_ctrl(0x00990b84 /* prepend_sps_pps */, 1);
    set_ctrl(0x00992003 /* lowlatency_mode */, 1);

    // 4. REQBUFS, QBUF, STREAMON, then your QBUF/DQBUF loop
}