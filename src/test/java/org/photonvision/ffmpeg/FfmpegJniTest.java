package org.photonvision.ffmpeg;

import org.junit.jupiter.api.Test;


class FfmpegJniTest {
    @Test
    public void testMeme() {
        FfmpegRtspHandler.putFrame("test", 0);
    }
}
