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

#include "starboard/player.h"

#include "starboard/android/shared/jni_env_ext.h"
#include "starboard/configuration.h"
#include "starboard/decode_target.h"
#include "starboard/log.h"
#include "starboard/shared/starboard/player/filter/filter_based_player_worker_handler.h"
#include "starboard/shared/starboard/player/player_internal.h"
#include "starboard/shared/starboard/player/player_worker.h"

using starboard::shared::starboard::player::filter::
    FilterBasedPlayerWorkerHandler;
using starboard::shared::starboard::player::PlayerWorker;
using starboard::android::shared::JniEnvExt;

SbPlayer SbPlayerCreate(SbWindow window,
                        SbMediaVideoCodec video_codec,
                        SbMediaAudioCodec audio_codec,
                        SbMediaTime duration_pts,
                        SbDrmSystem drm_system,
                        const SbMediaAudioHeader* audio_header,
                        SbPlayerDeallocateSampleFunc sample_deallocate_func,
                        SbPlayerDecoderStatusFunc decoder_status_func,
                        SbPlayerStatusFunc player_status_func,
                        void* context,
                        SbPlayerOutputMode output_mode,
                        SbDecodeTargetProvider* provider) {
  SB_UNREFERENCED_PARAMETER(window);
  SB_UNREFERENCED_PARAMETER(provider);

  if (audio_codec != kSbMediaAudioCodecAac) {
    SB_LOG(ERROR) << "Unsupported audio codec " << audio_codec;
    return kSbPlayerInvalid;
  }

  if (!audio_header) {
    SB_LOG(ERROR) << "SbPlayerCreate() requires a non-NULL SbMediaAudioHeader";
    return kSbPlayerInvalid;
  }

  if (video_codec != kSbMediaVideoCodecH264 &&
      video_codec != kSbMediaVideoCodecVp9) {
    SB_LOG(ERROR) << "Unsupported video codec " << video_codec;
    return kSbPlayerInvalid;
  }

  if (!SbPlayerOutputModeSupported(output_mode, video_codec, drm_system)) {
    SB_LOG(ERROR) << "Unsupported player output mode " << output_mode;
    return kSbPlayerInvalid;
  }

  JniEnvExt* env = JniEnvExt::Get();
  env->CallActivityVoidMethodOrAbort("onMediaStart", "()V");

  starboard::scoped_ptr<PlayerWorker::Handler> handler(
      new FilterBasedPlayerWorkerHandler(video_codec, audio_codec, drm_system,
                                         *audio_header, output_mode, provider));
  return new SbPlayerPrivate(duration_pts, sample_deallocate_func,
                             decoder_status_func, player_status_func, context,
                             handler.Pass());
}
