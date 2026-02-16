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
