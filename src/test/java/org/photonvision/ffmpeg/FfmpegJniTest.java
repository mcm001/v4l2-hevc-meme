package org.photonvision.ffmpeg;

import org.junit.jupiter.api.Test;

import io.javalin.Javalin;

class FfmpegJniTest {
    @Test
    public void testMeme() {
        var app = Javalin.create(/*config*/)
            .get("/", ctx -> ctx.result("Hello World"))
            .get("/stream", ctx -> {
                    var handler = new FfmpegRtspHandler();
                    handler.runStream(ctx);
            })
            .start(7070);
    }
}
