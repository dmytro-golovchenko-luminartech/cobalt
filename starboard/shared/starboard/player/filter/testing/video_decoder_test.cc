// Copyright 2018 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/shared/starboard/player/filter/video_decoder_internal.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <set>

#include "starboard/common/condition_variable.h"
#include "starboard/common/mutex.h"
#include "starboard/common/scoped_ptr.h"
#include "starboard/common/string.h"
#include "starboard/configuration_constants.h"
#include "starboard/drm.h"
#include "starboard/media.h"
#include "starboard/memory.h"
#include "starboard/shared/starboard/media/media_util.h"
#include "starboard/shared/starboard/player/filter/stub_player_components_factory.h"
#include "starboard/shared/starboard/player/filter/testing/test_util.h"
#include "starboard/shared/starboard/player/job_queue.h"
#include "starboard/shared/starboard/player/video_dmp_reader.h"
#include "starboard/testing/fake_graphics_context_provider.h"
#include "starboard/thread.h"
#include "starboard/time.h"
#include "testing/gtest/include/gtest/gtest.h"

// This has to be defined in the global namespace as its instance will be used
// as SbPlayer.
struct SbPlayerPrivate {};

namespace starboard {
namespace shared {
namespace starboard {
namespace player {
namespace filter {
namespace testing {
namespace {

using ::starboard::testing::FakeGraphicsContextProvider;
using ::std::placeholders::_1;
using ::std::placeholders::_2;
using ::testing::AssertionFailure;
using ::testing::AssertionResult;
using ::testing::AssertionSuccess;
using ::testing::Bool;
using ::testing::Combine;
using ::testing::ValuesIn;
using video_dmp::VideoDmpReader;

const SbTimeMonotonic kDefaultWaitForNextEventTimeOut = 5 * kSbTimeSecond;

AssertionResult AlmostEqualTime(SbTime time1, SbTime time2) {
  const SbTime kEpsilon = kSbTimeSecond / 1000;
  SbTime diff = time1 - time2;
  if (-kEpsilon <= diff && diff <= kEpsilon) {
    return AssertionSuccess();
  }
  return AssertionFailure()
         << "time " << time1 << " doesn't match with time " << time2;
}

#if SB_HAS(PLAYER_CREATION_AND_OUTPUT_MODE_QUERY_IMPROVEMENT)
shared::starboard::media::VideoSampleInfo CreateVideoSampleInfo(
    SbMediaVideoCodec codec) {
  shared::starboard::media::VideoSampleInfo video_sample_info = {};

  video_sample_info.codec = codec;
  video_sample_info.mime = "";
  video_sample_info.max_video_capabilities = "";

  video_sample_info.color_metadata.primaries = kSbMediaPrimaryIdBt709;
  video_sample_info.color_metadata.transfer = kSbMediaTransferIdBt709;
  video_sample_info.color_metadata.matrix = kSbMediaMatrixIdBt709;
  video_sample_info.color_metadata.range = kSbMediaRangeIdLimited;

  video_sample_info.frame_width = 1920;
  video_sample_info.frame_height = 1080;

  return video_sample_info;
}
#endif  // SB_HAS(PLAYER_CREATION_AND_OUTPUT_MODE_QUERY_IMPROVEMENT)

class VideoDecoderTestFixture {
 public:
  enum Status {
    kNeedMoreInput = VideoDecoder::kNeedMoreInput,
    kBufferFull = VideoDecoder::kBufferFull,
    kError,
    kTimeout
  };

  struct Event {
    Status status;
    scoped_refptr<VideoFrame> frame;

    Event() : status(kNeedMoreInput) {}
    Event(Status status, scoped_refptr<VideoFrame> frame)
        : status(status), frame(frame) {}
  };

  // This function is called inside WriteMultipleInputs() whenever an event has
  // been processed.
  // |continue_process| will always be a valid pointer and always contains
  // |true| when calling this callback.  The callback can set it to false to
  // stop further processing.
  typedef std::function<void(const Event&, bool* continue_process)> EventCB;

  VideoDecoderTestFixture(
      JobQueue* job_queue,
      FakeGraphicsContextProvider* fake_graphics_context_provider,
      const char* test_filename,
      SbPlayerOutputMode output_mode,
      bool using_stub_decoder)
      : job_queue_(job_queue),
        fake_graphics_context_provider_(fake_graphics_context_provider),
        test_filename_(test_filename),
        output_mode_(output_mode),
        using_stub_decoder_(using_stub_decoder),
        dmp_reader_(ResolveTestFileName(test_filename).c_str()) {
    SB_DCHECK(job_queue_);
    SB_DCHECK(fake_graphics_context_provider_);
    SB_LOG(INFO) << "Testing " << test_filename_ << ", output mode "
                 << output_mode_
                 << (using_stub_decoder_ ? " with stub video decoder." : ".");
  }

