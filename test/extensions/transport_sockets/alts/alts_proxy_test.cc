#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "envoy/network/address.h"

#include "source/extensions/transport_sockets/alts/alts_proxy.h"

#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/types/span.h"
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

constexpr absl::string_view ClientStartResponse = "client_start_response";
constexpr absl::string_view ServerStartResponse = "server_start_response";
constexpr absl::string_view NextResponse = "next_response";

HandshakerResp expectedClientStartResponse() {
  HandshakerResp response;
  response.set_out_frames(ClientStartResponse);
  return response;
}

HandshakerResp expectedServerStartResponse() {
  HandshakerResp response;
  response.set_out_frames(ServerStartResponse);
  return response;
}

HandshakerResp expectedNextResponse() {
  HandshakerResp response;
  response.set_out_frames(NextResponse);
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
      if (!status_to_return_.ok()) {
        return status_to_return_;
      }
      if (request_number < static_cast<int>(expected_requests_.size())) {
        EXPECT_TRUE(TestUtility::protoEqual(request, expected_requests_[request_number]));
        request_number++;
      }
      HandshakerResp response;
      if (return_error_response_) {
        response.mutable_status()->set_code(static_cast<int>(grpc::StatusCode::INTERNAL));
        response.mutable_status()->set_details("An internal error occurred.");
      } else if (request.has_client_start()) {
        response.set_out_frames(ClientStartResponse);
      } else if (request.has_server_start()) {
        response.set_out_frames(ServerStartResponse);
      } else if (request.has_next()) {
        response.set_out_frames(NextResponse);
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

class AltsProxyTest : public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  AltsProxyTest() : version_(GetParam()){};
  void startFakeHandshakerService(const std::vector<HandshakerReq>& expected_requests,
                                  grpc::Status status_to_return,
                                  bool return_error_response = false) {
    server_address_ = absl::StrCat(Network::Test::getLoopbackAddressUrlString(version_), ":0");
    absl::Notification notification;
    server_thread_ = std::make_unique<std::thread>([this, &notification, expected_requests,
                                                    status_to_return, return_error_response]() {
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

INSTANTIATE_TEST_SUITE_P(IpVersions, AltsProxyTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verify that a StartClientHandshakeReq can successfully be created, sent to
// the handshaker service, and the correct response is received.
TEST_P(AltsProxyTest, ClientStartSuccess) {
  HandshakerReq expected_request;
  StartClientHandshakeReq* expected_client_start = expected_request.mutable_client_start();
  expected_client_start->set_handshake_security_protocol(grpc::gcp::ALTS);
  expected_client_start->add_application_protocols(ApplicationProtocol);
  expected_client_start->add_record_protocols(RecordProtocol);
  expected_client_start->mutable_rpc_versions()->mutable_max_rpc_version()->set_major(
      MaxMajorRpcVersion);
  expected_client_start->mutable_rpc_versions()->mutable_max_rpc_version()->set_minor(
      MaxMinorRpcVersion);
  expected_client_start->mutable_rpc_versions()->mutable_min_rpc_version()->set_major(
      MinMajorRpcVersion);
  expected_client_start->mutable_rpc_versions()->mutable_min_rpc_version()->set_minor(
      MinMinorRpcVersion);
  expected_client_start->set_max_frame_size(MaxFrameSize);
  startFakeHandshakerService({expected_request}, grpc::Status::OK);

  auto alts_proxy = AltsProxy::create(getChannel());
  EXPECT_OK(alts_proxy.status());
  EXPECT_TRUE(TestUtility::protoEqual((*alts_proxy)->sendStartClientHandshakeReq().value(),
                                      expectedClientStartResponse()));
}

// Verify that a full client-side ALTS handshake can be performed when talking
// to a fake handshaker service.
TEST_P(AltsProxyTest, ClientFullHandshakeSuccess) {
  HandshakerReq expected_request_1;
  StartClientHandshakeReq* expected_client_start = expected_request_1.mutable_client_start();
  expected_client_start->set_handshake_security_protocol(grpc::gcp::ALTS);
  expected_client_start->add_application_protocols(ApplicationProtocol);
  expected_client_start->add_record_protocols(RecordProtocol);
  expected_client_start->mutable_rpc_versions()->mutable_max_rpc_version()->set_major(
      MaxMajorRpcVersion);
  expected_client_start->mutable_rpc_versions()->mutable_max_rpc_version()->set_minor(
      MaxMinorRpcVersion);
  expected_client_start->mutable_rpc_versions()->mutable_min_rpc_version()->set_major(
      MinMajorRpcVersion);
  expected_client_start->mutable_rpc_versions()->mutable_min_rpc_version()->set_minor(
      MinMinorRpcVersion);
  expected_client_start->set_max_frame_size(MaxFrameSize);
  HandshakerReq expected_request_2;
  expected_request_2.mutable_next()->set_in_bytes(ServerStartResponse);
  startFakeHandshakerService({expected_request_1, expected_request_2}, grpc::Status::OK);

  auto alts_proxy = AltsProxy::create(getChannel());
  EXPECT_OK(alts_proxy.status());
  auto resp = (*alts_proxy)->sendStartClientHandshakeReq();
  EXPECT_OK(resp);
  grpc::gcp::HandshakerResp& handshaker_resp = resp.value();
  EXPECT_TRUE(TestUtility::protoEqual(handshaker_resp, expectedClientStartResponse()));

  resp = (*alts_proxy)
             ->sendNextHandshakeReq(
                 absl::MakeSpan(reinterpret_cast<const uint8_t*>(ServerStartResponse.data()),
                                ServerStartResponse.size()));
  EXPECT_OK(resp);
  handshaker_resp = resp.value();
  EXPECT_TRUE(TestUtility::protoEqual(handshaker_resp, expectedNextResponse()));
}

// Verify that a StartServerHandshakeReq can successfully be created, sent to
// the handshaker service, and the correct response is received.
TEST_P(AltsProxyTest, ServerStartSuccess) {
  HandshakerReq expected_request;
  ServerHandshakeParameters server_parameters;
  server_parameters.add_record_protocols(RecordProtocol);
  StartServerHandshakeReq* expected_server_start = expected_request.mutable_server_start();
  expected_server_start->add_application_protocols(ApplicationProtocol);
  (*expected_server_start->mutable_handshake_parameters())[HandshakeProtocol::ALTS] =
      server_parameters;
  expected_server_start->mutable_rpc_versions()->mutable_max_rpc_version()->set_major(
      MaxMajorRpcVersion);
  expected_server_start->mutable_rpc_versions()->mutable_max_rpc_version()->set_minor(
      MaxMinorRpcVersion);
  expected_server_start->mutable_rpc_versions()->mutable_min_rpc_version()->set_major(
      MinMajorRpcVersion);
  expected_server_start->mutable_rpc_versions()->mutable_min_rpc_version()->set_minor(
      MinMinorRpcVersion);
  expected_server_start->set_in_bytes(ClientStartResponse);
  expected_server_start->set_max_frame_size(MaxFrameSize);
  startFakeHandshakerService({expected_request}, grpc::Status::OK);

  auto alts_proxy = AltsProxy::create(getChannel());
  EXPECT_OK(alts_proxy.status());

  auto resp = (*alts_proxy)
                  ->sendStartServerHandshakeReq(
                      absl::MakeSpan(reinterpret_cast<const uint8_t*>(ClientStartResponse.data()),
                                     ClientStartResponse.size()));
  EXPECT_OK(resp);
  grpc::gcp::HandshakerResp& handshaker_resp = resp.value();
  EXPECT_TRUE(TestUtility::protoEqual(handshaker_resp, expectedServerStartResponse()));
}

// Verify that a full server-side ALTS handshake can be performed when talking
// to a fake handshaker service.
TEST_P(AltsProxyTest, ServerFullHandshakeSuccess) {
  HandshakerReq expected_request_1;
  ServerHandshakeParameters server_parameters;
  server_parameters.add_record_protocols(RecordProtocol);
  StartServerHandshakeReq* expected_server_start = expected_request_1.mutable_server_start();
  expected_server_start->add_application_protocols(ApplicationProtocol);
  (*expected_server_start->mutable_handshake_parameters())[HandshakeProtocol::ALTS] =
      server_parameters;
  expected_server_start->mutable_rpc_versions()->mutable_max_rpc_version()->set_major(
      MaxMajorRpcVersion);
  expected_server_start->mutable_rpc_versions()->mutable_max_rpc_version()->set_minor(
      MaxMinorRpcVersion);
  expected_server_start->mutable_rpc_versions()->mutable_min_rpc_version()->set_major(
      MinMajorRpcVersion);
  expected_server_start->mutable_rpc_versions()->mutable_min_rpc_version()->set_minor(
      MinMinorRpcVersion);
  expected_server_start->set_in_bytes(ClientStartResponse);
  expected_server_start->set_max_frame_size(MaxFrameSize);
  HandshakerReq expected_request_2;
  expected_request_2.mutable_next()->set_in_bytes(ServerStartResponse);
  startFakeHandshakerService({expected_request_1, expected_request_2}, grpc::Status::OK);

  auto alts_proxy = AltsProxy::create(getChannel());
  EXPECT_OK(alts_proxy.status());
  EXPECT_TRUE(
      TestUtility::protoEqual((*alts_proxy)
                                  ->sendStartServerHandshakeReq(absl::MakeSpan(
                                      reinterpret_cast<const uint8_t*>(ClientStartResponse.data()),
                                      ClientStartResponse.size()))
                                  .value(),
                              expectedServerStartResponse()));
  EXPECT_TRUE(
      TestUtility::protoEqual((*alts_proxy)
                                  ->sendNextHandshakeReq(absl::MakeSpan(
                                      reinterpret_cast<const uint8_t*>(ServerStartResponse.data()),
                                      ServerStartResponse.size()))
                                  .value(),
                              expectedNextResponse()));
}

// Check that the AltsProxy cannot be created when the channel to the handshaker
// service is nullptr.
TEST_P(AltsProxyTest, CreateFailsDueToNullChannel) {
  EXPECT_THAT(AltsProxy::create(/*handshaker_service_channel=*/nullptr),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

// Check that the AltsProxy correctly handles gRPC-level errors returned by the
// handshaker service when sending a StartClientHandshakeReq.
TEST_P(AltsProxyTest, ClientStartRpcLevelFailure) {
  startFakeHandshakerService(
      /*expected_requests=*/{},
      grpc::Status(grpc::StatusCode::INTERNAL, "An RPC-level internal error occurred."));

  auto alts_proxy = AltsProxy::create(getChannel());
  EXPECT_OK(alts_proxy.status());
  EXPECT_THAT((*alts_proxy)->sendStartClientHandshakeReq(), StatusIs(absl::StatusCode::kInternal));
}

// Check that the AltsProxy correctly handles gRPC-level errors returned by the
// handshaker service when sending a StartServerHandshakeReq.
TEST_P(AltsProxyTest, ServerStartRpcLevelFailure) {
  startFakeHandshakerService(
      /*expected_requests=*/{},
      grpc::Status(grpc::StatusCode::INTERNAL, "An RPC-level internal error occurred."));

  auto alts_proxy = AltsProxy::create(getChannel());
  EXPECT_OK(alts_proxy.status());
  EXPECT_THAT((*alts_proxy)
                  ->sendStartServerHandshakeReq(
                      absl::MakeSpan(reinterpret_cast<const uint8_t*>(ClientStartResponse.data()),
                                     ClientStartResponse.size())),
              StatusIs(absl::StatusCode::kInternal));
}

// Check that the AltsProxy correctly handles gRPC-level errors returned by the
// handshaker service when sending a NextHandshakeReq.
TEST_P(AltsProxyTest, NextRpcLevelFailure) {
  startFakeHandshakerService(
      /*expected_requests=*/{},
      grpc::Status(grpc::StatusCode::INTERNAL, "An RPC-level internal error occurred."));

  auto alts_proxy = AltsProxy::create(getChannel());
  EXPECT_OK(alts_proxy.status());
  EXPECT_THAT((*alts_proxy)
                  ->sendNextHandshakeReq(
                      absl::MakeSpan(reinterpret_cast<const uint8_t*>(ServerStartResponse.data()),
                                     ServerStartResponse.size())),
              StatusIs(absl::StatusCode::kInternal));
}

// Check that the AltsProxy correctly handles a handshake-level error returned by the
// handshaker service when sending a StartClientHandshakeReq.
TEST_P(AltsProxyTest, ClientStartRequestLevelFailure) {
  startFakeHandshakerService(
      /*expected_requests=*/{}, grpc::Status::OK,
      /*return_error_response=*/true);

  auto alts_proxy = AltsProxy::create(getChannel());
  EXPECT_OK(alts_proxy.status());
  EXPECT_THAT((*alts_proxy)->sendStartClientHandshakeReq(), StatusIs(absl::StatusCode::kInternal));
}

// Check that the AltsProxy correctly handles a handshake-level error returned by the
// handshaker service when sending a StartServerHandshakeReq.
TEST_P(AltsProxyTest, ServerStartRequestLevelFailure) {
  startFakeHandshakerService(
      /*expected_requests=*/{}, grpc::Status::OK,
      /*return_error_response=*/true);

  auto alts_proxy = AltsProxy::create(getChannel());
  EXPECT_OK(alts_proxy.status());
  EXPECT_THAT((*alts_proxy)
                  ->sendStartServerHandshakeReq(
                      absl::MakeSpan(reinterpret_cast<const uint8_t*>(ClientStartResponse.data()),
                                     ClientStartResponse.size())),
              StatusIs(absl::StatusCode::kInternal));
}

// Check that the AltsProxy correctly handles a handshake-level error returned by the
// handshaker service when sending a NextHandshakeReq.
TEST_P(AltsProxyTest, NextRequestLevelFailure) {
  startFakeHandshakerService(
      /*expected_requests=*/{}, grpc::Status::OK,
      /*return_error_response=*/true);

  auto alts_proxy = AltsProxy::create(getChannel());
  EXPECT_OK(alts_proxy.status());
  EXPECT_THAT((*alts_proxy)
                  ->sendNextHandshakeReq(
                      absl::MakeSpan(reinterpret_cast<const uint8_t*>(ServerStartResponse.data()),
                                     ServerStartResponse.size())),
              StatusIs(absl::StatusCode::kInternal));
}

// Check that the AltsProxy correctly handles an unreachable handshaker service
// when sending a StartClientHandshakeReq.
TEST_P(AltsProxyTest, HandshakerServiceIsUnreacheableOnClientStart) {
  std::string unreacheable_address = absl::StrCat("[::1]:", "1001");
  auto channel = grpc::CreateChannel(unreacheable_address,
                                     grpc::InsecureChannelCredentials()); // NOLINT
  auto alts_proxy = AltsProxy::create(channel);
  EXPECT_OK(alts_proxy.status());
  EXPECT_THAT((*alts_proxy)->sendStartClientHandshakeReq(),
              StatusIs(absl::StatusCode::kUnavailable));
}

// Check that the AltsProxy correctly handles an unreachable handshaker service
// when sending a StartServerHandshakeReq.
TEST_P(AltsProxyTest, HandshakerServiceIsUnreacheableOnServerStart) {
  std::string unreacheable_address = absl::StrCat("[::1]:", "1001");
  auto channel = grpc::CreateChannel(unreacheable_address,
                                     grpc::InsecureChannelCredentials()); // NOLINT
  auto alts_proxy = AltsProxy::create(channel);
  EXPECT_OK(alts_proxy.status());
  absl::Span<const uint8_t> in_bytes;
  EXPECT_THAT((*alts_proxy)->sendStartServerHandshakeReq(in_bytes),
              StatusIs(absl::StatusCode::kUnavailable));
}

// Check that the AltsProxy correctly handles an unreachable handshaker service
// when sending a NextHandshakeReq.
TEST_P(AltsProxyTest, HandshakerServiceIsUnreacheableOnNextRequest) {
  std::string unreacheable_address = absl::StrCat("[::1]:", "1001");
  auto channel = grpc::CreateChannel(unreacheable_address,
                                     grpc::InsecureChannelCredentials()); // NOLINT
  auto alts_proxy = AltsProxy::create(channel);
  EXPECT_OK(alts_proxy.status());
  absl::Span<const uint8_t> in_bytes;
  EXPECT_THAT((*alts_proxy)->sendNextHandshakeReq(in_bytes),
              StatusIs(absl::StatusCode::kUnavailable));
}

} // namespace
} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
