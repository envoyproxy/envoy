#include <memory>
#include <string>
#include <thread>

#include "source/extensions/transport_sockets/alts/alts_channel_pool.h"

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

class AltsChannelPoolTest : public Test {
protected:
  void StartFakeHandshakerService() {
    server_address_ = absl::StrCat("[::1]:", 0);
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
      server_address_ = absl::StrCat("[::1]:", listening_port);
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

  std::string ServerAddress() { return server_address_; }

private:
  std::string server_address_;
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<std::thread> server_thread_;
};

TEST_F(AltsChannelPoolTest, SuccessWithDefaultChannels) {
  StartFakeHandshakerService();

  // Create a channel pool and check that it has the correct dimensions.
  auto channel_pool = AltsChannelPool::Create(ServerAddress());
  EXPECT_THAT(channel_pool, NotNull());
  EXPECT_THAT(channel_pool->GetChannel(), NotNull());
  EXPECT_EQ(channel_pool->GetChannelPoolSize(), 10);

  // Check that we can write to and read from the channel multiple times.
  for (int i = 0; i < 10; ++i) {
    auto channel = channel_pool->GetChannel();
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
