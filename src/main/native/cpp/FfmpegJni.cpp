// Copyright (c) PhotonVision contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the GNU General Public License Version 3 in the root directory of this
// project.

#include "org_photonvision_ffmpeg_FfmpegRtspHandler.h"

#include "RtspClientsMap.hpp"
#include <opencv2/core.hpp>

/*
 * Class:     org_photonvision_ffmpeg_FfmpegRtspHandler
 * Method:    initialize
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL
Java_org_photonvision_ffmpeg_FfmpegRtspHandler_initialize
  (JNIEnv *, jclass)
{
  StartRtspServerLoop();
  return true;
}

/*
 * Class:     org_photonvision_ffmpeg_FfmpegRtspHandler
 * Method:    putFrame
 * Signature: (Ljava/lang/String;J)Z
 */
JNIEXPORT jboolean JNICALL
Java_org_photonvision_ffmpeg_FfmpegRtspHandler_putFrame
  (JNIEnv *env, jclass, jstring cameraName, jlong matPtr)
{
  cv::Mat *mat = reinterpret_cast<cv::Mat *>(matPtr);

  const char *cameraNameChars = env->GetStringUTFChars(cameraName, nullptr);
  std::string cameraNameStr(cameraNameChars);
  env->ReleaseStringUTFChars(cameraName, cameraNameChars);

  return PublishCameraFrame(cameraNameStr, *mat);
}
