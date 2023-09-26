#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/transport_sockets/alts/alts_proxy.h"
#include "source/extensions/transport_sockets/alts/alts_tsi_handshaker.h"
#include "source/extensions/transport_sockets/alts/tsi_handshaker.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/status_utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "grpcpp/channel.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/credentials.h"
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

using ::grpc::gcp::HandshakerReq;
using ::grpc::gcp::HandshakerResp;
using ::grpc::gcp::HandshakerResult;
using ::grpc::gcp::HandshakerService;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Test;

constexpr absl::string_view kClientInit = "CLIENT_INIT";
constexpr absl::string_view kServerInit = "SERVER_INIT";
constexpr absl::string_view kClientFinished = "CLIENT_FINISHED";
constexpr absl::string_view kServerFinished = "SERVER_FINISHED";

constexpr absl::string_view kKeyData = "fake_key_data_needs_to_be_at_least_44_characters_long";
constexpr absl::string_view kLocalServiceAccount = "local_service_account";
constexpr absl::string_view kPeerServiceAccount = "peer_service_account";

void populateHandshakeResult(HandshakerResult* result) {
  result->mutable_peer_identity()->set_service_account(kPeerServiceAccount);
  result->mutable_peer_rpc_versions();
  result->mutable_local_identity()->set_service_account(kLocalServiceAccount);
  result->set_application_protocol(kApplicationProtocol);
  result->set_record_protocol(kRecordProtocol);
  result->set_key_data(kKeyData);
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
        response.set_out_frames(kClientInit);
        response.set_bytes_consumed(kClientInit.size());
      } else if (request.has_server_start()) {
        // The request contains a StartServerHandshakeReq message.
        EXPECT_EQ(request.server_start().in_bytes(), kClientInit);
        std::string out_frames = absl::StrCat(kServerInit, kServerFinished);
        response.set_out_frames(out_frames);
        response.set_bytes_consumed(out_frames.size());
      } else if (request.has_next()) {
        // The request contains a NextHandshakeMessageReq message.
        std::string expected_in_bytes = is_assisting_client
                                            ? absl::StrCat(kServerInit, kServerFinished)
                                            : std::string(kClientFinished);
        EXPECT_EQ(request.next().in_bytes(), expected_in_bytes);
        response.set_bytes_consumed(expected_in_bytes.size());
        if (is_assisting_client) {
          response.set_out_frames(kClientFinished);
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

class AltsTsiHandshakerTest : public Test {
protected:
  void startFakeHandshakerService() {
    server_address_ = absl::StrCat("[::1]:", 0);
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
      server_address_ = absl::StrCat("[::1]:", listening_port);
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
};

TEST_F(AltsTsiHandshakerTest, ClientSideFullHandshake) {
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
    EXPECT_EQ(result->to_send_->toString(), kClientInit);
    EXPECT_THAT(result->result_, IsNull());
  }

  // Get the ClientFinished and the handshake result.
  {
    CapturingTsiHandshakerCallbacks capturing_callbacks;
    tsi_handshaker->setHandshakerCallbacks(capturing_callbacks);
    EXPECT_CALL(dispatcher, post(_)).WillOnce([](Envoy::Event::PostCb cb) { cb(); });
    Buffer::OwnedImpl received_bytes;
    received_bytes.add(kServerInit.data(), kServerInit.size());
    received_bytes.add(kServerFinished.data(), kServerFinished.size());
    EXPECT_TRUE(tsi_handshaker->next(received_bytes).ok());

    auto result = capturing_callbacks.getNextResult();
    EXPECT_THAT(result, NotNull());
    EXPECT_TRUE(result->status_.ok());
    EXPECT_EQ(result->to_send_->toString(), kClientFinished);
    EXPECT_THAT(result->result_, NotNull());
    EXPECT_THAT(result->result_->frame_protector, NotNull());
    EXPECT_EQ(result->result_->peer_identity, kPeerServiceAccount);
    EXPECT_EQ(result->result_->unused_bytes, "");
  }
}

TEST_F(AltsTsiHandshakerTest, ServerSideFullHandshake) {
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
    received_bytes.add(kClientInit.data(), kClientInit.size());
    EXPECT_OK(tsi_handshaker->next(received_bytes));

    auto result = capturing_callbacks.getNextResult();
    EXPECT_THAT(result, NotNull());
    EXPECT_TRUE(result->status_.ok());
    EXPECT_EQ(result->to_send_->toString(), absl::StrCat(kServerInit, kServerFinished));
    EXPECT_THAT(result->result_, IsNull());
  }

  // Get the handshake result.
  {
    CapturingTsiHandshakerCallbacks capturing_callbacks;
    tsi_handshaker->setHandshakerCallbacks(capturing_callbacks);
    EXPECT_CALL(dispatcher, post(_)).WillOnce([](Envoy::Event::PostCb cb) { cb(); });
    Buffer::OwnedImpl received_bytes;
    received_bytes.add(kClientFinished.data(), kClientFinished.size());
    EXPECT_TRUE(tsi_handshaker->next(received_bytes).ok());

    auto result = capturing_callbacks.getNextResult();
    EXPECT_THAT(result, NotNull());
    EXPECT_TRUE(result->status_.ok());
    EXPECT_EQ(result->to_send_->toString(), "");
    EXPECT_THAT(result->result_, NotNull());
    EXPECT_THAT(result->result_->frame_protector, NotNull());
    EXPECT_EQ(result->result_->peer_identity, kPeerServiceAccount);
    EXPECT_EQ(result->result_->unused_bytes, "");
  }
}

} // namespace
} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
