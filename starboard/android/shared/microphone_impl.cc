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

#include "starboard/shared/starboard/microphone/microphone_internal.h"

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include <algorithm>
#include <queue>

#include "starboard/log.h"
#include "starboard/memory.h"
#include "starboard/mutex.h"
#include "starboard/once.h"
#include "starboard/shared/starboard/thread_checker.h"

namespace starboard {
namespace android {
namespace shared {
namespace {
const int kMinReadSamples = 480;
// Minimum read size per read call.
const int kMinReadSize = kMinReadSamples * sizeof(int16_t);
const int kSampleRateInHz = 16000;
const int kSampleRateInMillihertz = kSampleRateInHz * 1000;
const int kNumOfOpenSLESBuffers = 2;

bool CheckReturnValue(SLresult result) {
  SB_DCHECK(result == SL_RESULT_SUCCESS) << result;
  return result == SL_RESULT_SUCCESS;
}
}  // namespace

class SbMicrophoneImpl : public SbMicrophonePrivate {
 public:
  SbMicrophoneImpl();
  ~SbMicrophoneImpl() SB_OVERRIDE;

  bool Open() SB_OVERRIDE;
  bool Close() SB_OVERRIDE;
  int Read(void* out_audio_data, int audio_data_size) SB_OVERRIDE;

  bool is_valid() const { return is_valid_; }

 private:
  enum State { kOpened, kClosed };

  static void SwapAndPublishBuffer(SLAndroidSimpleBufferQueueItf buf_obj,
                                   void* context);
  void SwapAndPublishBuffer();

  bool CreateAudioRecoder();
  void DeleteAudioRecoder();

  void ClearBuffer();

  SLObjectItf engine_object_;
  SLEngineItf engine_;
  SLObjectItf recorder_object_;
  SLRecordItf recorder_;
  SLAndroidSimpleBufferQueueItf buffer_object_;
  SLAndroidConfigurationItf config_object_;

