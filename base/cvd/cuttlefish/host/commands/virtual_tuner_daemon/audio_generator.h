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

#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <span>

#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"

namespace cuttlefish {
namespace virtualtuner {

inline constexpr size_t kSampleRate = 48000;
inline constexpr size_t kChannels = 2;
inline constexpr size_t kBytesPerSample = sizeof(int16_t);
inline constexpr size_t kFrameSizeBytes = kChannels * kBytesPerSample;

inline constexpr size_t kChunkFrames = 4096;
inline constexpr size_t kChunkSizeBytes = kChunkFrames * kFrameSizeBytes;

// Tone that identifies the tuned station, so that stations can be told apart
// by ear and by FFT. FM: 300 Hz at 87.5 MHz plus 10 Hz per 100 kHz. AM: 300 Hz
// at 530 kHz plus 0.5 Hz per kHz. Each HD subchannel above HD1 adds 1 kHz
// (HD1 simulcasts the analog program). Returns 0 when there is no AM/FM
// frequency, in which case only noise is played.
double StationToneHz(const TunerStateSnapshot& snapshot);

class AudioGenerator {
 public:
  AudioGenerator();

  void GenerateChunk(std::span<int16_t> samples,
                     const TunerStateSnapshot& snapshot);

 private:
  std::minstd_rand rng_;
  // Kept across chunks so the tone stays continuous.
  double phase_ = 0.0;
};

}  // namespace virtualtuner
}  // namespace cuttlefish
