package org.photonvision.ffmpeg;

import io.javalin.http.Context;

public class FfmpegRtspHandler {
    static {
        System.loadLibrary("ffmpegjni");
    }

    // long running handler
    public void runStream(Context httpServletRequestRequest) {

    }
}