  ~VideoDecoderTestFixture() { video_decoder_->Reset(); }

  void Initialize() {
    ASSERT_NE(dmp_reader_.video_codec(), kSbMediaVideoCodecNone);
    ASSERT_GT(dmp_reader_.number_of_video_buffers(), 0);
    ASSERT_TRUE(GetVideoInputBuffer(0)->video_sample_info().is_key_frame);

    SbPlayerOutputMode output_mode = output_mode_;
    ASSERT_TRUE(VideoDecoder::OutputModeSupported(
        output_mode, dmp_reader_.video_codec(), kSbDrmSystemInvalid));

    PlayerComponents::Factory::CreationParameters creation_parameters(
        dmp_reader_.video_codec(),
#if SB_HAS(PLAYER_CREATION_AND_OUTPUT_MODE_QUERY_IMPROVEMENT)
        GetVideoInputBuffer(0)->video_sample_info(),
#endif  // SB_HAS(PLAYER_CREATION_AND_OUTPUT_MODE_QUERY_IMPROVEMENT)
        &player_, output_mode,
        fake_graphics_context_provider_->decoder_target_provider(), nullptr);

    scoped_ptr<PlayerComponents::Factory> factory;
    if (using_stub_decoder_) {
      factory = StubPlayerComponentsFactory::Create();
    } else {
      factory = PlayerComponents::Factory::Create();
    }
    std::string error_message;
    ASSERT_TRUE(factory->CreateSubComponents(
        creation_parameters, nullptr, nullptr, &video_decoder_,
        &video_render_algorithm_, &video_renderer_sink_, &error_message));
    ASSERT_TRUE(video_decoder_);

    if (video_renderer_sink_) {
      video_renderer_sink_->SetRenderCB(
          std::bind(&VideoDecoderTestFixture::Render, this, _1));
    }

    video_decoder_->Initialize(
        std::bind(&VideoDecoderTestFixture::OnDecoderStatusUpdate, this, _1,
                  _2),
        std::bind(&VideoDecoderTestFixture::OnError, this));
    if (HasPendingEvents()) {
      bool error_occurred = false;
      ASSERT_NO_FATAL_FAILURE(DrainOutputs(&error_occurred));
      ASSERT_FALSE(error_occurred);
    }
  }

  void Render(VideoRendererSink::DrawFrameCB draw_frame_cb) {
  }

  void OnDecoderStatusUpdate(VideoDecoder::Status status,
                             const scoped_refptr<VideoFrame>& frame) {
    ScopedLock scoped_lock(mutex_);
    // TODO: Ensure that this is only called during dtor or Reset().
    if (status == VideoDecoder::kReleaseAllFrames) {
      SB_DCHECK(!frame);
      event_queue_.clear();
      decoded_frames_.clear();
      return;
    } else if (status == VideoDecoder::kNeedMoreInput) {
      event_queue_.push_back(Event(kNeedMoreInput, frame));
    } else if (status == VideoDecoder::kBufferFull) {
      event_queue_.push_back(Event(kBufferFull, frame));
    } else {
      event_queue_.push_back(Event(kError, frame));
    }
  }

  void OnError() {
    ScopedLock scoped_lock(mutex_);
    event_queue_.push_back(Event(kError, NULL));
  }

#if SB_HAS(GLES2)
  void AssertInvalidDecodeTarget() {
    if (output_mode_ == kSbPlayerOutputModeDecodeToTexture &&
        !using_stub_decoder_) {
      volatile bool is_decode_target_valid = true;
      fake_graphics_context_provider_->RunOnGlesContextThread([&]() {
        SbDecodeTarget decode_target = video_decoder_->GetCurrentDecodeTarget();
        is_decode_target_valid = SbDecodeTargetIsValid(decode_target);
        SbDecodeTargetRelease(decode_target);
      });
      ASSERT_FALSE(is_decode_target_valid);
    }
  }
#endif  // SB_HAS(GLES2)

  void WaitForNextEvent(
      Event* event,
      SbTimeMonotonic timeout = kDefaultWaitForNextEventTimeOut) {
    ASSERT_TRUE(event);

    SbTimeMonotonic start = SbTimeGetMonotonicNow();
    do {
      job_queue_->RunUntilIdle();
      GetDecodeTargetWhenSupported();
      {
        ScopedLock scoped_lock(mutex_);
        if (!event_queue_.empty()) {
          *event = event_queue_.front();
          event_queue_.pop_front();
          if (event->status == kNeedMoreInput) {
            need_more_input_ = true;
          } else if (event->status == kBufferFull) {
            if (!end_of_stream_written_) {
              ASSERT_FALSE(need_more_input_);
            }
          }
          return;
        }
      }
      SbThreadSleep(kSbTimeMillisecond);
    } while (SbTimeGetMonotonicNow() - start < timeout);
    event->status = kTimeout;
  }

