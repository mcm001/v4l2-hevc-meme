# HEVC Meme

Locks up my rubik pi. use at your own risk

```
pi@photonvision:~/v4l2-h265-encode$ ./build/hevc_meme out1.h265
Source: 1920x1080

OUTPUT  stride=1920  sizeimage=3133440
CAPTURE sizeimage=307200

terminate called after throwing an instance of 'std::runtime_error'
  what():  VIDIOC_STREAMON output: Cannot allocate memory
Aborted (core dumped)
pi@photonvision:~/v4l2-h265-encode$
```

the ffmpeg i can get from apt is 3 years old. let's build a new one from source. This saves artifacts to my ffmpeg directory

```
git clone https://github.com/FFmpeg/FFmpeg
./configure --disable-everything --enable-shared --disable-static --enable-gpl --enable-libx265 --enable-nonfree --enable-muxer=mov,mp4 --enable-demuxer=mp4,mov --enable-protocol=file --enable-protocol=rtp,udp --enable-encoder=aac,png
make -j
sudo make install
```

We send RTP/UDP to localhost:18888 by default. You can try this out with `ffmpeg -protocol_whitelist file,udp,rtp -i test.sdp -c copy output_file.mp4`

To poke at your decoder, try something like:

```
pi@photonvision2:~/v4l2-hevc-meme$ ffmpeg -h encoder=hevc_rkmpp
ffmpeg version 6.1.1-3ubuntu5+git20240717.8164ff7d~noble Copyright (c) 2000-2023 the FFmpeg developers
  built with gcc 13 (Ubuntu 13.2.0-23ubuntu4)
  configuration: --prefix=/usr --extra-version='3ubuntu5+git20240717.8164ff7d~noble' --toolchain=hardened --libdir=/usr/lib/aarch64-linux-gnu --incdir=/usr/include/aarch64-linux-gnu --arch=arm64 --enable-gpl --disable-stripping --disable-omx --enable-gnutls --enable-libaom --enable-libass --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libglslang --enable-libgme --enable-libgsm --enable-libharfbuzz --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzimg --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-rkmpp --enable-rkrga --enable-version3 --disable-sndio --enable-libdc1394 --enable-libdrm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-ladspa --enable-libbluray --enable-libjack --enable-libpulse --enable-librabbitmq --enable-librist --enable-libsrt --enable-libssh --enable-libsvtav1 --enable-libx264 --enable-libzmq --enable-libzvbi --enable-lv2 --enable-sdl2 --enable-libplacebo --enable-librav1e --enable-pocketsphinx --enable-librsvg --enable-libjxl --enable-shared
  libavutil      58. 29.100 / 58. 29.100
  libavcodec     60. 31.102 / 60. 31.102
  libavformat    60. 16.100 / 60. 16.100
  libavdevice    60.  3.100 / 60.  3.100
  libavfilter     9. 12.100 /  9. 12.100
  libswscale      7.  5.100 /  7.  5.100
  libswresample   4. 12.100 /  4. 12.100
  libpostproc    57.  3.100 / 57.  3.100
Encoder hevc_rkmpp [Rockchip MPP (Media Process Platform) HEVC encoder]:
    General capabilities: delay hardware 
    Threading capabilities: none
    Supported hardware devices: rkmpp rkmpp drm 
    Supported pixel formats: gray yuv420p yuv422p yuv444p nv12 nv21 nv16 nv24 yuyv422 yvyu422 uyvy422 rgb24 bgr24 rgba rgb0 bgra bgr0 argb 0rgb abgr 0bgr drm_prime
```
