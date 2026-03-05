// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the GNU General Public License Version 3 in the root directory of this project.

package org.photonvision.ffmpeg;

import static org.junit.jupiter.api.Assertions.assertTrue;

import com.sun.jna.NativeLibrary;
import edu.wpi.first.cameraserver.CameraServer;
import edu.wpi.first.cscore.CameraServerJNI;
import edu.wpi.first.cscore.OpenCvLoader;
import edu.wpi.first.networktables.NetworkTablesJNI;
import edu.wpi.first.util.CombinedRuntimeLoader;
import edu.wpi.first.util.PixelFormat;
import edu.wpi.first.util.RuntimeLoader;
import edu.wpi.first.util.WPIUtilJNI;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Comparator;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Disabled;
import org.junit.jupiter.api.Test;
import org.opencv.core.Core;
import org.opencv.core.CvType;
import org.opencv.core.Mat;
import org.opencv.core.Point;
import org.opencv.core.Scalar;
import org.opencv.imgproc.Imgproc;

class FfmpegJniTest {
    @Test
    // @Disabled
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

    @Test
    @Disabled
    public void testStaticImage() throws Exception {
        WPIUtilJNI.Helper.setExtractOnStaticLoad(false);
        OpenCvLoader.Helper.setExtractOnStaticLoad(false);

        CombinedRuntimeLoader.loadLibraries(
                FfmpegJniTest.class, Core.NATIVE_LIBRARY_NAME, "wpiutil", "wpinet");
        RuntimeLoader.loadLibrary("RtspServer");

        FfmpegRtspHandler.initialize();

        var mat = Mat.zeros(720, 1280, CvType.CV_8UC3);
        Imgproc.putText(
                mat,
                "Hello world",
                new Point(100, 100),
                Imgproc.FONT_HERSHEY_SIMPLEX,
                4,
                new Scalar(255, 128, 0));

        var framesDir = Path.of("test_frames");
        if (framesDir.toFile().exists()) {
            Files.walk(framesDir)
                    .sorted(Comparator.reverseOrder())
                    .map(Path::toFile)
                    .forEach(File::delete);
        }
        Files.createDirectories(framesDir);

        var builder =
                new ProcessBuilder(
                        "ffmpeg",
                        "-rtsp_transport",
                        "udp",
                        "-stimeout",
                        "5000000",
                        "-i",
                        "rtsp://localhost:5801/test",
                        "-vframes",
                        "100",
                        framesDir.resolve("frame_%04d.jpg").toString());
        builder.inheritIO();
        Process p = null;

        for (int i = 0; i < 800; i++) {
            if (i == 100) p = builder.start();
            FfmpegRtspHandler.putFrame("test", mat.getNativeObjAddr());
            Thread.sleep(1000 / 30);
        }

        boolean finished = p.waitFor(10, TimeUnit.SECONDS);
        if (!finished) {
            p.destroyForcibly();
            throw new RuntimeException("ffmpeg timed out");
        }
        var frames =
                Path.of("test_frames").toFile().listFiles((dir, name) -> name.startsWith("frame_"));
        assertTrue(10 < frames.length);
    }

    @Test
    public void testSmoketest() throws Exception {
        WPIUtilJNI.Helper.setExtractOnStaticLoad(false);
        OpenCvLoader.Helper.setExtractOnStaticLoad(false);

        CombinedRuntimeLoader.loadLibraries(
                FfmpegJniTest.class, Core.NATIVE_LIBRARY_NAME, "wpiutil", "wpinet");
        RuntimeLoader.loadLibrary("RtspServer");

        FfmpegRtspHandler.initialize();

        var mat = Mat.zeros(720, 1280, CvType.CV_8UC3);
        Imgproc.putText(
                mat,
                "Hello world",
                new Point(100, 100),
                Imgproc.FONT_HERSHEY_SIMPLEX,
                4,
                new Scalar(255, 128, 0));

        for (int i = 0; i < 100; i++) {
            FfmpegRtspHandler.putFrame("test", mat.getNativeObjAddr());
            Thread.sleep(1000 / 30);
        }
    }
}
