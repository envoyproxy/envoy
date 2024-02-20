#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "envoy/network/address.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/transport_sockets/alts/alts_proxy.h"
#include "source/extensions/transport_sockets/alts/alts_tsi_handshaker.h"
#include "source/extensions/transport_sockets/alts/tsi_handshaker.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "grpcpp/channel.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/sync_stream.h"
#include "gtest/gtest.h"
#include "src/proto/grpc/gcp/handshaker.grpc.pb.h"
#include "src/proto/grpc/gcp/handshaker.pb.h"
#include "src/proto/grpc/gcp/transport_security_common.pb.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {
namespace {

using ::Envoy::StatusHelpers::StatusCodeIs;
using ::grpc::gcp::HandshakerReq;
using ::grpc::gcp::HandshakerResp;
using ::grpc::gcp::HandshakerResult;
using ::grpc::gcp::HandshakerService;
using ::testing::IsNull;
using ::testing::NotNull;

constexpr absl::string_view ClientInit = "CLIENT_INIT";
constexpr absl::string_view ServerInit = "SERVER_INIT";
constexpr absl::string_view ClientFinished = "CLIENT_FINISHED";
constexpr absl::string_view ServerFinished = "SERVER_FINISHED";

constexpr absl::string_view KeyData = "fake_key_data_needs_to_be_at_least_44_characters_long";
constexpr absl::string_view LocalServiceAccount = "local_service_account";
constexpr absl::string_view PeerServiceAccount = "peer_service_account";

void populateHandshakeResult(HandshakerResult* result) {
  result->mutable_peer_identity()->set_service_account(PeerServiceAccount);
  result->mutable_peer_rpc_versions();
  result->mutable_local_identity()->set_service_account(LocalServiceAccount);
  result->set_application_protocol(ApplicationProtocol);
  result->set_record_protocol(RecordProtocol);
  result->set_key_data(KeyData);
}

class CapturingTsiHandshakerCallbacks final : public TsiHandshakerCallbacks {
public:
  CapturingTsiHandshakerCallbacks() = default;
  ~CapturingTsiHandshakerCallbacks() override = default;

  void onNextDone(NextResultPtr&& result) override { next_result_ = std::move(result); }

  NextResultPtr getNextResult() { return std::move(next_result_); }

private:
  NextResultPtr next_result_;
};

class FakeHandshakerService final : public HandshakerService::Service {
public:
  FakeHandshakerService() = default;

  grpc::Status
  DoHandshake(grpc::ServerContext* context,
              grpc::ServerReaderWriter<HandshakerResp, HandshakerReq>* stream) override {
    EXPECT_THAT(context, NotNull());
    HandshakerReq request;
    bool is_assisting_client = false;
    while (stream->Read(&request)) {
      HandshakerResp response;
      if (request.has_client_start()) {
        // The request contains a StartClientHandshakeReq message.
        is_assisting_client = true;
        response.set_out_frames(ClientInit);
        response.set_bytes_consumed(ClientInit.size());
      } else if (request.has_server_start()) {
        // The request contains a StartServerHandshakeReq message.
        EXPECT_EQ(request.server_start().in_bytes(), ClientInit);
        std::string out_frames = absl::StrCat(ServerInit, ServerFinished);
        response.set_out_frames(out_frames);
        response.set_bytes_consumed(out_frames.size());
      } else if (request.has_next()) {
        // The request contains a NextHandshakeMessageReq message.
        std::string expected_in_bytes = is_assisting_client
                                            ? absl::StrCat(ServerInit, ServerFinished)
                                            : std::string(ClientFinished);
        EXPECT_EQ(request.next().in_bytes(), expected_in_bytes);
        response.set_bytes_consumed(expected_in_bytes.size());
        if (is_assisting_client) {
          response.set_out_frames(ClientFinished);
        }
        populateHandshakeResult(response.mutable_result());
      } else {
        response.mutable_status()->set_code(
            static_cast<int>(grpc::StatusCode::FAILED_PRECONDITION));
        response.mutable_status()->set_details("Missing body of handshake request.");
      }
      EXPECT_TRUE(stream->Write(response));
    }
    return grpc::Status::OK;
  }
};

class ErrorHandshakerService final : public HandshakerService::Service {
public:
  ErrorHandshakerService() = default;

