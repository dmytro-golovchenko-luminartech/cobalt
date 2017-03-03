// Copyright 2017 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "starboard/android/shared/media_codec_bridge.h"

namespace starboard {
namespace android {
namespace shared {

// static
scoped_ptr<MediaCodecBridge> MediaCodecBridge::CreateAudioMediaCodecBridge(
    const std::string& mime,
    const SbMediaAudioHeader& audio_header) {
  JniEnvExt* env = JniEnvExt::Get();
  jstring j_mime = env->NewStringUTF(mime.c_str());
  jobject j_media_codec_bridge = env->CallStaticObjectMethod(
      "foo/cobalt/media/MediaCodecBridge", "createAudioMediaCodecBridge",
      "(Ljava/lang/String;ZZII)Lfoo/cobalt/media/MediaCodecBridge;", j_mime,
      false, false, audio_header.samples_per_second,
      audio_header.number_of_channels);

  if (!j_media_codec_bridge) {
    return scoped_ptr<MediaCodecBridge>(NULL);
  }

  j_media_codec_bridge = env->ConvertLocalRefToGlobalRef(j_media_codec_bridge);
  return scoped_ptr<MediaCodecBridge>(
      new MediaCodecBridge(j_media_codec_bridge));
}

MediaCodecBridge::~MediaCodecBridge() {
  JniEnvExt* env = JniEnvExt::Get();
  SB_DCHECK(j_media_codec_bridge_);
  env->CallVoidMethod(j_media_codec_bridge_, "release", "()V");
  env->DeleteGlobalRef(j_media_codec_bridge_);
  j_media_codec_bridge_ = NULL;
}

DequeueInputResult MediaCodecBridge::DequeueInputBuffer(jlong timeout_us) {
  JniEnvExt* env = JniEnvExt::Get();
  ScopedLocalJavaRef j_dequeue_input_result(env->CallObjectMethod(
      j_media_codec_bridge_, "dequeueInputBuffer",
      "(J)Lfoo/cobalt/media/MediaCodecBridge$DequeueInputResult;", timeout_us));
  return {env->CallIntMethod(j_dequeue_input_result.Get(), "status", "()I"),
          env->CallIntMethod(j_dequeue_input_result.Get(), "index", "()I")};
}

jobject MediaCodecBridge::GetInputBuffer(jint index) {
  SB_DCHECK(index >= 0);
  return JniEnvExt::Get()->CallObjectMethod(j_media_codec_bridge_,
                                            "getInputBuffer",
                                            "(I)Ljava/nio/ByteBuffer;", index);
}

jint MediaCodecBridge::QueueInputBuffer(jint index,
                                        jint offset,
                                        jint size,
                                        jlong presentation_time_microseconds,
                                        jint flags) {
  return JniEnvExt::Get()->CallIntMethod(
      j_media_codec_bridge_, "queueInputBuffer", "(IIIJI)I", index, offset,
      size, presentation_time_microseconds, flags);
}

DequeueOutputResult MediaCodecBridge::DequeueOutputBuffer(jlong timeout_us) {
  JniEnvExt* env = JniEnvExt::Get();
  ScopedLocalJavaRef j_dequeue_output_result(env->CallObjectMethod(
      j_media_codec_bridge_, "dequeueOutputBuffer",
      "(J)Lfoo/cobalt/media/MediaCodecBridge$DequeueOutputResult;",
      timeout_us));
  return {env->CallIntMethod(j_dequeue_output_result.Get(), "status", "()I"),
          env->CallIntMethod(j_dequeue_output_result.Get(), "index", "()I"),
          env->CallIntMethod(j_dequeue_output_result.Get(), "flags", "()I"),
          env->CallIntMethod(j_dequeue_output_result.Get(), "offset", "()I"),
          env->CallLongMethod(j_dequeue_output_result.Get(),
                              "presentationTimeMicroseconds", "()J"),
          env->CallIntMethod(j_dequeue_output_result.Get(), "numBytes", "()I")};
}

jobject MediaCodecBridge::GetOutputBuffer(jint index) {
  SB_DCHECK(index >= 0);
  return JniEnvExt::Get()->CallObjectMethod(j_media_codec_bridge_,
                                            "getOutputBuffer",
                                            "(I)Ljava/nio/ByteBuffer;", index);
}

void MediaCodecBridge::ReleaseOutputBuffer(jint index, jboolean render) {
  JniEnvExt::Get()->CallVoidMethod(j_media_codec_bridge_, "releaseOutputBuffer",
                                   "(IZ)V", index, render);
}

jint MediaCodecBridge::Flush() {
  return JniEnvExt::Get()->CallIntMethod(j_media_codec_bridge_, "flush", "()I");
}

MediaCodecBridge::MediaCodecBridge(jobject j_media_codec_bridge)
    : j_media_codec_bridge_(j_media_codec_bridge) {
  SB_DCHECK(j_media_codec_bridge_);
  SB_DCHECK(JniEnvExt::Get()->GetObjectRefType(j_media_codec_bridge_) ==
            JNIGlobalRefType);
}

}  // namespace shared
}  // namespace android
}  // namespace starboard
