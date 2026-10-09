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

#include <optional>
#include <string>

#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Whether the guest audio config declares a virtual tuner capture stream.
bool HasVirtualTunerStream(const CuttlefishConfig::InstanceSpecific& instance);

// Host-local socket that carries the tuner PCM from virtual_tuner_daemon to
// the webRTC streamer. Both ends are host processes, so this lives in the
// instance's internal directory, not in the vsock directory.
std::string VirtualTunerPcmSocketPath(
    const CuttlefishConfig::InstanceSpecific& instance);

// Launches virtual_tuner_daemon if the guest has a virtual tuner stream.
Result<std::optional<MonitorCommand>> VirtualTunerServer(
    const CuttlefishConfig::InstanceSpecific& instance);

}  // namespace cuttlefish
