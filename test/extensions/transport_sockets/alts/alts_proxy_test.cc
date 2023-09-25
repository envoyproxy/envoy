#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "source/extensions/transport_sockets/alts/alts_proxy.h"

#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
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

using ::Envoy::StatusHelpers::StatusIs;
using ::grpc::gcp::HandshakeProtocol;
using ::grpc::gcp::HandshakerReq;
using ::grpc::gcp::HandshakerResp;
using ::grpc::gcp::HandshakerService;
using ::grpc::gcp::ServerHandshakeParameters;
using ::grpc::gcp::StartClientHandshakeReq;
using ::grpc::gcp::StartServerHandshakeReq;

constexpr absl::string_view kClientStartResponse = "client_start_response";
constexpr absl::string_view kServerStartResponse = "server_start_response";
constexpr absl::string_view kNextResponse = "next_response";

HandshakerResp ExpectedClientStartResponse() {
  HandshakerResp response;
  response.set_out_frames(kClientStartResponse);
  return response;
}

HandshakerResp ExpectedServerStartResponse() {
  HandshakerResp response;
  response.set_out_frames(kServerStartResponse);
  return response;
}

HandshakerResp ExpectedNextResponse() {
  HandshakerResp response;
  response.set_out_frames(kNextResponse);
  return response;
}

class FakeHandshakerService final : public HandshakerService::Service {
public:
  FakeHandshakerService(const std::vector<HandshakerReq>& expected_requests,
                        grpc::Status status_to_return, bool return_error_response)
      : expected_requests_(expected_requests), status_to_return_(status_to_return),
        return_error_response_(return_error_response) {}

  grpc::Status
  DoHandshake(grpc::ServerContext*,
              grpc::ServerReaderWriter<HandshakerResp, HandshakerReq>* stream) override {
    HandshakerReq request;
    int request_number = 0;
    while (stream->Read(&request)) {
      if (!status_to_return_.ok())
        return status_to_return_;
      if (request_number < static_cast<int>(expected_requests_.size())) {
        EXPECT_TRUE(TestUtility::protoEqual(request, expected_requests_[request_number]));
        request_number++;
      }
      HandshakerResp response;
      if (return_error_response_) {
        response.mutable_status()->set_code(static_cast<int>(grpc::StatusCode::INTERNAL));
        response.mutable_status()->set_details("An internal error occurred.");
      } else if (request.has_client_start()) {
        response.set_out_frames(kClientStartResponse);
      } else if (request.has_server_start()) {
        response.set_out_frames(kServerStartResponse);
      } else if (request.has_next()) {
        response.set_out_frames(kNextResponse);
      }
      EXPECT_TRUE(stream->Write(response));
    }
    return grpc::Status::OK;
  }

private:
  std::vector<HandshakerReq> expected_requests_;
  grpc::Status status_to_return_;
  bool return_error_response_;
};

