// Copyright 2016 Google Inc. All Rights Reserved.
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

#include "starboard/shared/starboard/player/filter/filter_based_player_worker_handler.h"

#include "starboard/log.h"
#include "starboard/shared/starboard/application.h"
#include "starboard/shared/starboard/drm/drm_system_internal.h"
#include "starboard/shared/starboard/player/filter/audio_decoder_internal.h"
#include "starboard/shared/starboard/player/filter/video_decoder_internal.h"
#include "starboard/shared/starboard/player/input_buffer_internal.h"
#include "starboard/shared/starboard/player/video_frame_internal.h"
#include "starboard/time.h"

namespace starboard {
namespace shared {
namespace starboard {
namespace player {
namespace filter {

namespace {

// TODO: Make this configurable inside SbPlayerCreate().
const SbTimeMonotonic kUpdateInterval = 5 * kSbTimeMillisecond;

}  // namespace

FilterBasedPlayerWorkerHandler::FilterBasedPlayerWorkerHandler(
    SbMediaVideoCodec video_codec,
    SbMediaAudioCodec audio_codec,
    SbDrmSystem drm_system,
    const SbMediaAudioHeader& audio_header)
    : player_worker_(NULL),
      job_queue_(NULL),
      player_(kSbPlayerInvalid),
      update_media_time_cb_(NULL),
      get_player_state_cb_(NULL),
      update_player_state_cb_(NULL),
      video_codec_(video_codec),
      audio_codec_(audio_codec),
      drm_system_(drm_system),
      audio_header_(audio_header),
      paused_(false) {
#if SB_IS(PLAYER_PUNCHED_OUT)
  bounds_ = PlayerWorker::Bounds();
#endif  // SB_IS(PLAYER_PUNCHED_OUT)
}

bool FilterBasedPlayerWorkerHandler::Init(
    PlayerWorker* player_worker,
    JobQueue* job_queue,
    SbPlayer player,
    UpdateMediaTimeCB update_media_time_cb,
    GetPlayerStateCB get_player_state_cb,
    UpdatePlayerStateCB update_player_state_cb) {
  // This function should only be called once.
  SB_DCHECK(player_worker_ == NULL);

  // All parameters has to be valid.
  SB_DCHECK(player_worker);
  SB_DCHECK(job_queue);
  SB_DCHECK(job_queue->BelongsToCurrentThread());
  SB_DCHECK(SbPlayerIsValid(player));
  SB_DCHECK(update_media_time_cb);
  SB_DCHECK(get_player_state_cb);
  SB_DCHECK(update_player_state_cb);

  player_worker_ = player_worker;
  job_queue_ = job_queue;
  player_ = player;
  update_media_time_cb_ = update_media_time_cb;
  get_player_state_cb_ = get_player_state_cb;
  update_player_state_cb_ = update_player_state_cb;

  scoped_ptr<AudioDecoder> audio_decoder(
      AudioDecoder::Create(audio_codec_, audio_header_));
  scoped_ptr<VideoDecoder> video_decoder(VideoDecoder::Create(video_codec_));

  if (!audio_decoder || !video_decoder) {
    return false;
  }

  audio_renderer_.reset(
      new AudioRenderer(job_queue, audio_decoder.Pass(), audio_header_));
  video_renderer_.reset(new VideoRenderer(video_decoder.Pass()));
  if (audio_renderer_->is_valid() && video_renderer_->is_valid()) {
    update_closure_ = Bind(&FilterBasedPlayerWorkerHandler::Update, this);
    job_queue_->Schedule(update_closure_, kUpdateInterval);
    return true;
  }

  audio_renderer_.reset(NULL);
  video_renderer_.reset(NULL);
  return false;
}

bool FilterBasedPlayerWorkerHandler::Seek(SbMediaTime seek_to_pts, int ticket) {
  SB_DCHECK(job_queue_->BelongsToCurrentThread());

  if (seek_to_pts < 0) {
    SB_DLOG(ERROR) << "Try to seek to negative timestamp " << seek_to_pts;
    seek_to_pts = 0;
  }

  audio_renderer_->Pause();
  audio_renderer_->Seek(seek_to_pts);
  video_renderer_->Seek(seek_to_pts);
  return true;
}

bool FilterBasedPlayerWorkerHandler::WriteSample(InputBuffer input_buffer,
                                                 bool* written) {
  SB_DCHECK(job_queue_->BelongsToCurrentThread());
  SB_DCHECK(written != NULL);

  *written = true;

  if (input_buffer.sample_type() == kSbMediaTypeAudio) {
    if (audio_renderer_->IsEndOfStreamWritten()) {
      SB_LOG(WARNING) << "Try to write audio sample after EOS is reached";
    } else {
      if (!audio_renderer_->CanAcceptMoreData()) {
        *written = false;
        return true;
      }

      if (input_buffer.drm_info()) {
        if (!SbDrmSystemIsValid(drm_system_)) {
          return false;
        }
        if (drm_system_->Decrypt(&input_buffer) == SbDrmSystemPrivate::kRetry) {
          *written = false;
          return true;
        }
      }
      audio_renderer_->WriteSample(input_buffer);
    }
  } else {
    SB_DCHECK(input_buffer.sample_type() == kSbMediaTypeVideo);
    if (video_renderer_->IsEndOfStreamWritten()) {
      SB_LOG(WARNING) << "Try to write video sample after EOS is reached";
    } else {
      if (!video_renderer_->CanAcceptMoreData()) {
        *written = false;
        return true;
      }
      if (input_buffer.drm_info()) {
        if (!SbDrmSystemIsValid(drm_system_)) {
          return false;
        }
        if (drm_system_->Decrypt(&input_buffer) == SbDrmSystemPrivate::kRetry) {
          *written = false;
          return true;
        }
      }
      video_renderer_->WriteSample(input_buffer);
    }
  }

  return true;
}

bool FilterBasedPlayerWorkerHandler::WriteEndOfStream(SbMediaType sample_type) {
  SB_DCHECK(job_queue_->BelongsToCurrentThread());

  if (sample_type == kSbMediaTypeAudio) {
    if (audio_renderer_->IsEndOfStreamWritten()) {
      SB_LOG(WARNING) << "Try to write audio EOS after EOS is enqueued";
    } else {
      SB_LOG(INFO) << "Audio EOS enqueued";
      audio_renderer_->WriteEndOfStream();
    }
  } else {
    if (video_renderer_->IsEndOfStreamWritten()) {
      SB_LOG(WARNING) << "Try to write video EOS after EOS is enqueued";
    } else {
      SB_LOG(INFO) << "Video EOS enqueued";
      video_renderer_->WriteEndOfStream();
    }
  }

  return true;
}

bool FilterBasedPlayerWorkerHandler::SetPause(bool pause) {
  SB_DCHECK(job_queue_->BelongsToCurrentThread());

  paused_ = pause;

  if (pause) {
    audio_renderer_->Pause();
    SB_DLOG(INFO) << "Playback paused.";
  } else {
    audio_renderer_->Play();
    SB_DLOG(INFO) << "Playback started.";
  }

  return true;
}

#if SB_IS(PLAYER_PUNCHED_OUT)
bool FilterBasedPlayerWorkerHandler::SetBounds(
    const PlayerWorker::Bounds& bounds) {
  SB_DCHECK(job_queue_->BelongsToCurrentThread());

  if (SbMemoryCompare(&bounds_, &bounds, sizeof(bounds_)) != 0) {
    bounds_ = bounds;
    // Force an update
    job_queue_->Remove(update_closure_);
    Update();
  }

  return true;
}
#endif  // SB_IS(PLAYER_PUNCHED_OUT)

// TODO: This should be driven by callbacks instead polling.
void FilterBasedPlayerWorkerHandler::Update() {
  SB_DCHECK(job_queue_->BelongsToCurrentThread());

  if ((*player_worker_.*get_player_state_cb_)() == kSbPlayerStatePrerolling) {
    if (!audio_renderer_->IsSeekingInProgress() &&
        !video_renderer_->IsSeekingInProgress()) {
      (*player_worker_.*update_player_state_cb_)(kSbPlayerStatePresenting);
      if (!paused_) {
        audio_renderer_->Play();
      }
    }
  }

  if ((*player_worker_.*get_player_state_cb_)() == kSbPlayerStatePresenting) {
    if (audio_renderer_->IsEndOfStreamPlayed() &&
        video_renderer_->IsEndOfStreamPlayed()) {
      (*player_worker_.*update_player_state_cb_)(kSbPlayerStateEndOfStream);
    }

    scoped_refptr<VideoFrame> frame =
        video_renderer_->GetCurrentFrame(audio_renderer_->GetCurrentTime());
    player_worker_->UpdateDroppedVideoFrames(
        video_renderer_->GetDroppedFrames());

#if SB_IS(PLAYER_PUNCHED_OUT)
    shared::starboard::Application::Get()->HandleFrame(
        player_, frame, bounds_.x, bounds_.y, bounds_.width, bounds_.height);
#endif  // SB_IS(PLAYER_PUNCHED_OUT)

    (*player_worker_.*update_media_time_cb_)(audio_renderer_->GetCurrentTime());
  }

  if (video_renderer_) {
    video_renderer_->Update();
  }

  job_queue_->Schedule(update_closure_, kUpdateInterval);
}

void FilterBasedPlayerWorkerHandler::Stop() {
  job_queue_->Remove(update_closure_);

  audio_renderer_.reset();
  video_renderer_.reset();

#if SB_IS(PLAYER_PUNCHED_OUT)
  // Clear the video frame as we terminate.
  shared::starboard::Application::Get()->HandleFrame(
      player_, VideoFrame::CreateEOSFrame(), 0, 0, 0, 0);
#endif  // SB_IS(PLAYER_PUNCHED_OUT)
}

}  // namespace filter
}  // namespace player
}  // namespace starboard
}  // namespace shared
}  // namespace starboard
