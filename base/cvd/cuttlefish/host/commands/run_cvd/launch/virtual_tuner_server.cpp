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

#include <linux/vm_sockets.h>

#include <optional>
#include <string>

#include "fmt/format.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/commands/assemble_cvd/proto/guest_config.pb.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

// vsock port that the guest Broadcast Radio HAL dials for Tune/Stop.
constexpr unsigned int kVirtualTunerVsockPort = 7010;

}  // namespace

bool HasVirtualTunerStream(const CuttlefishConfig::InstanceSpecific& instance) {
  if (!instance.enable_audio()) {
    return false;
  }
  const std::optional<config::Audio> audio = instance.audio_settings();
  return audio.has_value() && !audio->pcm_devices().empty() &&
         audio->pcm_devices(0).has_virtual_tuner();
}

std::string VirtualTunerPcmSocketPath(
    const CuttlefishConfig::InstanceSpecific& instance) {
  return instance.PerInstanceInternalUdsPath("virtual_tuner_pcm.sock");
}

Result<std::optional<MonitorCommand>> VirtualTunerServer(
    const CuttlefishConfig::InstanceSpecific& instance) {
  if (!HasVirtualTunerStream(instance)) {
    return std::nullopt;
  }

  // run_cvd owns the listening socket so that it exists before webRTC starts:
  // TunerAudioSource connects once, at construction, and never retries.
  SharedFD pcm_server = SharedFD::SocketLocalServer(
      VirtualTunerPcmSocketPath(instance), false, SOCK_STREAM, 0600);
  CF_EXPECT(pcm_server->IsOpen(), pcm_server->StrError());

  std::string server_address;
  if (instance.vhost_user_vsock()) {
    server_address = fmt::format(
        "unix://{}", Fd::GetVhostUserVsockServerAddr(
                         kVirtualTunerVsockPort, instance.vsock_guest_cid()));
  } else {
    server_address =
        fmt::format("vsock:{}:{}", VMADDR_CID_HOST, kVirtualTunerVsockPort);
  }

  return Command(VirtualTunerDaemonBinary())
      .AddParameter("--server_address=", server_address)
      .AddParameter("--pcm_server_fd=", pcm_server);
}

}  // namespace cuttlefish
