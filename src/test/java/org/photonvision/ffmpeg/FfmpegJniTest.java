// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the GNU General Public License Version 3 in the root directory of this project.

package org.photonvision.ffmpeg;

import edu.wpi.first.util.CombinedRuntimeLoader;
import java.nio.file.Path;
import org.junit.jupiter.api.Test;
import org.opencv.core.Core;
import org.opencv.imgcodecs.Imgcodecs;

class FfmpegJniTest {
    @Test
    public void testMeme() throws Exception {
        var libPath =
                "/home/matt/Documents/GitHub/v4l2-hevc-meme/build/libs/rtspServer/shared/linuxx86-64/release/libRtspServer.so";
        assert Path.of(libPath).toFile().exists();

        CombinedRuntimeLoader.loadLibraries(
                FfmpegJniTest.class, Core.NATIVE_LIBRARY_NAME, "wpiutil", "wpinet");
        try {
            System.load(
                    "/home/matt/Documents/GitHub/v4l2-hevc-meme/build/libs/rtspServer/shared/linuxx86-64/release/libRtspServer.so");
        } catch (UnsatisfiedLinkError e) {
            e.printStackTrace();
            throw e;
        }

        var mat =
                Imgcodecs.imread("/home/matt/Downloads/left-cam_input_2025-07-02T065100975_None-0-.jpg");

        FfmpegRtspHandler.initialize();

        for (int i = 0; i < 10; i++) {
            // while (true) {
            FfmpegRtspHandler.putFrame("test", mat.getNativeObjAddr());
            Thread.sleep(1000 / 30);
        }
    }
}
