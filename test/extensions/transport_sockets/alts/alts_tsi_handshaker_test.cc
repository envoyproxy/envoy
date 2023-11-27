#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "envoy/network/address.h"

#include "source/extensions/transport_sockets/alts/alts_proxy.h"
#include "source/extensions/transport_sockets/alts/alts_tsi_handshaker.h"

#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/strings/match.h"
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

using ::Envoy::StatusHelpers::StatusCodeIs;
using ::Envoy::StatusHelpers::StatusIs;
using ::grpc::gcp::HandshakerReq;
using ::grpc::gcp::HandshakerResp;
using ::grpc::gcp::HandshakerResult;
using ::grpc::gcp::HandshakerService;
using ::testing::IsNull;
using ::testing::NotNull;

constexpr absl::string_view ApplicationData = "APPLICATION_DATA";
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

class FakeHandshakerService final : public HandshakerService::Service {
public:
  FakeHandshakerService() = default;

  grpc::Status
  DoHandshake(grpc::ServerContext*,
              grpc::ServerReaderWriter<HandshakerResp, HandshakerReq>* stream) override {
    bool is_assisting_client = false;
    bool is_handshake_complete = false;
    bool sent_client_finished = false;
    bool sent_server_init_and_server_finished = false;
    std::string bytes_from_client;
    std::string bytes_from_server;
    HandshakerReq request;
    while (stream->Read(&request)) {
      HandshakerResp response;
      if (request.has_client_start()) {
        // The request contains a StartClientHandshakeReq message.
        is_assisting_client = true;
        response.set_out_frames(ClientInit);
        response.set_bytes_consumed(ClientInit.size());
      } else if (request.has_server_start()) {
        // The request contains a StartServerHandshakeReq message.
        if (absl::StartsWith(ClientInit, request.server_start().in_bytes()) &&
            request.server_start().in_bytes() != ClientInit) {
          // If the in_bytes contain a subset of the ClientInit message and we
          // allow processing of an incomplete ClientInit message, then tell the
          // client that we consumed the bytes and are waiting for more.
          response.set_bytes_consumed(request.server_start().in_bytes().size());
        } else {
          EXPECT_EQ(request.server_start().in_bytes(), ClientInit);
          std::string out_frames = absl::StrCat(ServerInit, ServerFinished);
          response.set_out_frames(out_frames);
          response.set_bytes_consumed(out_frames.size());
          sent_server_init_and_server_finished = true;
        }
        bytes_from_client.append(request.server_start().in_bytes());
      } else if (request.has_next()) {
        std::size_t number_handshake_bytes = request.next().in_bytes().size();
        if (absl::EndsWith(request.next().in_bytes(), ApplicationData)) {
          number_handshake_bytes -= ApplicationData.size();
        }
        if (is_assisting_client) {
          bytes_from_server.append(request.next().in_bytes().substr(0, number_handshake_bytes));
        } else {
          bytes_from_client.append(request.next().in_bytes().substr(0, number_handshake_bytes));
        }
        response.set_bytes_consumed(number_handshake_bytes);

        // Consider sending the ServerInit and ServerFinished.
        if (!is_assisting_client && !sent_server_init_and_server_finished &&
            bytes_from_client == ClientInit) {
          sent_server_init_and_server_finished = true;
          response.set_out_frames(absl::StrCat(ServerInit, ServerFinished));
        }

        // Consider sending the ClientFinished.
        if (is_assisting_client && !sent_client_finished &&
            bytes_from_server == absl::StrCat(ServerInit, ServerFinished)) {
          sent_client_finished = true;
          response.set_out_frames(ClientFinished);
        }

        // Check if the handshake is complete and, if so, populate the result.
        if (is_assisting_client) {
          is_handshake_complete = (bytes_from_server == absl::StrCat(ServerInit, ServerFinished));
        } else {
          is_handshake_complete = (bytes_from_client == absl::StrCat(ClientInit, ClientFinished));
        }
        if (is_handshake_complete) {
          populateHandshakeResult(response.mutable_result());
        }
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

class CapturingHandshaker {
public:
  CapturingHandshaker() = default;

  std::string getBytesToSend() { return bytes_to_send_; }
  void setBytesToSend(absl::string_view bytes_to_send) { bytes_to_send_ = bytes_to_send; }

  absl::Status getStatus() { return status_; }
  void setStatus(const absl::Status& status) { status_ = status; }

  std::unique_ptr<AltsHandshakeResult> getAltsHandshakeResult() {
    return std::move(alts_handshake_result_);
  }
  void setAltsHandshakeResult(std::unique_ptr<AltsHandshakeResult> alts_handshake_result) {
    alts_handshake_result_ = std::move(alts_handshake_result);
  }

private:
  std::unique_ptr<AltsHandshakeResult> alts_handshake_result_;
  std::string bytes_to_send_;
  absl::Status status_ = absl::InternalError("Never populated.");
};

void onNextDoneImpl(absl::Status status, void* handshaker, const unsigned char* bytes_to_send,
                    size_t bytes_to_send_size,
                    std::unique_ptr<AltsHandshakeResult> handshake_result) {
  CapturingHandshaker* capturing_handshaker = static_cast<CapturingHandshaker*>(handshaker);
  capturing_handshaker->setStatus(status);
  absl::string_view bytes(reinterpret_cast<const char*>(bytes_to_send), bytes_to_send_size);
  capturing_handshaker->setBytesToSend(bytes);
  capturing_handshaker->setAltsHandshakeResult(std::move(handshake_result));
}

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

// Check that a client-side AltsTsiHandshaker can successfully complete a full
// client-side ALTS handshake.
TEST_P(AltsTsiHandshakerTest, ClientSideFullHandshake) {
  // Setup.
  startFakeHandshakerService();
  auto handshaker = AltsTsiHandshaker::createForClient(getChannel());

  // Get the ClientInit.
  {
    CapturingHandshaker capturing_handshaker;
    EXPECT_OK(handshaker->next(&capturing_handshaker,
                               /*received_bytes=*/nullptr,
                               /*received_bytes_size=*/0, onNextDoneImpl));
    EXPECT_EQ(capturing_handshaker.getBytesToSend(), ClientInit);
    EXPECT_OK(capturing_handshaker.getStatus());
    EXPECT_THAT(capturing_handshaker.getAltsHandshakeResult(), IsNull());
  }

  // Get the ClientFinished and the handshake result.
  {
    std::string handshake_message = absl::StrCat(ServerInit, ServerFinished);
    CapturingHandshaker capturing_handshaker;
    EXPECT_OK(handshaker->next(&capturing_handshaker,
                               reinterpret_cast<const unsigned char*>(handshake_message.c_str()),
                               handshake_message.size(), onNextDoneImpl));
    EXPECT_EQ(capturing_handshaker.getBytesToSend(), ClientFinished);
    EXPECT_OK(capturing_handshaker.getStatus());
    auto handshake_result = capturing_handshaker.getAltsHandshakeResult();
    EXPECT_THAT(handshake_result, NotNull());
    EXPECT_THAT(handshake_result->frame_protector, NotNull());
    EXPECT_EQ(handshake_result->peer_identity, PeerServiceAccount);
    EXPECT_EQ(handshake_result->unused_bytes.size(), 0);
  }

  // Confirm that the handshake cannot continue.
  CapturingHandshaker capturing_handshaker;
  EXPECT_THAT(handshaker->next(&capturing_handshaker, /*received_bytes=*/nullptr,
                               /*received_bytes_size=*/0, onNextDoneImpl),
              StatusCodeIs(absl::StatusCode::kInternal));
}

// Check that several client-side handshaker can successfully complete concurrent, full
// client-side ALTS handshakes over the same channel to the handshaker service.
TEST_P(AltsTsiHandshakerTest, ConcurrentClientSideFullHandshakes) {
  // Setup.
  startFakeHandshakerService();

  std::vector<std::unique_ptr<std::thread>> handshake_threads;
  for (int i = 0; i < 10; ++i) {
    auto handshake_thread = std::make_unique<std::thread>([this]() {
      auto handshaker = AltsTsiHandshaker::createForClient(getChannel());

      // Get the ClientInit.
      {
        CapturingHandshaker capturing_handshaker;
        EXPECT_OK(handshaker->next(&capturing_handshaker,
                                   /*received_bytes=*/nullptr,
                                   /*received_bytes_size=*/0, onNextDoneImpl));
        EXPECT_EQ(capturing_handshaker.getBytesToSend(), ClientInit);
        EXPECT_OK(capturing_handshaker.getStatus());
        EXPECT_THAT(capturing_handshaker.getAltsHandshakeResult(), IsNull());
      }

      // Get the ClientFinished and the handshake result.
      {
        std::string handshake_message = absl::StrCat(ServerInit, ServerFinished);
        CapturingHandshaker capturing_handshaker;
        EXPECT_OK(
            handshaker->next(&capturing_handshaker,
                             reinterpret_cast<const unsigned char*>(handshake_message.c_str()),
                             handshake_message.size(), onNextDoneImpl));
        EXPECT_EQ(capturing_handshaker.getBytesToSend(), ClientFinished);
        EXPECT_OK(capturing_handshaker.getStatus());
        auto handshake_result = capturing_handshaker.getAltsHandshakeResult();
        EXPECT_THAT(handshake_result, NotNull());
        EXPECT_THAT(handshake_result->frame_protector, NotNull());
        EXPECT_EQ(handshake_result->peer_identity, PeerServiceAccount);
        EXPECT_EQ(handshake_result->unused_bytes.size(), 0);
      }
    });
    handshake_threads.push_back(std::move(handshake_thread));
  }
  for (int i = 0; i < 10; ++i) {
    handshake_threads[i]->join();
  }
}

// Check that a client-side AltsTsiHandshaker can successfully complete a full
// client-side ALTS handshake when there are unused bytes after the handshake.
TEST_P(AltsTsiHandshakerTest, ClientSideFullHandshakeWithUnusedBytes) {
  // Setup.
  startFakeHandshakerService();
  auto handshaker = AltsTsiHandshaker::createForClient(getChannel());

  // Get the ClientInit.
  {
    CapturingHandshaker capturing_handshaker;
    EXPECT_OK(handshaker->next(&capturing_handshaker,
                               /*received_bytes=*/nullptr,
                               /*received_bytes_size=*/0, onNextDoneImpl));
    EXPECT_EQ(capturing_handshaker.getBytesToSend(), ClientInit);
    EXPECT_OK(capturing_handshaker.getStatus());
    EXPECT_THAT(capturing_handshaker.getAltsHandshakeResult(), IsNull());
  }

  // Get the ClientFinished and the handshake result.
  {
    std::string handshake_message = absl::StrCat(ServerInit, ServerFinished, ApplicationData);
    CapturingHandshaker capturing_handshaker;
    EXPECT_OK(handshaker->next(&capturing_handshaker,
                               reinterpret_cast<const unsigned char*>(handshake_message.c_str()),
                               handshake_message.size(), onNextDoneImpl));
    EXPECT_EQ(capturing_handshaker.getBytesToSend(), ClientFinished);
    EXPECT_OK(capturing_handshaker.getStatus());
    auto handshake_result = capturing_handshaker.getAltsHandshakeResult();
    EXPECT_THAT(handshake_result, NotNull());
    EXPECT_THAT(handshake_result->frame_protector, NotNull());
    EXPECT_EQ(handshake_result->peer_identity, PeerServiceAccount);
    absl::string_view unused_bytes(
        reinterpret_cast<const char*>(handshake_result->unused_bytes.data()),
        handshake_result->unused_bytes.size());
    EXPECT_EQ(unused_bytes, ApplicationData);
  }

  // Confirm that the handshake cannot continue.
  CapturingHandshaker capturing_handshaker;
  EXPECT_THAT(handshaker->next(&capturing_handshaker, /*received_bytes=*/nullptr,
                               /*received_bytes_size=*/0, onNextDoneImpl),
              StatusCodeIs(absl::StatusCode::kInternal));
}

// Check that a server-side AltsTsiHandshaker can successfully complete a full
// server-side ALTS handshake.
TEST_P(AltsTsiHandshakerTest, ServerSideFullHandshake) {
  // Setup.
  startFakeHandshakerService();
  auto handshaker = AltsTsiHandshaker::createForServer(getChannel());

  // Get the ServerInit and ServerFinished.
  {
    CapturingHandshaker capturing_handshaker;
    EXPECT_OK(handshaker->next(&capturing_handshaker,
                               reinterpret_cast<const unsigned char*>(ClientInit.data()),
                               ClientInit.size(), onNextDoneImpl));
    EXPECT_EQ(capturing_handshaker.getBytesToSend(), absl::StrCat(ServerInit, ServerFinished));
    EXPECT_OK(capturing_handshaker.getStatus());
    EXPECT_THAT(capturing_handshaker.getAltsHandshakeResult(), IsNull());
  }

  // Get the handshake result.
  {
    std::string handshake_message = absl::StrCat(ServerInit, ServerFinished);
    CapturingHandshaker capturing_handshaker;
    EXPECT_OK(handshaker->next(&capturing_handshaker,
                               reinterpret_cast<const unsigned char*>(ClientFinished.data()),
                               ClientFinished.size(), onNextDoneImpl));
    EXPECT_EQ(capturing_handshaker.getBytesToSend(), "");
    EXPECT_OK(capturing_handshaker.getStatus());
    auto handshake_result = capturing_handshaker.getAltsHandshakeResult();
    EXPECT_THAT(handshake_result, NotNull());
    EXPECT_THAT(handshake_result->frame_protector, NotNull());
    EXPECT_EQ(handshake_result->peer_identity, PeerServiceAccount);
    EXPECT_EQ(handshake_result->unused_bytes.size(), 0);
  }

  // Confirm that the handshake cannot continue.
  CapturingHandshaker capturing_handshaker;
  EXPECT_THAT(handshaker->next(&capturing_handshaker, /*received_bytes=*/nullptr,
                               /*received_bytes_size=*/0, onNextDoneImpl),
              StatusCodeIs(absl::StatusCode::kInternal));
}

// Check that several server-side handshaker can successfully complete concurrent, full
// server-side ALTS handshakes over the same channel to the handshaker service.
TEST_P(AltsTsiHandshakerTest, ConcurrentServerSideFullHandshakes) {
  // Setup.
  startFakeHandshakerService();

  std::vector<std::unique_ptr<std::thread>> handshake_threads;
  for (int i = 0; i < 10; ++i) {
    auto handshake_thread = std::make_unique<std::thread>([this]() {
      auto handshaker = AltsTsiHandshaker::createForServer(getChannel());

      // Get the ServerInit and ServerFinished.
      {
        CapturingHandshaker capturing_handshaker;
        EXPECT_OK(handshaker->next(&capturing_handshaker,
                                   reinterpret_cast<const unsigned char*>(ClientInit.data()),
                                   ClientInit.size(), onNextDoneImpl));
        EXPECT_EQ(capturing_handshaker.getBytesToSend(), absl::StrCat(ServerInit, ServerFinished));
        EXPECT_OK(capturing_handshaker.getStatus());
        EXPECT_THAT(capturing_handshaker.getAltsHandshakeResult(), IsNull());
      }

      // Get the handshake result.
      {
        std::string handshake_message = absl::StrCat(ServerInit, ServerFinished);
        CapturingHandshaker capturing_handshaker;
        EXPECT_OK(handshaker->next(&capturing_handshaker,
                                   reinterpret_cast<const unsigned char*>(ClientFinished.data()),
                                   ClientFinished.size(), onNextDoneImpl));
        EXPECT_EQ(capturing_handshaker.getBytesToSend(), "");
        EXPECT_OK(capturing_handshaker.getStatus());
        auto handshake_result = capturing_handshaker.getAltsHandshakeResult();
        EXPECT_THAT(handshake_result, NotNull());
        EXPECT_THAT(handshake_result->frame_protector, NotNull());
        EXPECT_EQ(handshake_result->peer_identity, PeerServiceAccount);
        EXPECT_EQ(handshake_result->unused_bytes.size(), 0);
      }
    });
    handshake_threads.push_back(std::move(handshake_thread));
  }
  for (int i = 0; i < 10; ++i) {
    handshake_threads[i]->join();
  }
}

// Check that a server-side AltsTsiHandshaker can successfully complete a full
// server-side ALTS handshake when there are unused bytes sent after the
// handshake.
TEST_P(AltsTsiHandshakerTest, ServerSideFullHandshakeWithUnusedBytes) {
  // Setup.
  startFakeHandshakerService();
  auto handshaker = AltsTsiHandshaker::createForServer(getChannel());

  // Get the ServerInit and ServerFinished.
  {
    CapturingHandshaker capturing_handshaker;
    EXPECT_OK(handshaker->next(&capturing_handshaker,
                               reinterpret_cast<const unsigned char*>(ClientInit.data()),
                               ClientInit.size(), onNextDoneImpl));
    EXPECT_EQ(capturing_handshaker.getBytesToSend(), absl::StrCat(ServerInit, ServerFinished));
    EXPECT_OK(capturing_handshaker.getStatus());
    EXPECT_THAT(capturing_handshaker.getAltsHandshakeResult(), IsNull());
  }

  // Get the handshake result.
  {
    std::string handshake_message = absl::StrCat(ServerInit, ServerFinished);
    std::string received_bytes = absl::StrCat(ClientFinished, ApplicationData);
    CapturingHandshaker capturing_handshaker;
    EXPECT_OK(handshaker->next(&capturing_handshaker,
                               reinterpret_cast<const unsigned char*>(received_bytes.data()),
                               received_bytes.size(), onNextDoneImpl));
    EXPECT_EQ(capturing_handshaker.getBytesToSend(), "");
    EXPECT_OK(capturing_handshaker.getStatus());
    auto handshake_result = capturing_handshaker.getAltsHandshakeResult();
    EXPECT_THAT(handshake_result, NotNull());
    EXPECT_THAT(handshake_result->frame_protector, NotNull());
    EXPECT_EQ(handshake_result->peer_identity, PeerServiceAccount);
    absl::string_view unused_bytes(
        reinterpret_cast<const char*>(handshake_result->unused_bytes.data()),
        handshake_result->unused_bytes.size());
    EXPECT_EQ(unused_bytes, ApplicationData);
  }

  // Confirm that the handshake cannot continue.
  CapturingHandshaker capturing_handshaker;
  EXPECT_THAT(handshaker->next(&capturing_handshaker, /*received_bytes=*/nullptr,
                               /*received_bytes_size=*/0, onNextDoneImpl),
              StatusCodeIs(absl::StatusCode::kInternal));
}

// Check that a server-side AltsTsiHandshaker can successfully complete a full
// server-side ALTS handshake when the initial bytes from the client are missing.
TEST_P(AltsTsiHandshakerTest, ServerSideFullHandshakeWithMissingInitialInBytes) {
  // Setup.
  startFakeHandshakerService();
  auto handshaker = AltsTsiHandshaker::createForServer(getChannel());

  // Fail to get the ServerInit and ServerFinished because the ClientInit has
  // not arrived yet.
  {
    CapturingHandshaker capturing_handshaker;
    EXPECT_OK(handshaker->next(&capturing_handshaker,
                               /*received_bytes=*/nullptr,
                               /*received_bytes_size=*/0, onNextDoneImpl));
    EXPECT_EQ(capturing_handshaker.getBytesToSend(), "");
    EXPECT_OK(capturing_handshaker.getStatus());
    EXPECT_THAT(capturing_handshaker.getAltsHandshakeResult(), IsNull());
  }

  // Get the ServerInit and ServerFinished now that the ClientInit has arrived.
  {
    CapturingHandshaker capturing_handshaker;
    EXPECT_OK(handshaker->next(&capturing_handshaker,
                               reinterpret_cast<const unsigned char*>(ClientInit.data()),
                               ClientInit.size(), onNextDoneImpl));
    EXPECT_EQ(capturing_handshaker.getBytesToSend(), absl::StrCat(ServerInit, ServerFinished));
    EXPECT_OK(capturing_handshaker.getStatus());
    EXPECT_THAT(capturing_handshaker.getAltsHandshakeResult(), IsNull());
  }

  // Get the handshake result.
  {
    std::string handshake_message = absl::StrCat(ServerInit, ServerFinished);
    CapturingHandshaker capturing_handshaker;
    EXPECT_OK(handshaker->next(&capturing_handshaker,
                               reinterpret_cast<const unsigned char*>(ClientFinished.data()),
                               ClientFinished.size(), onNextDoneImpl));
    EXPECT_EQ(capturing_handshaker.getBytesToSend(), "");
    EXPECT_OK(capturing_handshaker.getStatus());
    auto handshake_result = capturing_handshaker.getAltsHandshakeResult();
    EXPECT_THAT(handshake_result, NotNull());
    EXPECT_THAT(handshake_result->frame_protector, NotNull());
    EXPECT_EQ(handshake_result->peer_identity, PeerServiceAccount);
    EXPECT_EQ(handshake_result->unused_bytes.size(), 0);
  }

  // Confirm that the handshake cannot continue.
  CapturingHandshaker capturing_handshaker;
  EXPECT_THAT(handshaker->next(&capturing_handshaker, /*received_bytes=*/nullptr,
                               /*received_bytes_size=*/0, onNextDoneImpl),
              StatusCodeIs(absl::StatusCode::kInternal));
}

// Check that a server-side AltsTsiHandshaker can successfully complete a full
// server-side ALTS handshake when the ClientInit is split across 2 packets on
// the wire.
TEST_P(AltsTsiHandshakerTest, ServerSideFullHandshakeWithClientInitSplit) {
  // Setup.
  startFakeHandshakerService();
  auto handshaker = AltsTsiHandshaker::createForServer(getChannel());

  // Fail to get the ServerInit and ServerFinished because only part of the
  // ClientInit has arrived.
  {
    absl::string_view client_init_first_half = ClientInit.substr(0, ClientInit.size() / 2);
    CapturingHandshaker capturing_handshaker;
    EXPECT_OK(
        handshaker->next(&capturing_handshaker,
                         reinterpret_cast<const unsigned char*>(client_init_first_half.data()),
                         client_init_first_half.size(), onNextDoneImpl));
    EXPECT_EQ(capturing_handshaker.getBytesToSend(), "");
    EXPECT_OK(capturing_handshaker.getStatus());
    EXPECT_THAT(capturing_handshaker.getAltsHandshakeResult(), IsNull());
  }

  // Get the ServerInit and ServerFinished now that the rest of the ClientInit
  // has arrived.
  {
    absl::string_view client_init_second_half = ClientInit.substr(ClientInit.size() / 2);
    CapturingHandshaker capturing_handshaker;
    EXPECT_OK(
        handshaker->next(&capturing_handshaker,
                         reinterpret_cast<const unsigned char*>(client_init_second_half.data()),
                         client_init_second_half.size(), onNextDoneImpl));
    EXPECT_EQ(capturing_handshaker.getBytesToSend(), absl::StrCat(ServerInit, ServerFinished));
    EXPECT_OK(capturing_handshaker.getStatus());
    EXPECT_THAT(capturing_handshaker.getAltsHandshakeResult(), IsNull());
  }

  // Get the handshake result.
  {
    std::string handshake_message = absl::StrCat(ServerInit, ServerFinished);
    CapturingHandshaker capturing_handshaker;
    EXPECT_OK(handshaker->next(&capturing_handshaker,
                               reinterpret_cast<const unsigned char*>(ClientFinished.data()),
                               ClientFinished.size(), onNextDoneImpl));
    EXPECT_EQ(capturing_handshaker.getBytesToSend(), "");
    EXPECT_OK(capturing_handshaker.getStatus());
    auto handshake_result = capturing_handshaker.getAltsHandshakeResult();
    EXPECT_THAT(handshake_result, NotNull());
    EXPECT_THAT(handshake_result->frame_protector, NotNull());
    EXPECT_EQ(handshake_result->peer_identity, PeerServiceAccount);
    EXPECT_EQ(handshake_result->unused_bytes.size(), 0);
  }

  // Confirm that the handshake cannot continue.
  CapturingHandshaker capturing_handshaker;
  EXPECT_THAT(handshaker->next(&capturing_handshaker, /*received_bytes=*/nullptr,
                               /*received_bytes_size=*/0, onNextDoneImpl),
              StatusCodeIs(absl::StatusCode::kInternal));
}

// Check that AltsTsiHandshaker correctly handles invalid arguments.
TEST_P(AltsTsiHandshakerTest, InvalidArgumentToNext) {
  // Setup.
  startFakeHandshakerService();
  auto handshaker = AltsTsiHandshaker::createForClient(getChannel());

  CapturingHandshaker capturing_handshaker;
  std::string received_bytes;
  EXPECT_THAT(handshaker->next(
                  /*handshaker=*/nullptr,
                  reinterpret_cast<const unsigned char*>(received_bytes.data()),
                  received_bytes.size(), onNextDoneImpl),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(handshaker->next(&capturing_handshaker, /*received_bytes=*/nullptr,
                               /*received_bytes_size=*/1, onNextDoneImpl),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(handshaker->next(&capturing_handshaker,
                               reinterpret_cast<const unsigned char*>(received_bytes.data()),
                               received_bytes.size(), /*on_next_done=*/nullptr),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

// Check that the handshake result is correctly validated.
TEST_P(AltsTsiHandshakerTest, InvalidHandshakeResult) {
  // Setup.
  startFakeHandshakerService();
  auto handshaker = AltsTsiHandshaker::createForClient(getChannel());
  absl::Span<const uint8_t> received_bytes;

  // Fail due to a missing peer identity.
  HandshakerResult handshake_result;
  EXPECT_THAT(handshaker->getHandshakeResult(handshake_result, received_bytes,
                                             /*bytes_consumed=*/0),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  // Fail due to a missing local identity.
  handshake_result.mutable_peer_identity();
  EXPECT_THAT(handshaker->getHandshakeResult(handshake_result, received_bytes,
                                             /*bytes_consumed=*/0),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  // Fail due to a missing peer RPC versions.
  handshake_result.mutable_local_identity();
  EXPECT_THAT(handshaker->getHandshakeResult(handshake_result, received_bytes,
                                             /*bytes_consumed=*/0),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  // Fail due to empty application protocol.
  handshake_result.mutable_peer_rpc_versions();
  EXPECT_THAT(handshaker->getHandshakeResult(handshake_result, received_bytes,
                                             /*bytes_consumed=*/0),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  // Fail due to unsupported record protocol.
  handshake_result.set_application_protocol(ApplicationProtocol);
  handshake_result.set_record_protocol("ALTSRP_GCM_AES128");
  EXPECT_THAT(handshaker->getHandshakeResult(handshake_result, received_bytes,
                                             /*bytes_consumed=*/0),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  // Fail due to short key length.
  handshake_result.set_record_protocol("ALTSRP_GCM_AES128_REKEY");
  handshake_result.set_key_data(std::string(20, 'a'));
  EXPECT_THAT(handshaker->getHandshakeResult(handshake_result, received_bytes,
                                             /*bytes_consumed=*/0),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  // Fail due to consuming more bytes than we received from the peer.
  handshake_result.set_key_data(std::string(44, 'a'));
  EXPECT_THAT(handshaker->getHandshakeResult(handshake_result, received_bytes,
                                             /*bytes_consumed=*/1),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

// Check that the max frame size is correctly computed.
TEST_P(AltsTsiHandshakerTest, ComputeMaxFrameSize) {
  // Max frame size is not set.
  HandshakerResult result;
  EXPECT_EQ(AltsTsiHandshaker::computeMaxFrameSize(result), AltsMinFrameSize);

  // Max frame size is over the allowed limit.
  result.set_max_frame_size(MaxFrameSize + 1);
  EXPECT_EQ(AltsTsiHandshaker::computeMaxFrameSize(result), MaxFrameSize);

  // Max frame size is under the allowed limit.
  result.set_max_frame_size(AltsMinFrameSize - 1);
  EXPECT_EQ(AltsTsiHandshaker::computeMaxFrameSize(result), AltsMinFrameSize);

  // Max frame size is within the allowed limits.
  result.set_max_frame_size(AltsMinFrameSize + 1);
  EXPECT_EQ(AltsTsiHandshaker::computeMaxFrameSize(result), AltsMinFrameSize + 1);
}

} // namespace
} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
