#include <memory>
#include <string>
#include <thread>

#include "envoy/network/address.h"

#include "source/extensions/transport_sockets/alts/alts_channel_pool.h"

#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "grpcpp/client_context.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/sync_stream.h"
#include "gtest/gtest.h"
#include "src/proto/grpc/gcp/handshaker.grpc.pb.h"
#include "src/proto/grpc/gcp/handshaker.pb.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {
namespace {

using ::grpc::gcp::HandshakerReq;
using ::grpc::gcp::HandshakerResp;
using ::grpc::gcp::HandshakerService;
using ::testing::NotNull;
using ::testing::Test;

class FakeHandshakerService final : public HandshakerService::Service {
public:
  FakeHandshakerService() = default;

  grpc::Status
  DoHandshake(grpc::ServerContext*,
              grpc::ServerReaderWriter<HandshakerResp, HandshakerReq>* stream) override {
    HandshakerReq request;
    while (stream->Read(&request)) {
      HandshakerResp response;
      EXPECT_TRUE(stream->Write(response));
    }
    return grpc::Status::OK;
  }
};

class AltsChannelPoolTest : public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  AltsChannelPoolTest() : version_(GetParam()){};
  void startFakeHandshakerService() {
    server_address_ = absl::StrCat(Network::Test::getLoopbackAddressUrlString(version_), ":0");
    testing::internal::Notification notification;
    server_thread_ = std::make_unique<std::thread>([this, &notification]() {
      FakeHandshakerService fake_handshaker_service;
      grpc::ServerBuilder server_builder;
      int listening_port = -1;
      server_builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials(),
                                      &listening_port);
      server_builder.RegisterService(&fake_handshaker_service);
      server_ = server_builder.BuildAndStart();
      EXPECT_THAT(server_, NotNull());
      EXPECT_NE(listening_port, -1);
      server_address_ =
          absl::StrCat(Network::Test::getLoopbackAddressUrlString(version_), ":", listening_port);
      (&notification)->Notify();
      server_->Wait();
    });
    notification.WaitForNotification();
  }

  void TearDown() override {
    if (server_thread_) {
      server_->Shutdown();
      server_thread_->join();
    }
  }

  std::string serverAddress() { return server_address_; }

private:
  std::string server_address_;
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<std::thread> server_thread_;
  Network::Address::IpVersion version_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AltsChannelPoolTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AltsChannelPoolTest, SuccessWithDefaultChannels) {
  startFakeHandshakerService();

  // Create a channel pool and check that it has the correct dimensions.
  auto channel_pool = AltsChannelPool::create(serverAddress());
  EXPECT_THAT(channel_pool, NotNull());
  EXPECT_THAT(channel_pool->getChannel(), NotNull());
  EXPECT_EQ(channel_pool->getChannelPoolSize(), 10);

  // Check that we can write to and read from the channel multiple times.
  for (int i = 0; i < 10; ++i) {
    auto channel = channel_pool->getChannel();
    EXPECT_THAT(channel, NotNull());
    grpc::ClientContext client_context;
    auto stub = HandshakerService::NewStub(channel);
    auto stream = stub->DoHandshake(&client_context);
    HandshakerReq request;
    EXPECT_TRUE(stream->Write(request));
    HandshakerResp response;
    EXPECT_TRUE(stream->Read(&response));
  }
}

} // namespace
} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
