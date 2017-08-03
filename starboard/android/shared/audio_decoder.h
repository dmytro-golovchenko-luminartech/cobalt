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

#ifndef STARBOARD_ANDROID_SHARED_AUDIO_DECODER_H_
#define STARBOARD_ANDROID_SHARED_AUDIO_DECODER_H_

#include <jni.h>

#include <deque>
#include <queue>

#include "starboard/android/shared/drm_system.h"
#include "starboard/android/shared/media_codec_bridge.h"
#include "starboard/atomic.h"
#include "starboard/common/ref_counted.h"
#include "starboard/file.h"
#include "starboard/log.h"
#include "starboard/media.h"
#include "starboard/shared/internal_only.h"
#include "starboard/shared/starboard/player/decoded_audio_internal.h"
#include "starboard/shared/starboard/player/filter/audio_decoder_internal.h"
#include "starboard/shared/starboard/player/job_queue.h"

namespace starboard {
namespace android {
namespace shared {

class AudioDecoder
    : public ::starboard::shared::starboard::player::filter::AudioDecoder,
      private ::starboard::shared::starboard::player::JobQueue::JobOwner {
 public:
  AudioDecoder(SbMediaAudioCodec audio_codec,
               const SbMediaAudioHeader& audio_header,
               SbDrmSystem drm_system);
  ~AudioDecoder() SB_OVERRIDE;

  void Initialize(const Closure& output_cb) SB_OVERRIDE;
  void Decode(const scoped_refptr<InputBuffer>& input_buffer,
              const Closure& consumed_cb) SB_OVERRIDE;
  void WriteEndOfStream() SB_OVERRIDE;
  scoped_refptr<DecodedAudio> Read() SB_OVERRIDE;
  void Reset() SB_OVERRIDE;

  SbMediaAudioSampleType GetSampleType() const SB_OVERRIDE {
    return sample_type_;
  }
  SbMediaAudioFrameStorageType GetStorageType() const SB_OVERRIDE {
    return kSbMediaAudioFrameStorageTypeInterleaved;
  }
  int GetSamplesPerSecond() const SB_OVERRIDE {
    return audio_header_.samples_per_second;
  }

  bool is_valid() const { return media_codec_bridge_ != NULL; }

 private:
  struct Event {
    enum Type {
      kInvalid,
      kReset,
      kWriteCodecConfig,
      kWriteInputBuffer,
      kWriteEndOfStream,
    };

    explicit Event(Type type = kInvalid) : type(type) {
      SB_DCHECK(type != kWriteInputBuffer);
    }
    explicit Event(const scoped_refptr<InputBuffer>& input_buffer)
        : type(kWriteInputBuffer), input_buffer(input_buffer) {}

    Type type;
    scoped_refptr<InputBuffer> input_buffer;
  };

  // The maximum amount of work that can exist in the union of |EventQueue|,
  // |pending_work| and |decoded_audios_|.
  static const int kMaxPendingWorkSize = 64;

  static void* ThreadEntryPoint(void* context);
  void DecoderThreadFunc();
  void JoinOnDecoderThread();

  bool InitializeCodec();
  void TeardownCodec();

  bool ProcessOneInputBuffer(std::deque<Event>* pending_work);
  bool ProcessOneOutputBuffer();

  Closure output_cb_;
  Closure consumed_cb_;
  scoped_ptr<MediaCodecBridge> media_codec_bridge_;

  SbMediaAudioSampleType sample_type_;

  bool stream_ended_;
  std::queue<scoped_refptr<DecodedAudio> > decoded_audios_;
  SbMediaAudioCodec audio_codec_;
  SbMediaAudioHeader audio_header_;

  DrmSystem* drm_system_;

  // Working thread to avoid lengthy decoding work block the player thread.
  SbThread decoder_thread_;
  // Events are processed in a queue, except for when handling events of type
  // |kReset|, which are allowed to cut to the front.
  EventQueue<Event> event_queue_;
  starboard::Mutex decoded_audios_mutex_;
  volatile SbAtomic32 pending_work_size_;

  jint output_sample_rate_;
  jint output_channel_count_;
};

}  // namespace shared
}  // namespace android
}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_AUDIO_DECODER_H_
