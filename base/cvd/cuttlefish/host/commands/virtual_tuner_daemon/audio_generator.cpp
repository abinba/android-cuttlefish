/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/host/commands/virtual_tuner_daemon/audio_generator.h"

#include <cstdlib>
#include <cstring>

namespace cuttlefish {
namespace virtualtuner {

void AudioGenerator::GenerateChunk(int16_t* buffer, size_t frame_count,
                                   const TunerStateSnapshot& snapshot) {
  if (!snapshot.is_playing) {
    std::memset(buffer, 0, frame_count * kFrameSizeBytes);
    return;
  }

  // Generate white noise when tuned and playing
  for (size_t i = 0; i < frame_count; ++i) {
    int16_t sample = static_cast<int16_t>((std::rand() % 8000) - 4000);
    buffer[i * 2] = sample;      // Left
    buffer[i * 2 + 1] = sample;  // Right
  }
}

void AudioGenerator::Reset() {
  // No-op for white noise
}

}  // namespace virtualtuner
}  // namespace cuttlefish
