// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the GNU General Public License Version 3 in the root directory of this project.

package org.photonvision.ffmpeg;

import com.sun.jna.NativeLibrary;
import edu.wpi.first.cameraserver.CameraServer;
import edu.wpi.first.cscore.CameraServerJNI;
import edu.wpi.first.cscore.OpenCvLoader;
import edu.wpi.first.networktables.NetworkTablesJNI;
import edu.wpi.first.util.CombinedRuntimeLoader;
import edu.wpi.first.util.PixelFormat;
import edu.wpi.first.util.WPIUtilJNI;
import java.nio.file.Path;
import org.junit.jupiter.api.Test;
import org.opencv.core.Core;
import org.opencv.core.Mat;

class FfmpegJniTest {
    @Test
    public void testMeme() throws Exception {
        var libPath =
                "/home/matt/Documents/GitHub/v4l2-hevc-meme/build/libs/rtspServer/shared/linuxx86-64/release/libRtspServer.so";
        assert Path.of(libPath).toFile().exists();

        NetworkTablesJNI.Helper.setExtractOnStaticLoad(false);
        CameraServerJNI.Helper.setExtractOnStaticLoad(false);
        WPIUtilJNI.Helper.setExtractOnStaticLoad(false);
        OpenCvLoader.Helper.setExtractOnStaticLoad(false);

        CombinedRuntimeLoader.loadLibraries(
                FfmpegJniTest.class,
                Core.NATIVE_LIBRARY_NAME,
                "wpiutil",
                "wpinet",
                "wpiutiljni",
                "ntcorejni",
                "cscorejni");
        try {
            NativeLibrary.getInstance("avutil");
            NativeLibrary.getInstance("avcodec");
            NativeLibrary.getInstance("avformat");
            System.load(
                    "/home/matt/Documents/GitHub/v4l2-hevc-meme/build/libs/rtspServer/shared/linuxx86-64/release/libRtspServer.so");
        } catch (UnsatisfiedLinkError e) {
            e.printStackTrace();
            throw e;
        }

        FfmpegRtspHandler.initialize();

        var video = CameraServer.startAutomaticCapture();
        video.setVideoMode(PixelFormat.kMJPEG, 1280, 720, 30);
        var cvSink = CameraServer.getVideo();
        var mat = new Mat();

        while (true) {
            if (cvSink.grabFrame(mat) == 0) {
                continue;
            } else {
                FfmpegRtspHandler.putFrame("test", mat.getNativeObjAddr());
            }
        }
    }
}
