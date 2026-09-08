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

#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include <grpcpp/grpcpp.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/pcm_stream_server.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/virtual_tuner_service.h"
#include "cuttlefish/host/libs/config/logging.h"

namespace cuttlefish {
namespace virtualtuner {

int VirtualTunerDaemonMain(int argc, char** argv) {
  DefaultSubprocessLogging(argv);

  std::vector<Flag> flags;
  std::string server_address;
  std::string pcm_server_address;

  flags.emplace_back(GflagsCompatFlag("server_address", server_address)
                         .Help("gRPC listening server address (e.g. "
                               "unix:/tmp/vsock_3_1000/vm.vsock_7010)"));
  flags.emplace_back(GflagsCompatFlag("pcm_server_address", pcm_server_address)
                         .Help("PCM streaming server address (e.g. "
                               "unix:/tmp/vsock_3_1000/vm.vsock_7011)"));

  std::vector<std::string> args(argv + 1, argv + argc);
  auto parse_res = ConsumeFlags(flags, args, {.fail_on_unexpected_argument = false});
  if (!parse_res.has_value()) {
    LOG(FATAL) << "Could not process command line flags: "
               << parse_res.error().FormatForEnv();
  }

  if (server_address.empty()) {
    LOG(FATAL) << "Did not receive a --server_address";
  }

  if (pcm_server_address.empty()) {
    // If pcm_server_address not explicitly passed, derive from server_address (7010 -> 7011)
    if (server_address.find("7010") != std::string::npos) {
      pcm_server_address = server_address;
      size_t pos = pcm_server_address.find("7010");
      pcm_server_address.replace(pos, 4, "7011");
    } else {
      pcm_server_address = server_address + "_pcm";
    }
  }

  LOG(INFO) << "Virtual Tuner gRPC Control Server starting on address: "
            << server_address;
  LOG(INFO) << "Virtual Tuner PCM Stream Server starting on address: "
            << pcm_server_address;

  TunerState tuner_state;

  // Start the PCM audio stream server
  PcmStreamServer pcm_server(&tuner_state, pcm_server_address);
  auto pcm_res = pcm_server.Start();
  if (!pcm_res.has_value()) {
    LOG(FATAL) << "Failed to start PCM streaming server: "
               << pcm_res.error().FormatForEnv();
  }

  // Start the gRPC control server
  VirtualTunerServiceImpl service(&tuner_state);
  ::grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ::grpc::ServerBuilder builder;

  builder.RegisterService(&service);
  builder.AddListeningPort(server_address, ::grpc::InsecureServerCredentials());

  std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
  if (!server) {
    LOG(FATAL) << "Failed to start gRPC Server on " << server_address;
  }

  LOG(INFO) << "Virtual Tuner Daemon is fully initialized and running.";
  server->Wait();

  pcm_server.Stop();
  return 0;
}

}  // namespace virtualtuner
}  // namespace cuttlefish

int main(int argc, char** argv) {
  ::signal(SIGPIPE, SIG_IGN);
  return cuttlefish::virtualtuner::VirtualTunerDaemonMain(argc, argv);
}
