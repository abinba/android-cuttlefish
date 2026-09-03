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

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/audio_source.h"

namespace cuttlefish {

class TunerAudioSource : public webrtc_streaming::AudioSource {
 public:
  TunerAudioSource(
      std::shared_ptr<webrtc_streaming::AudioSource> fallback_source,
      std::string pcm_socket_path)
      : fallback_source_(std::move(fallback_source)),
        pcm_socket_path_(std::move(pcm_socket_path)),
        fd_(-1) {
    fifo_buffer_.reserve(48000 * 2 * sizeof(int16_t));
  }

  ~TunerAudioSource() override {
    CloseSocket();
  }

  int GetMoreAudioData(void* data, int bytes_per_sample,
                       int samples_per_channel, int num_channels,
                       int sample_rate, bool& muted) override {
    EnsureConnected();
    const size_t bytes_needed =
        static_cast<size_t>(samples_per_channel) * num_channels * bytes_per_sample;

    DrainSocket();

    // Ensure we have buffered at least 1 full guest period (16,384 bytes = 4096 frames)
    // before streaming to prevent initial starving.
    static constexpr size_t kMinPrebufferBytes = 16384;
    if (!is_prebuffered_) {
      if (fifo_buffer_.size() >= kMinPrebufferBytes) {
        is_prebuffered_ = true;
      } else {
        std::memset(data, 0, bytes_needed);
        muted = false;
        return samples_per_channel;
      }
    }

    if (fifo_buffer_.size() >= bytes_needed) {
      std::memcpy(data, fifo_buffer_.data(), bytes_needed);
      fifo_buffer_.erase(fifo_buffer_.begin(), fifo_buffer_.begin() + bytes_needed);
      muted = false;
      return samples_per_channel;
    }

    if (fallback_source_) {
      return fallback_source_->GetMoreAudioData(
          data, bytes_per_sample, samples_per_channel, num_channels,
          sample_rate, muted);
    }

    std::memset(data, 0, bytes_needed);
    muted = false;
    return samples_per_channel;
  }

 private:
  void CloseSocket() {
    if (fd_ >= 0) {
      close(fd_);
      fd_ = -1;
    }
    fifo_buffer_.clear();
    is_prebuffered_ = false;
  }

  void DrainSocket() {
    if (fd_ < 0) {
      return;
    }
    uint8_t temp[4096];
    while (true) {
      ssize_t n = read(fd_, temp, sizeof(temp));
      if (n > 0) {
        fifo_buffer_.insert(fifo_buffer_.end(), temp, temp + n);
        static constexpr size_t kMaxFifoBytes = 48000 * 2 * sizeof(int16_t) * 500 / 1000;
        if (fifo_buffer_.size() > kMaxFifoBytes) {
          fifo_buffer_.erase(
              fifo_buffer_.begin(),
              fifo_buffer_.begin() + (fifo_buffer_.size() - kMaxFifoBytes));
        }
      } else if (n == 0 ||
                 (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        CloseSocket();
        break;
      } else {
        break;
      }
    }
  }

  void EnsureConnected() {
    if (fd_ >= 0) {
      return;
    }
    int s = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (s < 0) {
      return;
    }
    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, pcm_socket_path_.c_str(),
                 sizeof(addr.sun_path) - 1);
    if (connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0 ||
        errno == EINPROGRESS) {
      fd_ = s;
      LOG(INFO) << "TunerAudioSource connected to " << pcm_socket_path_;
    } else {
      close(s);
    }
  }

  std::shared_ptr<webrtc_streaming::AudioSource> fallback_source_;
  std::string pcm_socket_path_;
  int fd_;
  std::vector<uint8_t> fifo_buffer_;
  bool is_prebuffered_ = false;
};

}  // namespace cuttlefish
