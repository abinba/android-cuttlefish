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

#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>
#include <unistd.h>

#include <cmath>
#include <future>
#include <thread>
#include <vector>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.grpc.pb.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.pb.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/audio_generator.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/pcm_stream_server.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/virtual_tuner_service.h"

namespace cuttlefish {
namespace virtualtuner {
namespace {

// ============================================================================
// 1. TunerState Tests
// ============================================================================

TEST(TunerStateTest, InitialStateIsUntuned) {
  TunerState state;
  TunerStateSnapshot snapshot = state.GetSnapshot();

  EXPECT_FALSE(snapshot.is_playing);
  EXPECT_EQ(snapshot.frequency_hz, 0u);
  EXPECT_FALSE(state.IsPlaying());
  EXPECT_EQ(state.GetFrequency(), 0u);
}

TEST(TunerStateTest, SetTuneUpdatesState) {
  TunerState state;
  state.SetTune(RadioBand::FM, 98500000, 1);

  TunerStateSnapshot snapshot = state.GetSnapshot();
  EXPECT_TRUE(snapshot.is_playing);
  EXPECT_EQ(snapshot.band, RadioBand::FM);
  EXPECT_EQ(snapshot.frequency_hz, 98500000u);
  EXPECT_EQ(snapshot.hd_subchannel, 1u);

  EXPECT_TRUE(state.IsPlaying());
  EXPECT_EQ(state.GetBand(), RadioBand::FM);
  EXPECT_EQ(state.GetFrequency(), 98500000u);
  EXPECT_EQ(state.GetSubchannel(), 1u);
}

TEST(TunerStateTest, StopResetsState) {
  TunerState state;
  state.SetTune(RadioBand::AM, 1000000, 0);
  EXPECT_TRUE(state.IsPlaying());

  state.Stop();
  TunerStateSnapshot snapshot = state.GetSnapshot();
  EXPECT_FALSE(snapshot.is_playing);
  EXPECT_EQ(snapshot.frequency_hz, 0u);
  EXPECT_FALSE(state.IsPlaying());
}

TEST(TunerStateTest, ConcurrentAccess) {
  TunerState state;
  std::atomic<bool> run{true};

  std::vector<std::thread> readers;
  for (int i = 0; i < 4; ++i) {
    readers.emplace_back([&]() {
      while (run) {
        auto snap = state.GetSnapshot();
        (void)snap;
      }
    });
  }

  std::vector<std::thread> writers;
  for (int i = 0; i < 2; ++i) {
    writers.emplace_back([&, i]() {
      for (uint32_t freq = 88000000; freq <= 108000000 && run; freq += 200000) {
        state.SetTune(RadioBand::FM, freq, i);
      }
    });
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  run = false;

  for (auto& t : writers) t.join();
  for (auto& t : readers) t.join();
}

// ============================================================================
// 2. AudioGenerator Tests
// ============================================================================

TEST(AudioGeneratorTest, AudioConstantsAreStandard) {
  EXPECT_EQ(kSampleRate, 48000u);
  EXPECT_EQ(kChannels, 2u);
  EXPECT_EQ(kBytesPerSample, 2u);
  EXPECT_EQ(kFrameSizeBytes, 4u);
  EXPECT_EQ(kChunkFrames, 4096u);      // 85.33ms chunk
  EXPECT_EQ(kChunkSizeBytes, 16384u);  // 4096 * 4 = 16384 bytes
}

TEST(AudioGeneratorTest, GenerateChunkSilenceWhenUntuned) {
  AudioGenerator generator;
  std::vector<int16_t> buffer(kChunkFrames * kChannels, 0x55);

  TunerStateSnapshot untuned{RadioBand::FM, 0, 0, false};
  generator.GenerateChunk(buffer.data(), kChunkFrames, untuned);

  for (size_t i = 0; i < kChunkFrames * kChannels; ++i) {
    EXPECT_EQ(buffer[i], 0);
  }
}

TEST(AudioGeneratorTest, GenerateChunkWhiteNoiseWhenPlaying) {
  AudioGenerator generator;
  std::vector<int16_t> buffer(kChunkFrames * kChannels, 0);

  TunerStateSnapshot tuned{RadioBand::FM, 88500000, 0, true};
  generator.GenerateChunk(buffer.data(), kChunkFrames, tuned);

  bool has_non_zero = false;
  for (size_t i = 0; i < kChunkFrames; ++i) {
    int16_t left = buffer[i * 2];
    int16_t right = buffer[i * 2 + 1];

    EXPECT_EQ(left, right);  // Stereo matches
    EXPECT_GE(left, -4000);
    EXPECT_LE(left, 4000);
    if (left != 0) has_non_zero = true;
  }
  EXPECT_TRUE(has_non_zero);
}

// ============================================================================
// 3. VirtualTunerServiceImpl (gRPC) Tests
// ============================================================================

TEST(VirtualTunerServiceTest, GrpcTuneAndStop) {
  TunerState state;
  VirtualTunerServiceImpl service(&state);

  std::string server_address("127.0.0.1:50077");
  ::grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, ::grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
  ASSERT_NE(server, nullptr);

  auto channel = ::grpc::CreateChannel(server_address,
                                       ::grpc::InsecureChannelCredentials());
  auto stub = VirtualTuner::NewStub(channel);

  // 1. Tune RPC
  {
    ::grpc::ClientContext context;
    TuneRequest request;
    request.set_band(RadioBand::FM);
    request.set_frequency_hz(101100000);
    request.set_hd_subchannel(0);

    TuneResponse response;
    auto status = stub->Tune(&context, request, &response);
    EXPECT_TRUE(status.ok());
    EXPECT_TRUE(response.success());
    EXPECT_EQ(state.GetFrequency(), 101100000u);
    EXPECT_TRUE(state.IsPlaying());
  }

  // 2. Stop RPC
  {
    ::grpc::ClientContext context;
    StopRequest request;
    StopResponse response;
    auto status = stub->Stop(&context, request, &response);
    EXPECT_TRUE(status.ok());
    EXPECT_TRUE(response.success());
    EXPECT_FALSE(state.IsPlaying());
    EXPECT_EQ(state.GetFrequency(), 0u);
  }

  server->Shutdown();
}

// ============================================================================
// 4. PcmStreamServer Tests
// ============================================================================

TEST(PcmStreamServerTest, StartStreamAndReadFrames) {
  TunerState state;
  state.SetTune(RadioBand::FM, 88500000, 0);

  std::string sock_path = "/tmp/test_cf_pcm_stream_" + std::to_string(getpid()) + ".sock";
  std::string server_addr = "unix:" + sock_path;

  PcmStreamServer server(&state, server_addr);
  auto start_res = server.Start();
  ASSERT_TRUE(start_res.has_value());
  EXPECT_TRUE(server.IsRunning());

  // Connect client socket
  SharedFD client_fd = SharedFD::SocketLocalClient(sock_path, false, SOCK_STREAM);
  ASSERT_TRUE(client_fd->IsOpen()) << "Failed to connect: " << client_fd->StrError();

  // Read 2 chunks of 10ms PCM audio frames (1920 bytes each)
  std::vector<uint8_t> read_buffer(kChunkSizeBytes);

  for (int chunk = 0; chunk < 2; ++chunk) {
    auto read_res = client_fd->Read(read_buffer.data(), kChunkSizeBytes);
    ASSERT_TRUE(read_res.has_value());
    EXPECT_EQ(*read_res, kChunkSizeBytes);

    // Verify non-zero data
    bool non_zero = false;
    for (uint8_t b : read_buffer) {
      if (b != 0) non_zero = true;
    }
    EXPECT_TRUE(non_zero);
  }

  // Stop server
  client_fd->Close();
  server.Stop();
  EXPECT_FALSE(server.IsRunning());
  ::unlink(sock_path.c_str());
}

}  // namespace
}  // namespace virtualtuner
}  // namespace cuttlefish
