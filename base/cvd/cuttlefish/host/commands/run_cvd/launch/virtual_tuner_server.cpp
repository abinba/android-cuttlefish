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

#include "cuttlefish/host/commands/run_cvd/launch/virtual_tuner_server.h"

#include <unistd.h>
#include <string>

#include "absl/log/log.h"
#include "fmt/core.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/known_paths.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<MonitorCommand> VirtualTunerServer(
    const CuttlefishConfig::InstanceSpecific& instance) {
  LOG(INFO) << "VirtualTunerServer launcher function called";
  std::string server_address =
      fmt::format("unix:{}/vsock_{}_{}/vm.vsock_7010", TempDir(),
                  instance.vsock_guest_cid(), getuid());
  std::string pcm_server_address =
      fmt::format("unix:{}/vsock_{}_{}/vm.vsock_7011", TempDir(),
                  instance.vsock_guest_cid(), getuid());

  return Command(VirtualTunerDaemonBinary())
      .AddParameter("--server_address=", server_address)
      .AddParameter("--pcm_server_address=", pcm_server_address);
}

}  // namespace cuttlefish