  // Microphone information.
  SbMicrophoneInfo info_;
  // Used to track the microphone state.
  State state_;
  // Record if audio recorder is created successfully.
  bool is_valid_;
  // Ready for read.
  std::queue<int16_t*> ready_queue_;
  // Delivered to BufferQueue.
  std::queue<int16_t*> delivered_queue_;
  // Used to synchronize the calls of microphone and the callback from audio
  // recorder.
  Mutex mutex_;
  // Check if all the calls are from the same thread.
  starboard::shared::starboard::ThreadChecker thread_checker_;
};

SbMicrophoneImpl::SbMicrophoneImpl()
    : engine_object_(NULL),
      engine_(NULL),
      recorder_object_(NULL),
      recorder_(NULL),
      buffer_object_(NULL),
      config_object_(NULL),
      state_(kClosed) {
  is_valid_ = CreateAudioRecoder();
}

SbMicrophoneImpl::~SbMicrophoneImpl() {
  SB_DCHECK(thread_checker_.CalledOnValidThread());

  Close();
  DeleteAudioRecoder();

  // Destroy engine object.
  if (engine_object_) {
    (*engine_object_)->Destroy(engine_object_);
  }
  engine_ = NULL;
  engine_object_ = NULL;
}

bool SbMicrophoneImpl::Open() {
  SB_DCHECK(thread_checker_.CalledOnValidThread());
  starboard::ScopedLock lock(mutex_);

  // If the microphone has already been started, the following microphone open
  // call will clear the unread buffer. See starboard/microphone.h.
  ClearBuffer();

  if (state_ == kOpened) {
    // Already opened.
    return true;
  }

  // Enqueues kNumOfOpenSLESBuffers zero buffers to get the ball rolling.
  // Add buffers to the queue before changing state to ensure that recording
  // starts as soon as the state is modified.
  for (int i = 0; i < kNumOfOpenSLESBuffers; ++i) {
    int16_t* buffer = new int16_t[kMinReadSamples];
    SbMemorySet(buffer, 0, kMinReadSize);
    delivered_queue_.push(buffer);
    SLresult result =
        (*buffer_object_)->Enqueue(buffer_object_, buffer, kMinReadSize);
    if (!CheckReturnValue(result)) {
      return false;
    }
  }

  // Start the recording by setting the state to |SL_RECORDSTATE_RECORDING|.
  // When the object is in the SL_RECORDSTATE_RECORDING state, adding buffers
  // will implicitly start the filling process.
  SLresult result;
  result = (*recorder_)->SetRecordState(recorder_, SL_RECORDSTATE_RECORDING);
  if (!CheckReturnValue(result)) {
    return false;
  }

  state_ = kOpened;
  return true;
}

bool SbMicrophoneImpl::Close() {
  SB_DCHECK(thread_checker_.CalledOnValidThread());
  starboard::ScopedLock lock(mutex_);

  if (state_ == kClosed) {
    // Already closed.
    return true;
  }

  SLresult result;
  // Stop recording by setting the record state to |SL_RECORDSTATE_STOPPED|.
  result = (*recorder_)->SetRecordState(recorder_, SL_RECORDSTATE_STOPPED);
  if (!CheckReturnValue(result)) {
    return false;
  }

  ClearBuffer();

  state_ = kClosed;
  return true;
}

int SbMicrophoneImpl::Read(void* out_audio_data, int audio_data_size) {
  SB_DCHECK(thread_checker_.CalledOnValidThread());
  starboard::ScopedLock lock(mutex_);

  if (state_ != kOpened) {
    return -1;
  }

  if (!out_audio_data || audio_data_size == 0) {
    // No data is required.
    return 0;
  }

  if (ready_queue_.empty()) {
    return 0;
  }

  SB_DCHECK(audio_data_size >= kMinReadSize);
  int16_t* buffer = ready_queue_.front();
  SbMemoryCopy(out_audio_data, buffer, kMinReadSize);
  ready_queue_.pop();
  delete[] buffer;
  return kMinReadSize;
}

// static
void SbMicrophoneImpl::SwapAndPublishBuffer(
    SLAndroidSimpleBufferQueueItf buffer_object,
    void* context) {
  SbMicrophoneImpl* recorder = static_cast<SbMicrophoneImpl*>(context);
  recorder->SwapAndPublishBuffer();
}

void SbMicrophoneImpl::SwapAndPublishBuffer() {
  starboard::ScopedLock lock(mutex_);

  if (!delivered_queue_.empty()) {
    // When this function is called, the front item in the delivered queue
    // already has the buffered data, so remove the front item from the
    // delivered queue and save onto ready queue for future reads.
    int16_t* buffer = delivered_queue_.front();
    delivered_queue_.pop();
    ready_queue_.push(buffer);
  }

  if (state_ == kOpened) {
    int16_t* buffer = new int16_t[kMinReadSamples];
    SbMemorySet(buffer, 0, kMinReadSize);
    delivered_queue_.push(buffer);
    SLresult result =
        (*buffer_object_)->Enqueue(buffer_object_, buffer, kMinReadSize);
    CheckReturnValue(result);
  }
}

bool SbMicrophoneImpl::CreateAudioRecoder() {
  SB_DCHECK(thread_checker_.CalledOnValidThread());

  SLresult result;
  // Initializes the engine object with specific option.
  // OpenSL ES for Android is designed for multi-threaded applications and
  // is thread-safe.
  result = slCreateEngine(&engine_object_, 0, NULL, 0, NULL, NULL);
  if (!CheckReturnValue(result)) {
    return false;
  }

  // Realize the SL engine object in synchronous mode.
  result = (*engine_object_)
               ->Realize(engine_object_, /* async = */ SL_BOOLEAN_FALSE);
  if (!CheckReturnValue(result)) {
    return false;
  }

  // Get the SL engine interface.
  result =
      (*engine_object_)->GetInterface(engine_object_, SL_IID_ENGINE, &engine_);
  if (!CheckReturnValue(result)) {
    return false;
  }

  // Audio source configuration.
  SLDataLocator_IODevice input_dev_locator = {
      SL_DATALOCATOR_IODEVICE, SL_IODEVICE_AUDIOINPUT,
      SL_DEFAULTDEVICEID_AUDIOINPUT, NULL};

  SLDataSource audio_source = {&input_dev_locator, NULL};
  SLDataLocator_AndroidSimpleBufferQueue simple_buffer_queue = {
      SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, kNumOfOpenSLESBuffers};

  SLAndroidDataFormat_PCM_EX format = {
      SL_ANDROID_DATAFORMAT_PCM_EX, 1 /* numChannels */,
      kSampleRateInMillihertz,      SL_PCMSAMPLEFORMAT_FIXED_16,
      SL_PCMSAMPLEFORMAT_FIXED_16,  SL_SPEAKER_FRONT_CENTER,
      SL_BYTEORDER_LITTLEENDIAN,    SL_ANDROID_PCM_REPRESENTATION_SIGNED_INT};
  SLDataSink audio_sink = {&simple_buffer_queue, &format};

  const int kCount = 2;
  const SLInterfaceID kInterfaceId[kCount] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                                              SL_IID_ANDROIDCONFIGURATION};
  const SLboolean kInterfaceRequired[kCount] = {SL_BOOLEAN_TRUE,
                                                SL_BOOLEAN_TRUE};
  // Create audio recorder.
  result = (*engine_)->CreateAudioRecorder(engine_, &recorder_object_,
                                           &audio_source, &audio_sink, kCount,
                                           kInterfaceId, kInterfaceRequired);
  if (!CheckReturnValue(result)) {
    return false;
  }

  // Configure the audio recorder (before it is realized).
  // Get configuration interface.
  result = (*recorder_object_)
               ->GetInterface(recorder_object_, SL_IID_ANDROIDCONFIGURATION,
                              &config_object_);
  if (!CheckReturnValue(result)) {
    return false;
  }