class AltsProxyTest : public testing::Test {
protected:
  void StartFakeHandshakerService(const std::vector<HandshakerReq>& expected_requests,
                                  grpc::Status status_to_return,
                                  bool return_error_response = false) {
    server_address_ = absl::StrCat("[::1]:", 0);
    absl::Notification notification;
    server_thread_ = std::make_unique<std::thread>(
        [this, &notification, expected_requests, status_to_return, return_error_response]() {
          FakeHandshakerService fake_handshaker_service(expected_requests, status_to_return,
                                                        return_error_response);
          grpc::ServerBuilder server_builder;
          int listening_port = -1;
          server_builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials(),
                                          &listening_port);
          server_builder.RegisterService(&fake_handshaker_service);
          server_ = server_builder.BuildAndStart();
          EXPECT_THAT(server_, ::testing::NotNull());
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

  std::shared_ptr<grpc::Channel> GetChannel() {
    return grpc::CreateChannel(server_address_,
                               grpc::InsecureChannelCredentials()); // NOLINT
  }

private:
  std::string server_address_;
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<std::thread> server_thread_;
};

TEST_F(AltsProxyTest, ClientStartSuccess) {
  HandshakerReq expected_request;
  StartClientHandshakeReq* expected_client_start = expected_request.mutable_client_start();
  expected_client_start->set_handshake_security_protocol(grpc::gcp::ALTS);
  expected_client_start->add_application_protocols(kApplicationProtocol);
  expected_client_start->add_record_protocols(kRecordProtocol);
  expected_client_start->mutable_rpc_versions()->mutable_max_rpc_version()->set_major(
      kMaxMajorRpcVersion);
  expected_client_start->mutable_rpc_versions()->mutable_max_rpc_version()->set_minor(
      kMaxMinorRpcVersion);
  expected_client_start->mutable_rpc_versions()->mutable_min_rpc_version()->set_major(
      kMinMajorRpcVersion);
  expected_client_start->mutable_rpc_versions()->mutable_min_rpc_version()->set_minor(
      kMinMinorRpcVersion);
  expected_client_start->set_max_frame_size(kMaxFrameSize);
  StartFakeHandshakerService({expected_request}, grpc::Status::OK);

  auto alts_proxy = AltsProxy::Create(GetChannel());
  EXPECT_OK(alts_proxy.status());
  EXPECT_TRUE(TestUtility::protoEqual((*alts_proxy)->SendStartClientHandshakeReq().value(),
                                      ExpectedClientStartResponse()));
}

TEST_F(AltsProxyTest, ClientFullHandshakeSuccess) {
  HandshakerReq expected_request_1;
  StartClientHandshakeReq* expected_client_start = expected_request_1.mutable_client_start();
  expected_client_start->set_handshake_security_protocol(grpc::gcp::ALTS);
  expected_client_start->add_application_protocols(kApplicationProtocol);
  expected_client_start->add_record_protocols(kRecordProtocol);
  expected_client_start->mutable_rpc_versions()->mutable_max_rpc_version()->set_major(
      kMaxMajorRpcVersion);
  expected_client_start->mutable_rpc_versions()->mutable_max_rpc_version()->set_minor(
      kMaxMinorRpcVersion);
  expected_client_start->mutable_rpc_versions()->mutable_min_rpc_version()->set_major(
      kMinMajorRpcVersion);
  expected_client_start->mutable_rpc_versions()->mutable_min_rpc_version()->set_minor(
      kMinMinorRpcVersion);
  expected_client_start->set_max_frame_size(kMaxFrameSize);
  HandshakerReq expected_request_2;
  expected_request_2.mutable_next()->set_in_bytes(kServerStartResponse);
  StartFakeHandshakerService({expected_request_1, expected_request_2}, grpc::Status::OK);

  auto alts_proxy = AltsProxy::Create(GetChannel());
  EXPECT_OK(alts_proxy.status());
  auto resp = (*alts_proxy)->SendStartClientHandshakeReq();
  EXPECT_OK(resp);
  grpc::gcp::HandshakerResp& handshaker_resp = resp.value();
  EXPECT_TRUE(TestUtility::protoEqual(handshaker_resp, ExpectedClientStartResponse()));

  resp = (*alts_proxy)->SendNextHandshakeReq(kServerStartResponse);
  EXPECT_OK(resp);
  handshaker_resp = resp.value();
  EXPECT_TRUE(TestUtility::protoEqual(handshaker_resp, ExpectedNextResponse()));
}

TEST_F(AltsProxyTest, ServerStartSuccess) {
  HandshakerReq expected_request;
  ServerHandshakeParameters server_parameters;
  server_parameters.add_record_protocols(kRecordProtocol);
  StartServerHandshakeReq* expected_server_start = expected_request.mutable_server_start();
  expected_server_start->add_application_protocols(kApplicationProtocol);
  (*expected_server_start->mutable_handshake_parameters())[HandshakeProtocol::ALTS] =
      server_parameters;
  expected_server_start->mutable_rpc_versions()->mutable_max_rpc_version()->set_major(
      kMaxMajorRpcVersion);
  expected_server_start->mutable_rpc_versions()->mutable_max_rpc_version()->set_minor(
      kMaxMinorRpcVersion);
  expected_server_start->mutable_rpc_versions()->mutable_min_rpc_version()->set_major(
      kMinMajorRpcVersion);
  expected_server_start->mutable_rpc_versions()->mutable_min_rpc_version()->set_minor(
      kMinMinorRpcVersion);
  expected_server_start->set_in_bytes(kClientStartResponse);
  expected_server_start->set_max_frame_size(kMaxFrameSize);
  StartFakeHandshakerService({expected_request}, grpc::Status::OK);

  auto alts_proxy = AltsProxy::Create(GetChannel());
  EXPECT_OK(alts_proxy.status());

  auto resp = (*alts_proxy)->SendStartServerHandshakeReq(kClientStartResponse);
  EXPECT_OK(resp);
  grpc::gcp::HandshakerResp& handshaker_resp = resp.value();
  EXPECT_TRUE(TestUtility::protoEqual(handshaker_resp, ExpectedServerStartResponse()));

  resp = (*alts_proxy)->SendNextHandshakeReq(kClientStartResponse);
  EXPECT_OK(resp);
  handshaker_resp = resp.value();
  EXPECT_TRUE(TestUtility::protoEqual(handshaker_resp, ExpectedNextResponse()));
}

TEST_F(AltsProxyTest, ServerFullHandshakeSuccess) {
  HandshakerReq expected_request_1;
  ServerHandshakeParameters server_parameters;
  server_parameters.add_record_protocols(kRecordProtocol);
  StartServerHandshakeReq* expected_server_start = expected_request_1.mutable_server_start();
  expected_server_start->add_application_protocols(kApplicationProtocol);
  (*expected_server_start->mutable_handshake_parameters())[HandshakeProtocol::ALTS] =
      server_parameters;
  expected_server_start->mutable_rpc_versions()->mutable_max_rpc_version()->set_major(
      kMaxMajorRpcVersion);
  expected_server_start->mutable_rpc_versions()->mutable_max_rpc_version()->set_minor(
      kMaxMinorRpcVersion);
  expected_server_start->mutable_rpc_versions()->mutable_min_rpc_version()->set_major(
      kMinMajorRpcVersion);
  expected_server_start->mutable_rpc_versions()->mutable_min_rpc_version()->set_minor(
      kMinMinorRpcVersion);
  expected_server_start->set_in_bytes(kClientStartResponse);
  expected_server_start->set_max_frame_size(kMaxFrameSize);
  HandshakerReq expected_request_2;
  expected_request_2.mutable_next()->set_in_bytes(kServerStartResponse);
  StartFakeHandshakerService({expected_request_1, expected_request_2}, grpc::Status::OK);

  auto alts_proxy = AltsProxy::Create(GetChannel());
  EXPECT_OK(alts_proxy.status());
  EXPECT_TRUE(TestUtility::protoEqual(
      (*alts_proxy)->SendStartServerHandshakeReq(kClientStartResponse).value(),
      ExpectedServerStartResponse()));
  EXPECT_TRUE(TestUtility::protoEqual(
      (*alts_proxy)->SendNextHandshakeReq(kServerStartResponse).value(), ExpectedNextResponse()));
}

TEST_F(AltsProxyTest, CreateFailsDueToNullChannel) {
  EXPECT_THAT(AltsProxy::Create(/*handshaker_service_channel=*/nullptr),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(AltsProxyTest, ClientStartRpcLevelFailure) {
  StartFakeHandshakerService(
      /*expected_requests=*/{},
      grpc::Status(grpc::StatusCode::INTERNAL, "An RPC-level internal error occurred."));

  auto alts_proxy = AltsProxy::Create(GetChannel());
  EXPECT_OK(alts_proxy.status());
  EXPECT_THAT((*alts_proxy)->SendStartClientHandshakeReq(), StatusIs(absl::StatusCode::kInternal));
}

TEST_F(AltsProxyTest, ServerStartRpcLevelFailure) {
  StartFakeHandshakerService(
      /*expected_requests=*/{},
      grpc::Status(grpc::StatusCode::INTERNAL, "An RPC-level internal error occurred."));

  auto alts_proxy = AltsProxy::Create(GetChannel());
  EXPECT_OK(alts_proxy.status());
  EXPECT_THAT((*alts_proxy)->SendStartServerHandshakeReq(kClientStartResponse),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(AltsProxyTest, NextRpcLevelFailure) {
  StartFakeHandshakerService(
      /*expected_requests=*/{},
      grpc::Status(grpc::StatusCode::INTERNAL, "An RPC-level internal error occurred."));

  auto alts_proxy = AltsProxy::Create(GetChannel());
  EXPECT_OK(alts_proxy.status());
  EXPECT_THAT((*alts_proxy)->SendNextHandshakeReq(kServerStartResponse),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(AltsProxyTest, ClientStartRequestLevelFailure) {
  StartFakeHandshakerService(
      /*expected_requests=*/{}, grpc::Status::OK,
      /*return_error_response=*/true);

  auto alts_proxy = AltsProxy::Create(GetChannel());
  EXPECT_OK(alts_proxy.status());
  EXPECT_THAT((*alts_proxy)->SendStartClientHandshakeReq(), StatusIs(absl::StatusCode::kInternal));
}

TEST_F(AltsProxyTest, ServerStartRequestLevelFailure) {
  StartFakeHandshakerService(
      /*expected_requests=*/{}, grpc::Status::OK,
      /*return_error_response=*/true);

  auto alts_proxy = AltsProxy::Create(GetChannel());
  EXPECT_OK(alts_proxy.status());
  EXPECT_THAT((*alts_proxy)->SendStartServerHandshakeReq(kClientStartResponse),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(AltsProxyTest, NextRequestLevelFailure) {
  StartFakeHandshakerService(
      /*expected_requests=*/{}, grpc::Status::OK,
      /*return_error_response=*/true);

  auto alts_proxy = AltsProxy::Create(GetChannel());
  EXPECT_OK(alts_proxy.status());
  EXPECT_THAT((*alts_proxy)->SendNextHandshakeReq(kServerStartResponse),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(AltsProxyTest, HandshakerServiceIsUnreacheableOnClientStart) {
  std::string unreacheable_address = absl::StrCat("[::1]:", "1001");
  auto channel = grpc::CreateChannel(unreacheable_address,
                                     grpc::InsecureChannelCredentials()); // NOLINT
  auto alts_proxy = AltsProxy::Create(channel);
  EXPECT_OK(alts_proxy.status());
  EXPECT_THAT((*alts_proxy)->SendStartClientHandshakeReq(),
              StatusIs(absl::StatusCode::kUnavailable));
}

TEST_F(AltsProxyTest, HandshakerServiceIsUnreacheableOnServerStart) {
  std::string unreacheable_address = absl::StrCat("[::1]:", "1001");
  auto channel = grpc::CreateChannel(unreacheable_address,
                                     grpc::InsecureChannelCredentials()); // NOLINT
  auto alts_proxy = AltsProxy::Create(channel);
  EXPECT_OK(alts_proxy.status());
  absl::string_view in_bytes;
  EXPECT_THAT((*alts_proxy)->SendStartServerHandshakeReq(in_bytes),
              StatusIs(absl::StatusCode::kUnavailable));
}

TEST_F(AltsProxyTest, HandshakerServiceIsUnreacheableOnNextRequest) {
  std::string unreacheable_address = absl::StrCat("[::1]:", "1001");
  auto channel = grpc::CreateChannel(unreacheable_address,
                                     grpc::InsecureChannelCredentials()); // NOLINT
  auto alts_proxy = AltsProxy::Create(channel);
  EXPECT_OK(alts_proxy.status());
  absl::string_view in_bytes;
  EXPECT_THAT((*alts_proxy)->SendNextHandshakeReq(in_bytes),
              StatusIs(absl::StatusCode::kUnavailable));
}

} // namespace
} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
