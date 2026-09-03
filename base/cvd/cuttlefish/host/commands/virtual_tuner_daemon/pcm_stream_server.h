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

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/audio_generator.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace virtualtuner {

class PcmStreamServer {
 public:
  PcmStreamServer(TunerState* tuner_state, std::string server_address);
  ~PcmStreamServer();

  Result<void> Start();
  void Stop();
  bool IsRunning() const { return is_running_; }
  std::string ServerAddress() const { return server_address_; }

 private:
  void AcceptLoop();
  void StreamClient(SharedFD client_fd);

  TunerState* tuner_state_;
  std::string server_address_;
  std::atomic<bool> is_running_{false};
  SharedFD server_fd_;
  std::thread accept_thread_;
  std::mutex clients_mutex_;
  std::vector<SharedFD> active_clients_;
};

}  // namespace virtualtuner
}  // namespace cuttlefish