  // Uses the main microphone tuned for voice recognition.
  const SLuint32 kPresetValue = SL_ANDROID_RECORDING_PRESET_VOICE_RECOGNITION;
  result =
      (*config_object_)
          ->SetConfiguration(config_object_, SL_ANDROID_KEY_RECORDING_PRESET,
                             &kPresetValue, sizeof(SLuint32));
  if (!CheckReturnValue(result)) {
    return false;
  }

  // Realizing the recorder in synchronous mode.
  result = (*recorder_object_)
               ->Realize(recorder_object_, /* async = */ SL_BOOLEAN_FALSE);
  if (!CheckReturnValue(result)) {
    return false;
  }

  // Get the record interface. It is an implicit interface.
  result = (*recorder_object_)
               ->GetInterface(recorder_object_, SL_IID_RECORD, &recorder_);
  if (!CheckReturnValue(result)) {
    return false;
  }

  // Get the buffer queue interface which was explicitly requested.
  result = (*recorder_object_)
               ->GetInterface(recorder_object_, SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                              &buffer_object_);
  if (!CheckReturnValue(result)) {
    return false;
  }

  // Setup to receive buffer queue event callbacks.
  result =
      (*buffer_object_)
          ->RegisterCallback(buffer_object_,
                             &SbMicrophoneImpl::SwapAndPublishBuffer, this);
  if (!CheckReturnValue(result)) {
    return false;
  }

  return true;
}

void SbMicrophoneImpl::DeleteAudioRecoder() {
  SB_DCHECK(thread_checker_.CalledOnValidThread());

  if (recorder_object_) {
    (*recorder_object_)->Destroy(recorder_object_);
  }

  config_object_ = NULL;
  buffer_object_ = NULL;
  recorder_ = NULL;
  recorder_object_ = NULL;
}

void SbMicrophoneImpl::ClearBuffer() {
  SB_DCHECK(thread_checker_.CalledOnValidThread());

  SLresult result;
  // Clear the buffer queue to get rid of old data when resuming recording.
  result = (*buffer_object_)->Clear(buffer_object_);
  CheckReturnValue(result);

  while (!delivered_queue_.empty()) {
    delete[] delivered_queue_.front();
    delivered_queue_.pop();
  }

  while (!ready_queue_.empty()) {
    delete[] ready_queue_.front();
    ready_queue_.pop();
  }
}

}  // namespace shared
}  // namespace android
}  // namespace starboard

int SbMicrophonePrivate::GetAvailableMicrophones(
    SbMicrophoneInfo* out_info_array,
    int info_array_size) {
  // TODO: Detect if a microphone is available.
  if (out_info_array && info_array_size > 0) {
    // Only support one microphone.
    out_info_array[0].id = reinterpret_cast<SbMicrophoneId>(1);
    out_info_array[0].type = kSbMicrophoneUnknown;
    out_info_array[0].max_sample_rate_hz =
        starboard::android::shared::kSampleRateInHz;
    out_info_array[0].min_read_size = starboard::android::shared::kMinReadSize;
  }

  return 1;
}

bool SbMicrophonePrivate::IsMicrophoneSampleRateSupported(
    SbMicrophoneId id,
    int sample_rate_in_hz) {
  if (!SbMicrophoneIdIsValid(id)) {
    return false;
  }

  return sample_rate_in_hz == starboard::android::shared::kSampleRateInHz;
}

namespace {
const int kUnusedBufferSize = 32 * 1024;
// We only support a single microphone.
//
// Control to initialize s_microphone.
SbOnceControl s_instance_control = SB_ONCE_INITIALIZER;
SbMicrophone s_microphone = kSbMicrophoneInvalid;

void Initialize() {
  starboard::android::shared::SbMicrophoneImpl* microphone =
      new starboard::android::shared::SbMicrophoneImpl();
  if (!microphone->is_valid()) {
    delete microphone;
    microphone = NULL;
  }

  s_microphone = microphone;
}

SbMicrophone GetMicrophone() {
  SbOnce(&s_instance_control, &Initialize);
  return s_microphone;
}

}  // namespace

SbMicrophone SbMicrophonePrivate::CreateMicrophone(SbMicrophoneId id,
                                                   int sample_rate_in_hz,
                                                   int buffer_size_bytes) {
  if (!SbMicrophoneIdIsValid(id) ||
      !IsMicrophoneSampleRateSupported(id, sample_rate_in_hz) ||
      buffer_size_bytes > kUnusedBufferSize || buffer_size_bytes <= 0) {
    return kSbMicrophoneInvalid;
  }

  return GetMicrophone();
}

void SbMicrophonePrivate::DestroyMicrophone(SbMicrophone microphone) {
  if (!SbMicrophoneIsValid(microphone)) {
    return;
  }

  SB_DCHECK(s_microphone == microphone);
  s_microphone->Close();

  delete s_microphone;
  s_microphone = kSbMicrophoneInvalid;
}