  bool HasPendingEvents() {
    const SbTime kDelay = 5 * kSbTimeMillisecond;
    SbThreadSleep(kDelay);
    ScopedLock scoped_lock(mutex_);
    return !event_queue_.empty();
  }

  void GetDecodeTargetWhenSupported() {
#if SB_HAS(GLES2)
    if (output_mode_ == kSbPlayerOutputModeDecodeToTexture &&
        !using_stub_decoder_) {
      fake_graphics_context_provider_->RunOnGlesContextThread([&]() {
        SbDecodeTargetRelease(video_decoder_->GetCurrentDecodeTarget());
      });
    }
#endif  // SB_HAS(GLES2)
  }

  void AssertValidDecodeTargetWhenSupported() {
#if SB_HAS(GLES2)
    volatile bool is_decode_target_valid = false;
    if (output_mode_ == kSbPlayerOutputModeDecodeToTexture &&
        !using_stub_decoder_) {
      fake_graphics_context_provider_->RunOnGlesContextThread([&]() {
        SbDecodeTarget decode_target = video_decoder_->GetCurrentDecodeTarget();
        is_decode_target_valid = SbDecodeTargetIsValid(decode_target);
        SbDecodeTargetRelease(decode_target);
      });
      ASSERT_TRUE(is_decode_target_valid);
    }
#endif  // SB_HAS(GLES2)
  }

  // This has to be called when the decoder is just initialized/reseted or when
  // status is |kNeedMoreInput|.
  void WriteSingleInput(size_t index) {
    ASSERT_TRUE(need_more_input_);
    ASSERT_LT(index, dmp_reader_.number_of_video_buffers());

    auto input_buffer = GetVideoInputBuffer(index);
    {
      ScopedLock scoped_lock(mutex_);
      need_more_input_ = false;
      outstanding_inputs_.insert(input_buffer->timestamp());
    }

    video_decoder_->WriteInputBuffer(input_buffer);
  }

  void WriteEndOfStream() {
    {
      ScopedLock scoped_lock(mutex_);
      end_of_stream_written_ = true;
    }
    video_decoder_->WriteEndOfStream();
  }

  void WriteMultipleInputs(size_t start_index,
                           size_t number_of_inputs_to_write,
                           EventCB event_cb = EventCB()) {
    ASSERT_LE(start_index + number_of_inputs_to_write,
              dmp_reader_.number_of_video_buffers());

    ASSERT_NO_FATAL_FAILURE(WriteSingleInput(start_index));
    ++start_index;
    --number_of_inputs_to_write;

    while (number_of_inputs_to_write > 0) {
      Event event;
      ASSERT_NO_FATAL_FAILURE(WaitForNextEvent(&event));
      if (event.status == kNeedMoreInput) {
        ASSERT_NO_FATAL_FAILURE(WriteSingleInput(start_index));
        ++start_index;
        --number_of_inputs_to_write;
      } else if (event.status == kError || event.status == kTimeout) {
        // Assume that the caller does't expect an error when |event_cb| isn't
        // provided.
        ASSERT_TRUE(event_cb);
        bool continue_process = true;
        event_cb(event, &continue_process);
        ASSERT_FALSE(continue_process);
        return;
      } else {
        ASSERT_EQ(event.status, kBufferFull);
      }
      if (event.frame) {
        ASSERT_FALSE(event.frame->is_end_of_stream());
        if (!decoded_frames_.empty()) {
          ASSERT_LT(decoded_frames_.back()->timestamp(),
                    event.frame->timestamp());
        }
        decoded_frames_.push_back(event.frame);
        ASSERT_TRUE(AlmostEqualTime(*outstanding_inputs_.begin(),
                                    event.frame->timestamp()));
        outstanding_inputs_.erase(outstanding_inputs_.begin());
      }
      if (event_cb) {
        bool continue_process = true;
        event_cb(event, &continue_process);
        if (!continue_process) {
          return;
        }
      }
    }
  }