  grpc::Status
  DoHandshake(grpc::ServerContext* context,
              grpc::ServerReaderWriter<HandshakerResp, HandshakerReq>* stream) override {
    EXPECT_THAT(context, NotNull());
    HandshakerReq request;
    while (stream->Read(&request)) {
      HandshakerResp response;
      response.mutable_status()->set_code(static_cast<int>(grpc::StatusCode::INTERNAL));
      response.mutable_status()->set_details("Internal error.");
      EXPECT_TRUE(stream->Write(response));
    }
    return {grpc::StatusCode::INTERNAL, "DoHandshake internal error."};
  }
};

class AltsTsiHandshakerTest : public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  AltsTsiHandshakerTest() : version_(GetParam()){};
  void startFakeHandshakerService() {
    server_address_ = absl::StrCat(Network::Test::getLoopbackAddressUrlString(version_), ":0");
    absl::Notification notification;
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
      notification.Notify();
      server_->Wait();
    });
    notification.WaitForNotification();
  }

  void startErrorHandshakerService() {
    server_address_ = absl::StrCat(Network::Test::getLoopbackAddressUrlString(version_), ":0");
    absl::Notification notification;
    server_thread_ = std::make_unique<std::thread>([this, &notification]() {
      ErrorHandshakerService error_handshaker_service;
      grpc::ServerBuilder server_builder;
      int listening_port = -1;
      server_builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials(),
                                      &listening_port);
      server_builder.RegisterService(&error_handshaker_service);
      server_ = server_builder.BuildAndStart();
      EXPECT_THAT(server_, NotNull());
      EXPECT_NE(listening_port, -1);
      server_address_ =
          absl::StrCat(Network::Test::getLoopbackAddressUrlString(version_), ":", listening_port);
      notification.Notify();
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

  std::shared_ptr<grpc::Channel> getChannel() {
    return grpc::CreateChannel(server_address_,
                               grpc::InsecureChannelCredentials()); // NOLINT
  }

private:
  std::string server_address_;
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<std::thread> server_thread_;
  Network::Address::IpVersion version_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AltsTsiHandshakerTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AltsTsiHandshakerTest, ClientSideFullHandshake) {
  // Setup.
  startFakeHandshakerService();
  auto handshaker = AltsTsiHandshaker::createForClient(getChannel());
  Event::MockDispatcher dispatcher;
  auto tsi_handshaker = std::make_unique<TsiHandshaker>(std::move(handshaker), dispatcher);

  // Get the ClientInit.
  {
    CapturingTsiHandshakerCallbacks capturing_callbacks;
    tsi_handshaker->setHandshakerCallbacks(capturing_callbacks);
    EXPECT_CALL(dispatcher, post(_)).WillOnce([](Envoy::Event::PostCb cb) { cb(); });
    Buffer::OwnedImpl received_bytes;
    EXPECT_OK(tsi_handshaker->next(received_bytes));

    auto result = capturing_callbacks.getNextResult();
    EXPECT_THAT(result, NotNull());
    EXPECT_TRUE(result->status_.ok());
    EXPECT_EQ(result->to_send_->toString(), ClientInit);
    EXPECT_THAT(result->result_, IsNull());
  }

  // Get the ClientFinished and the handshake result.
  {
    CapturingTsiHandshakerCallbacks capturing_callbacks;
    tsi_handshaker->setHandshakerCallbacks(capturing_callbacks);
    EXPECT_CALL(dispatcher, post(_)).WillOnce([](Envoy::Event::PostCb cb) { cb(); });
    Buffer::OwnedImpl received_bytes;
    received_bytes.add(ServerInit.data(), ServerInit.size());
    received_bytes.add(ServerFinished.data(), ServerFinished.size());
    EXPECT_TRUE(tsi_handshaker->next(received_bytes).ok());

    auto result = capturing_callbacks.getNextResult();
    EXPECT_THAT(result, NotNull());
    EXPECT_TRUE(result->status_.ok());
    EXPECT_EQ(result->to_send_->toString(), ClientFinished);
    EXPECT_THAT(result->result_, NotNull());
    EXPECT_THAT(result->result_->frame_protector, NotNull());
    EXPECT_EQ(result->result_->peer_identity, PeerServiceAccount);
    EXPECT_EQ(result->result_->unused_bytes.size(), 0);
  }
}

TEST_P(AltsTsiHandshakerTest, ServerSideFullHandshake) {
  // Setup.
  startFakeHandshakerService();
  auto handshaker = AltsTsiHandshaker::createForServer(getChannel());
  Event::MockDispatcher dispatcher;
  auto tsi_handshaker = std::make_unique<TsiHandshaker>(std::move(handshaker), dispatcher);

  // Get the ServerInit and ServerFinished.
  {
    CapturingTsiHandshakerCallbacks capturing_callbacks;
    tsi_handshaker->setHandshakerCallbacks(capturing_callbacks);
    EXPECT_CALL(dispatcher, post(_)).WillOnce([](Envoy::Event::PostCb cb) { cb(); });
    Buffer::OwnedImpl received_bytes;
    received_bytes.add(ClientInit.data(), ClientInit.size());
    EXPECT_OK(tsi_handshaker->next(received_bytes));

    auto result = capturing_callbacks.getNextResult();
    EXPECT_THAT(result, NotNull());
    EXPECT_TRUE(result->status_.ok());
    EXPECT_EQ(result->to_send_->toString(), absl::StrCat(ServerInit, ServerFinished));
    EXPECT_THAT(result->result_, IsNull());
  }

  // Get the handshake result.
  {
    CapturingTsiHandshakerCallbacks capturing_callbacks;
    tsi_handshaker->setHandshakerCallbacks(capturing_callbacks);
    EXPECT_CALL(dispatcher, post(_)).WillOnce([](Envoy::Event::PostCb cb) { cb(); });
    Buffer::OwnedImpl received_bytes;
    received_bytes.add(ClientFinished.data(), ClientFinished.size());
    EXPECT_TRUE(tsi_handshaker->next(received_bytes).ok());

    auto result = capturing_callbacks.getNextResult();
    EXPECT_THAT(result, NotNull());
    EXPECT_TRUE(result->status_.ok());
    EXPECT_EQ(result->to_send_->toString(), "");
    EXPECT_THAT(result->result_, NotNull());
    EXPECT_THAT(result->result_->frame_protector, NotNull());
    EXPECT_EQ(result->result_->peer_identity, PeerServiceAccount);
    EXPECT_EQ(result->result_->unused_bytes.size(), 0);
  }
}

TEST_P(AltsTsiHandshakerTest, ClientSideError) {
  // Setup.
  startErrorHandshakerService();
  auto handshaker = AltsTsiHandshaker::createForClient(getChannel());
  Event::MockDispatcher dispatcher;
  auto tsi_handshaker = std::make_unique<TsiHandshaker>(std::move(handshaker), dispatcher);

  // Try to get the ClientInit and observe failure.
  {
    CapturingTsiHandshakerCallbacks capturing_callbacks;
    tsi_handshaker->setHandshakerCallbacks(capturing_callbacks);
    EXPECT_CALL(dispatcher, post(_)).WillOnce([](Envoy::Event::PostCb cb) { cb(); });
    Buffer::OwnedImpl received_bytes;
    ASSERT_THAT(tsi_handshaker->next(received_bytes), StatusCodeIs(absl::StatusCode::kInternal));

    auto result = capturing_callbacks.getNextResult();
    EXPECT_THAT(result, NotNull());
    EXPECT_THAT(result->status_, StatusCodeIs(absl::StatusCode::kInternal));
    EXPECT_EQ(result->to_send_->toString(), "");
    EXPECT_THAT(result->result_, IsNull());
  }
}

TEST_P(AltsTsiHandshakerTest, ServerSideError) {
  // Setup.
  startErrorHandshakerService();
  auto handshaker = AltsTsiHandshaker::createForServer(getChannel());
  Event::MockDispatcher dispatcher;
  auto tsi_handshaker = std::make_unique<TsiHandshaker>(std::move(handshaker), dispatcher);

  // Try to get the ServerInit and ServerFinished and observe failure.
  {
    CapturingTsiHandshakerCallbacks capturing_callbacks;
    tsi_handshaker->setHandshakerCallbacks(capturing_callbacks);
    EXPECT_CALL(dispatcher, post(_)).WillOnce([](Envoy::Event::PostCb cb) { cb(); });
    Buffer::OwnedImpl received_bytes;
    received_bytes.add(ClientInit.data(), ClientInit.size());
    ASSERT_THAT(tsi_handshaker->next(received_bytes), StatusCodeIs(absl::StatusCode::kInternal));

    auto result = capturing_callbacks.getNextResult();
    EXPECT_THAT(result, NotNull());
    EXPECT_THAT(result->status_, StatusCodeIs(absl::StatusCode::kInternal));
    EXPECT_EQ(result->to_send_->toString(), "");
    EXPECT_THAT(result->result_, IsNull());
  }
}

} // namespace
} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
