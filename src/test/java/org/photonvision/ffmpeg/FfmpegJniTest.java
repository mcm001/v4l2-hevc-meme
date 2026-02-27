// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the GNU General Public License Version 3 in the root directory of this project.

package org.photonvision.ffmpeg;

import org.junit.jupiter.api.Test;
import org.opencv.imgcodecs.Imgcodecs;
import org.opencv.core.Core;

import edu.wpi.first.util.CombinedRuntimeLoader;


class FfmpegJniTest {
    @Test
    public void testMeme() throws Exception {
        CombinedRuntimeLoader.loadLibraries(FfmpegJniTest.class, Core.NATIVE_LIBRARY_NAME, "wpiutil", "wpinet");
        System.load("/home/matt/Documents/GitHub/v4l2-hevc-meme/build/libs/rtspServer/shared/linuxx86-64/release/libRtspServer.so");

        var mat = Imgcodecs.imread("/home/matt/Downloads/left-cam_input_2025-07-02T065100975_None-0-.jpg");

        FfmpegRtspHandler.initialize();
        
        while (true) {
            FfmpegRtspHandler.putFrame("test", mat.getNativeObjAddr());
            Thread.sleep(1000/30);
        }
    }
}