  void DrainOutputs(bool* error_occurred = NULL, EventCB event_cb = EventCB()) {
    if (error_occurred) {
      *error_occurred = false;
    }

    bool end_of_stream_decoded = false;

    while (!end_of_stream_decoded) {
      Event event;
      ASSERT_NO_FATAL_FAILURE(WaitForNextEvent(&event));
      if (event.status == kError || event.status == kTimeout) {
        if (error_occurred) {
          *error_occurred = true;
        } else {
          FAIL();
        }
        return;
      }
      if (event.frame) {
        if (event.frame->is_end_of_stream()) {
          end_of_stream_decoded = true;
          if (!outstanding_inputs_.empty()) {
            SB_LOG(WARNING) << "|outstanding_inputs_| is not empty.";
            if (error_occurred) {
              *error_occurred = true;
            } else {
              // |error_occurred| is NULL indicates that the caller doesn't
              // expect an error, use the following redundant ASSERT to trigger
              // a failure.
              ASSERT_TRUE(outstanding_inputs_.empty());
            }
          }
        } else {
          if (!decoded_frames_.empty()) {
            ASSERT_LT(decoded_frames_.back()->timestamp(),
                      event.frame->timestamp());
          }
          decoded_frames_.push_back(event.frame);
          ASSERT_TRUE(AlmostEqualTime(*outstanding_inputs_.begin(),
                                      event.frame->timestamp()));
          outstanding_inputs_.erase(outstanding_inputs_.begin());
        }
      }
      if (event_cb) {
        bool continue_process = true;
        event_cb(event, &continue_process);
        if (!continue_process) {
          return;
        }
      }
    }
  }

  void ResetDecoderAndClearPendingEvents() {
    video_decoder_->Reset();
    ScopedLock scoped_lock(mutex_);
    event_queue_.clear();
    need_more_input_ = true;
    end_of_stream_written_ = false;
    outstanding_inputs_.clear();
    decoded_frames_.clear();
  }

  scoped_refptr<InputBuffer> GetVideoInputBuffer(size_t index) const {
    auto video_sample_info =
        dmp_reader_.GetPlayerSampleInfo(kSbMediaTypeVideo, index);
#if SB_API_VERSION >= 11
    auto input_buffer = new InputBuffer(StubDeallocateSampleFunc, NULL, NULL,
                                        video_sample_info);
#else   // SB_API_VERSION >= 11
    auto input_buffer =
        new InputBuffer(kSbMediaTypeVideo, StubDeallocateSampleFunc, NULL, NULL,
                        video_sample_info, NULL);
#endif  // SB_API_VERSION >= 11
    auto iter = invalid_inputs_.find(index);
    if (iter != invalid_inputs_.end()) {
      std::vector<uint8_t> content(input_buffer->size(), iter->second);
      // Replace the content with invalid data.
      input_buffer->SetDecryptedContent(content.data(),
                                        static_cast<int>(content.size()));
    }
    return input_buffer;
  }

  void UseInvalidDataForInput(size_t index, uint8_t byte_to_fill) {
    invalid_inputs_[index] = byte_to_fill;
  }
  const scoped_ptr<VideoDecoder>& video_decoder() const {
    return video_decoder_;
  }
  const VideoDmpReader& dmp_reader() const { return dmp_reader_; }
  SbPlayerOutputMode output_mode() const { return output_mode_; }
  size_t GetDecodedFramesCount() const { return decoded_frames_.size(); }
  void PopDecodedFrame() { decoded_frames_.pop_front(); }
  void ClearDecodedFrames() { decoded_frames_.clear(); }

 protected:
  JobQueue* job_queue_;

  Mutex mutex_;
  std::deque<Event> event_queue_;

  // Test parameter filename for the VideoDmpReader to load and test with.
  const char* test_filename_;

  // Test parameter for OutputMode.
  SbPlayerOutputMode output_mode_;

  // Test parameter for whether or not to use the StubVideoDecoder, or the
  // platform-specific VideoDecoderImpl.
  bool using_stub_decoder_;

  FakeGraphicsContextProvider* fake_graphics_context_provider_;
  VideoDmpReader dmp_reader_;
  scoped_ptr<VideoDecoder> video_decoder_;

  bool need_more_input_ = true;
  std::set<SbTime> outstanding_inputs_;
  std::deque<scoped_refptr<VideoFrame>> decoded_frames_;

  SbPlayerPrivate player_;
  scoped_ptr<VideoRenderAlgorithm> video_render_algorithm_;
  scoped_refptr<VideoRendererSink> video_renderer_sink_;

  bool end_of_stream_written_ = false;

