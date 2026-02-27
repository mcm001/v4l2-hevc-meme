// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the GNU General Public License Version 3 in the root directory of this project.

package org.photonvision.ffmpeg;

public class FfmpegRtspHandler {
  public static native boolean initialize();

  public static native boolean putFrame(String streamName, long matPtr);
}
