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

#include "cuttlefish/host/commands/virtual_tuner_daemon/pcm_stream_server.h"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "cuttlefish/common/libs/fs/unique_fd.h"

namespace cuttlefish {
namespace virtualtuner {

PcmStreamServer::PcmStreamServer(TunerState* tuner_state,
                                 std::string server_address)
    : tuner_state_(tuner_state),
      server_address_(std::move(server_address)) {}

PcmStreamServer::~PcmStreamServer() {
  Stop();
}

Result<void> PcmStreamServer::Start() {
  if (is_running_) {
    return {};
  }

  std::string addr = server_address_;
  if (addr.rfind("unix:", 0) == 0) {
    std::string path = addr.substr(5);
    ::unlink(path.c_str());
    server_fd_ = SharedFD::SocketLocalServer(path, false, SOCK_STREAM, 0666);
  } else if (addr.rfind("vsock:", 0) == 0) {
    auto last_colon = addr.rfind(':');
    int port = 7011;
    if (last_colon != std::string::npos &&
        absl::SimpleAtoi(addr.substr(last_colon + 1), &port)) {
      server_fd_ = SharedFD::VsockServer(port, SOCK_STREAM, std::nullopt);
    } else {
      server_fd_ = SharedFD::VsockServer(7011, SOCK_STREAM, std::nullopt);
    }
  } else if (addr.rfind("tcp:", 0) == 0 ||
             (addr.find(':') != std::string::npos &&
              addr.find('/') == std::string::npos)) {
    auto last_colon = addr.rfind(':');
    int port = 7011;
    if (last_colon != std::string::npos) {
      (void)absl::SimpleAtoi(addr.substr(last_colon + 1), &port);
    }
    server_fd_ = SharedFD::SocketLocalServer(port, SOCK_STREAM);
  } else {
    ::unlink(addr.c_str());
    server_fd_ = SharedFD::SocketLocalServer(addr, false, SOCK_STREAM, 0666);
  }

  CF_EXPECT(server_fd_->IsOpen(),
            "Failed to open PCM streaming server socket at "
                << server_address_ << ": " << server_fd_->StrError());

  LOG(INFO) << "PCM Streaming Server listening on: " << server_address_;
  is_running_ = true;
  accept_thread_ = std::thread(&PcmStreamServer::AcceptLoop, this);
  return {};
}

void PcmStreamServer::Stop() {
  if (!is_running_) {
    return;
  }
  is_running_ = false;
  if (server_fd_->IsOpen()) {
    server_fd_->Shutdown(SHUT_RDWR);
    server_fd_->Close();
  }
  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& client : active_clients_) {
      if (client->IsOpen()) {
        client->Shutdown(SHUT_RDWR);
        client->Close();
      }
    }
    active_clients_.clear();
  }
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }
  LOG(INFO) << "PCM Streaming Server stopped.";
}

void PcmStreamServer::AcceptLoop() {
  while (is_running_) {
    SharedFD client_fd = UniqueFd::Accept(*server_fd_);
    if (!client_fd->IsOpen()) {
      if (!is_running_) {
        break;
      }
      continue;
    }
    LOG(INFO) << "PCM Streaming Server accepted client connection.";
    {
      std::lock_guard<std::mutex> lock(clients_mutex_);
      active_clients_.push_back(client_fd);
    }
    std::thread(&PcmStreamServer::StreamClient, this, client_fd).detach();
  }
}

void PcmStreamServer::StreamClient(SharedFD client_fd) {
  AudioGenerator generator;
  std::vector<int16_t> buffer(kChunkFrames * kChannels);

  // Pre-buffer 2 chunks (170ms headroom) upon client connection to absorb host scheduling jitter
  static constexpr size_t kPrebufferChunks = 2;
  for (size_t i = 0; i < kPrebufferChunks && is_running_ && client_fd->IsOpen();
       ++i) {
    TunerStateSnapshot snapshot = tuner_state_->GetSnapshot();
    generator.GenerateChunk(buffer.data(), kChunkFrames, snapshot);
    size_t bytes_to_write = buffer.size() * sizeof(int16_t);
    ssize_t written =
        client_fd->Send(buffer.data(), bytes_to_write, MSG_NOSIGNAL);
    if (written < 0 || static_cast<size_t>(written) != bytes_to_write) {
      LOG(INFO) << "PCM client disconnected during prebuffering: "
                << client_fd->StrError();
      break;
    }
  }

  auto next_tick = std::chrono::steady_clock::now();
  static constexpr auto kChunkInterval = std::chrono::microseconds(85333);

  while (is_running_ && client_fd->IsOpen()) {
    next_tick += kChunkInterval;
    std::this_thread::sleep_until(next_tick);
    if (std::chrono::steady_clock::now() >
        next_tick + std::chrono::milliseconds(200)) {
      next_tick = std::chrono::steady_clock::now();
    }

    TunerStateSnapshot snapshot = tuner_state_->GetSnapshot();
    generator.GenerateChunk(buffer.data(), kChunkFrames, snapshot);

    size_t bytes_to_write = buffer.size() * sizeof(int16_t);
    ssize_t written =
        client_fd->Send(buffer.data(), bytes_to_write, MSG_NOSIGNAL);
    if (written < 0 || static_cast<size_t>(written) != bytes_to_write) {
      LOG(INFO) << "PCM client disconnected or write failed: "
                << client_fd->StrError();
      break;
    }
  }

  if (client_fd->IsOpen()) {
    client_fd->Close();
  }
  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    active_clients_.erase(
        std::remove(active_clients_.begin(), active_clients_.end(), client_fd),
        active_clients_.end());
  }
}

}  // namespace virtualtuner
}  // namespace cuttlefish