  std::map<size_t, uint8_t> invalid_inputs_;
};

class VideoDecoderTest
    : public ::testing::TestWithParam<std::tuple<VideoTestParam, bool>> {
 public:
  typedef VideoDecoderTestFixture::Event Event;
  typedef VideoDecoderTestFixture::EventCB EventCB;
  typedef VideoDecoderTestFixture::Status Status;

  VideoDecoderTest()
      : fixture_(&job_queue_,
                 &fake_graphics_context_provider_,
                 std::get<0>(std::get<0>(GetParam())),
                 std::get<1>(std::get<0>(GetParam())),
                 std::get<1>(GetParam())) {}

  void SetUp() override { fixture_.Initialize(); }

 protected:
  JobQueue job_queue_;
  FakeGraphicsContextProvider fake_graphics_context_provider_;
  VideoDecoderTestFixture fixture_;
};

TEST_P(VideoDecoderTest, PrerollFrameCount) {
  EXPECT_GT(fixture_.video_decoder()->GetPrerollFrameCount(), 0);
}

TEST_P(VideoDecoderTest, MaxNumberOfCachedFrames) {
  EXPECT_GT(fixture_.video_decoder()->GetMaxNumberOfCachedFrames(), 1);
}

TEST_P(VideoDecoderTest, PrerollTimeout) {
  EXPECT_GE(fixture_.video_decoder()->GetPrerollTimeout(), 0);
}

// Ensure that OutputModeSupported() is callable on all combinations.
TEST_P(VideoDecoderTest, OutputModeSupported) {
  SbPlayerOutputMode kOutputModes[] = {kSbPlayerOutputModeDecodeToTexture,
                                       kSbPlayerOutputModePunchOut};
  SbMediaVideoCodec kVideoCodecs[] = {
    kSbMediaVideoCodecNone,
    kSbMediaVideoCodecH264,
    kSbMediaVideoCodecH265,
    kSbMediaVideoCodecMpeg2,
    kSbMediaVideoCodecTheora,
    kSbMediaVideoCodecVc1,
#if SB_API_VERSION < 11
    kSbMediaVideoCodecVp10,
#else   // SB_API_VERSION < 11
    kSbMediaVideoCodecAv1,
#endif  // SB_API_VERSION < 11
    kSbMediaVideoCodecVp8,
    kSbMediaVideoCodecVp9
  };
  for (auto output_mode : kOutputModes) {
    for (auto video_codec : kVideoCodecs) {
      VideoDecoder::OutputModeSupported(output_mode, video_codec,
                                        kSbDrmSystemInvalid);
    }
  }
}

#if SB_HAS(GLES2)
TEST_P(VideoDecoderTest, GetCurrentDecodeTargetBeforeWriteInputBuffer) {
  if (fixture_.output_mode() == kSbPlayerOutputModeDecodeToTexture) {
    fixture_.AssertInvalidDecodeTarget();
  }
}
#endif  // SB_HAS(GLES2)

TEST_P(VideoDecoderTest, ThreeMoreDecoders) {
  // Create three more decoders for each supported combinations.
  const int kDecodersToCreate = 3;

  scoped_ptr<PlayerComponents::Factory> factory =
      PlayerComponents::Factory::Create();

  SbPlayerOutputMode kOutputModes[] = {kSbPlayerOutputModeDecodeToTexture,
                                       kSbPlayerOutputModePunchOut};
  SbMediaVideoCodec kVideoCodecs[] = {
    kSbMediaVideoCodecNone,
    kSbMediaVideoCodecH264,
    kSbMediaVideoCodecH265,
    kSbMediaVideoCodecMpeg2,
    kSbMediaVideoCodecTheora,
    kSbMediaVideoCodecVc1,
#if SB_API_VERSION < 11
    kSbMediaVideoCodecVp10,
#else   // SB_API_VERSION < 11
    kSbMediaVideoCodecAv1,
#endif  // SB_API_VERSION < 11
    kSbMediaVideoCodecVp8,
    kSbMediaVideoCodecVp9
  };

  for (auto output_mode : kOutputModes) {
    for (auto video_codec : kVideoCodecs) {
      if (VideoDecoder::OutputModeSupported(output_mode, video_codec,
                                            kSbDrmSystemInvalid)) {
        SbPlayerPrivate players[kDecodersToCreate];
        scoped_ptr<VideoDecoder> video_decoders[kDecodersToCreate];
        scoped_ptr<VideoRenderAlgorithm>
            video_render_algorithms[kDecodersToCreate];
        scoped_refptr<VideoRendererSink>
            video_renderer_sinks[kDecodersToCreate];

        for (int i = 0; i < kDecodersToCreate; ++i) {
          SbMediaAudioSampleInfo dummy_audio_sample_info = {
#if SB_API_VERSION >= 11
            kSbMediaAudioCodecNone
#endif  // SB_API_VERSION >= 11
          };
          PlayerComponents::Factory::CreationParameters creation_parameters(
              fixture_.dmp_reader().video_codec(),
#if SB_HAS(PLAYER_CREATION_AND_OUTPUT_MODE_QUERY_IMPROVEMENT)
              CreateVideoSampleInfo(fixture_.dmp_reader().video_codec()),
#endif  // SB_HAS(PLAYER_CREATION_AND_OUTPUT_MODE_QUERY_IMPROVEMENT)
              &players[i], output_mode,
              fake_graphics_context_provider_.decoder_target_provider(),
              nullptr);

          std::string error_message;
          ASSERT_TRUE(factory->CreateSubComponents(
              creation_parameters, nullptr, nullptr, &video_decoders[i],
              &video_render_algorithms[i], &video_renderer_sinks[i],
              &error_message));
          ASSERT_TRUE(video_decoders[i]);

          if (video_renderer_sinks[i]) {
            video_renderer_sinks[i]->SetRenderCB(
                std::bind(&VideoDecoderTestFixture::Render, &fixture_, _1));
          }

          video_decoders[i]->Initialize(
              std::bind(&VideoDecoderTestFixture::OnDecoderStatusUpdate,
                        &fixture_, _1, _2),
              std::bind(&VideoDecoderTestFixture::OnError, &fixture_));

#if SB_HAS(GLES2)
          if (output_mode == kSbPlayerOutputModeDecodeToTexture) {
            fixture_.AssertInvalidDecodeTarget();
          }
#endif  // SB_HAS(GLES2)
        }
        if (fixture_.HasPendingEvents()) {
          bool error_occurred = false;
          ASSERT_NO_FATAL_FAILURE(fixture_.DrainOutputs(&error_occurred));
          ASSERT_FALSE(error_occurred);
        }
      }
    }
  }
}

TEST_P(VideoDecoderTest, SingleInput) {
  fixture_.WriteSingleInput(0);
  fixture_.WriteEndOfStream();

  bool error_occurred = false;
  ASSERT_NO_FATAL_FAILURE(fixture_.DrainOutputs(
      &error_occurred, [=](const Event& event, bool* continue_process) {
        if (event.frame) {
          // TODO: On some platforms, decode texture will be ready only after
          // rendered by renderer, so decode target is not always available
          // at this point. We should provide a mock renderer and then check
          // the decode target here with AssertValidDecodeTargetWhenSupported().
        }
        *continue_process = true;
      }));
  ASSERT_FALSE(error_occurred);
}

TEST_P(VideoDecoderTest, SingleInvalidKeyFrame) {
  fixture_.UseInvalidDataForInput(0, 0xab);

  fixture_.WriteSingleInput(0);
  fixture_.WriteEndOfStream();

  bool error_occurred = true;
  ASSERT_NO_FATAL_FAILURE(fixture_.DrainOutputs(&error_occurred));
  // We don't expect the video decoder can always recover from a bad key frame
  // and to raise an error, but it shouldn't crash or hang.
  fixture_.GetDecodeTargetWhenSupported();
}

TEST_P(VideoDecoderTest, MultipleValidInputsAfterInvalidKeyFrame) {
  const size_t kMaxNumberOfInputToWrite = 10;
  const size_t number_of_input_to_write =
      std::min(kMaxNumberOfInputToWrite,
               fixture_.dmp_reader().number_of_video_buffers());

  fixture_.UseInvalidDataForInput(0, 0xab);

  bool error_occurred = false;
  bool timeout_occurred = false;
  // Write first few frames.  The first one is invalid and the rest are valid.
  fixture_.WriteMultipleInputs(0, number_of_input_to_write,
                               [&](const Event& event, bool* continue_process) {
                                 if (event.status == Status::kTimeout) {
                                   timeout_occurred = true;
                                   *continue_process = false;
                                   return;
                                 }
                                 if (event.status == Status::kError) {
                                   error_occurred = true;
                                   *continue_process = false;
                                   return;
                                 }
                                 *continue_process =
                                     event.status != Status::kBufferFull;
                               });
  ASSERT_FALSE(timeout_occurred);
  if (!error_occurred) {
    fixture_.GetDecodeTargetWhenSupported();
    fixture_.WriteEndOfStream();
    ASSERT_NO_FATAL_FAILURE(fixture_.DrainOutputs(&error_occurred));
  }
  // We don't expect the video decoder can always recover from a bad key frame
  // and to raise an error, but it shouldn't crash or hang.
  fixture_.GetDecodeTargetWhenSupported();
}

TEST_P(VideoDecoderTest, MultipleInvalidInput) {
  const size_t kMaxNumberOfInputToWrite = 128;
  const size_t number_of_input_to_write =
      std::min(kMaxNumberOfInputToWrite,
               fixture_.dmp_reader().number_of_video_buffers());
  // Replace the content of the first few input buffers with invalid data.
  // Every test instance loads its own copy of data so this won't affect other
  // tests.
  for (size_t i = 0; i < number_of_input_to_write; ++i) {
    fixture_.UseInvalidDataForInput(i, static_cast<uint8_t>(0xab + i));
  }

  bool error_occurred = false;
  bool timeout_occurred = false;
  fixture_.WriteMultipleInputs(0, number_of_input_to_write,
                               [&](const Event& event, bool* continue_process) {
                                 if (event.status == Status::kTimeout) {
                                   timeout_occurred = true;
                                   *continue_process = false;
                                   return;
                                 }
                                 if (event.status == Status::kError) {
                                   error_occurred = true;
                                   *continue_process = false;
                                   return;
                                 }

                                 *continue_process =
                                     event.status != Status::kBufferFull;
                               });
  ASSERT_FALSE(timeout_occurred);
  if (!error_occurred) {
    fixture_.GetDecodeTargetWhenSupported();
    fixture_.WriteEndOfStream();
    ASSERT_NO_FATAL_FAILURE(fixture_.DrainOutputs(&error_occurred));
  }
  // We don't expect the video decoder can always recover from a bad key frame
  // and to raise an error, but it shouldn't crash or hang.
  fixture_.GetDecodeTargetWhenSupported();
}

TEST_P(VideoDecoderTest, EndOfStreamWithoutAnyInput) {
  fixture_.WriteEndOfStream();
  ASSERT_NO_FATAL_FAILURE(fixture_.DrainOutputs());
}

TEST_P(VideoDecoderTest, ResetBeforeInput) {
  EXPECT_FALSE(fixture_.HasPendingEvents());
  fixture_.ResetDecoderAndClearPendingEvents();
  EXPECT_FALSE(fixture_.HasPendingEvents());

  fixture_.WriteSingleInput(0);
  fixture_.WriteEndOfStream();
  ASSERT_NO_FATAL_FAILURE(fixture_.DrainOutputs());
}

TEST_P(VideoDecoderTest, ResetAfterInput) {
  const size_t max_inputs_to_write =
      std::min<size_t>(fixture_.dmp_reader().number_of_video_buffers(), 10);
  bool error_occurred = false;
  fixture_.WriteMultipleInputs(
      0, max_inputs_to_write, [&](const Event& event, bool* continue_process) {
        if (event.status == Status::kTimeout ||
            event.status == Status::kError) {
          error_occurred = true;
          *continue_process = false;
          return;
        }
        *continue_process = event.status != Status::kBufferFull;
      });
  ASSERT_FALSE(error_occurred);
  fixture_.ResetDecoderAndClearPendingEvents();
  EXPECT_FALSE(fixture_.HasPendingEvents());
}

TEST_P(VideoDecoderTest, MultipleResets) {
  const size_t max_inputs_to_write =
      std::min<size_t>(fixture_.dmp_reader().number_of_video_buffers(), 10);
  for (int max_inputs = 1; max_inputs < max_inputs_to_write; ++max_inputs) {
    bool error_occurred = false;
    fixture_.WriteMultipleInputs(
        0, max_inputs, [&](const Event& event, bool* continue_process) {
          if (event.status == Status::kTimeout ||
              event.status == Status::kError) {
            error_occurred = true;
            *continue_process = false;
            return;
          }
          *continue_process = event.status != Status::kBufferFull;
        });
    ASSERT_FALSE(error_occurred);
    fixture_.ResetDecoderAndClearPendingEvents();
    EXPECT_FALSE(fixture_.HasPendingEvents());
    fixture_.WriteSingleInput(0);
    fixture_.WriteEndOfStream();
    ASSERT_NO_FATAL_FAILURE(fixture_.DrainOutputs());
    fixture_.ResetDecoderAndClearPendingEvents();
    EXPECT_FALSE(fixture_.HasPendingEvents());
  }
}

TEST_P(VideoDecoderTest, MultipleInputs) {
  const size_t kMaxNumberOfExpectedDecodedFrames = 5;
  const size_t number_of_expected_decoded_frames =
      std::min(kMaxNumberOfExpectedDecodedFrames,
               fixture_.dmp_reader().number_of_video_buffers());
  size_t frames_decoded = 0;
  bool error_occurred = false;
  ASSERT_NO_FATAL_FAILURE(fixture_.WriteMultipleInputs(
      0, fixture_.dmp_reader().number_of_video_buffers(),
      [&](const Event& event, bool* continue_process) {
        if (event.status == Status::kTimeout ||
            event.status == Status::kError) {
          error_occurred = true;
          *continue_process = false;
          return;
        }
        frames_decoded += fixture_.GetDecodedFramesCount();
        fixture_.ClearDecodedFrames();
        *continue_process = frames_decoded < number_of_expected_decoded_frames;
      }));
  ASSERT_FALSE(error_occurred);
  if (frames_decoded < number_of_expected_decoded_frames) {
    fixture_.WriteEndOfStream();
    ASSERT_NO_FATAL_FAILURE(fixture_.DrainOutputs());
  }
}

TEST_P(VideoDecoderTest, Preroll) {
  SbTimeMonotonic start = SbTimeGetMonotonicNow();
  SbTime preroll_timeout = fixture_.video_decoder()->GetPrerollTimeout();
  bool error_occurred = false;
  ASSERT_NO_FATAL_FAILURE(fixture_.WriteMultipleInputs(
      0, fixture_.dmp_reader().number_of_video_buffers(),
      [&](const Event& event, bool* continue_process) {
        if (event.status == Status::kError) {
          error_occurred = true;
          *continue_process = false;
          return;
        }
        if (fixture_.GetDecodedFramesCount() >=
            fixture_.video_decoder()->GetPrerollFrameCount()) {
          *continue_process = false;
          return;
        }
        if (SbTimeGetMonotonicNow() - start >= preroll_timeout) {
          // After preroll timeout, we should get at least 1 decoded frame.
          ASSERT_GT(fixture_.GetDecodedFramesCount(), 0);
          *continue_process = false;
          return;
        }
        *continue_process = true;
        return;
      }));
  ASSERT_FALSE(error_occurred);
}

TEST_P(VideoDecoderTest, HoldFramesUntilFull) {
  bool error_occurred = false;
  ASSERT_NO_FATAL_FAILURE(fixture_.WriteMultipleInputs(
      0, fixture_.dmp_reader().number_of_video_buffers(),
      [&](const Event& event, bool* continue_process) {
        if (event.status == Status::kTimeout ||
            event.status == Status::kError) {
          error_occurred = true;
          *continue_process = false;
          return;
        }
        *continue_process =
            fixture_.GetDecodedFramesCount() <
            fixture_.video_decoder()->GetMaxNumberOfCachedFrames();
      }));
  ASSERT_FALSE(error_occurred);
  fixture_.WriteEndOfStream();
  if (fixture_.GetDecodedFramesCount() >=
      fixture_.video_decoder()->GetMaxNumberOfCachedFrames()) {
    return;
  }
  ASSERT_NO_FATAL_FAILURE(fixture_.DrainOutputs(
      &error_occurred, [=](const Event& event, bool* continue_process) {
        *continue_process =
            fixture_.GetDecodedFramesCount() <
            fixture_.video_decoder()->GetMaxNumberOfCachedFrames();
      }));
  ASSERT_FALSE(error_occurred);
}

TEST_P(VideoDecoderTest, DecodeFullGOP) {
  int gop_size = 1;
  while (gop_size < fixture_.dmp_reader().number_of_video_buffers()) {
    if (fixture_.GetVideoInputBuffer(gop_size)
            ->video_sample_info()
            .is_key_frame) {
      break;
    }
    ++gop_size;
  }
  bool error_occurred = false;
  ASSERT_NO_FATAL_FAILURE(fixture_.WriteMultipleInputs(
      0, gop_size, [&](const Event& event, bool* continue_process) {
        if (event.status == Status::kTimeout ||
            event.status == Status::kError) {
          error_occurred = true;
          *continue_process = false;
          return;
        }
        // Keep 1 decoded frame, assuming it's used by renderer.
        while (fixture_.GetDecodedFramesCount() > 1) {
          fixture_.PopDecodedFrame();
        }
        *continue_process = true;
      }));
  ASSERT_FALSE(error_occurred);
  fixture_.WriteEndOfStream();

  ASSERT_NO_FATAL_FAILURE(fixture_.DrainOutputs(
      &error_occurred, [=](const Event& event, bool* continue_process) {
        // Keep 1 decoded frame, assuming it's used by renderer.
        while (fixture_.GetDecodedFramesCount() > 1) {
          fixture_.PopDecodedFrame();
        }
        *continue_process = true;
      }));
  ASSERT_FALSE(error_occurred);
}

INSTANTIATE_TEST_CASE_P(VideoDecoderTests,
                        VideoDecoderTest,
                        Combine(ValuesIn(GetSupportedVideoTests()), Bool()));

}  // namespace
}  // namespace testing
}  // namespace filter
}  // namespace player
}  // namespace starboard
}  // namespace shared
}  // namespace starboard
