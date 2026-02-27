package org.photonvision.ffmpeg;

public class FfmpegRtspHandler {
  public static native int putFrame(String streamName, long matPtr);
}