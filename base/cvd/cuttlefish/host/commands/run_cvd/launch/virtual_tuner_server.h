#pragma once

#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<MonitorCommand> VirtualTunerServer(
    const CuttlefishConfig::InstanceSpecific& instance);

}  // namespace cuttlefish
