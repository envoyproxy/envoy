#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/network/post_io_action.h"
#include "envoy/network/transport_socket.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/transport_sockets/alts/alts_proxy.h"
#include "source/extensions/transport_sockets/alts/alts_tsi_handshaker.h"
#include "source/extensions/transport_sockets/alts/tsi_handshaker.h"
#include "source/extensions/transport_sockets/alts/tsi_socket.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/network/transport_socket.h"
#include "test/mocks/upstream/cluster_info.h"

#include "absl/strings/match.h"
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

using ::grpc::gcp::HandshakerReq;
using ::grpc::gcp::HandshakerResp;
using ::grpc::gcp::HandshakerResult;
using ::grpc::gcp::HandshakerService;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::Test;
using ::testing::WithArgs;

constexpr absl::string_view kApplicationData = "application_data";
constexpr absl::string_view kClientInit = "CLIENT_INIT";
constexpr absl::string_view kServerInit = "SERVER_INIT";
constexpr absl::string_view kClientFinished = "CLIENT_FINISHED";
constexpr absl::string_view kServerFinished = "SERVER_FINISHED";

constexpr absl::string_view kKeyData = "fake_key_data_needs_to_be_at_least_44_characters_long";
constexpr absl::string_view kLocalServiceAccount = "local_service_account";
constexpr absl::string_view kPeerServiceAccount = "peer_service_account";

void PopulateHandshakeResult(HandshakerResult* result) {
  result->mutable_peer_identity()->set_service_account(kPeerServiceAccount);
  result->mutable_peer_rpc_versions();
  result->mutable_local_identity()->set_service_account(kLocalServiceAccount);
  result->set_application_protocol(kApplicationProtocol);
  result->set_record_protocol(kRecordProtocol);
  result->set_key_data(kKeyData);
}

class FakeHandshakerService final : public HandshakerService::Service {
public:
  explicit FakeHandshakerService(bool allow_unused_bytes = false)
      : allow_unused_bytes_(allow_unused_bytes) {}

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
        if (!is_assisting_client && allow_unused_bytes_) {
          EXPECT_TRUE(absl::StartsWith(request.next().in_bytes(), kClientFinished));
          response.set_bytes_consumed(kClientFinished.size());
        } else {
          std::string expected_in_bytes = is_assisting_client
                                              ? absl::StrCat(kServerInit, kServerFinished)
                                              : std::string(kClientFinished);
          EXPECT_EQ(request.next().in_bytes(), expected_in_bytes);
          response.set_bytes_consumed(expected_in_bytes.size());
        }
        if (is_assisting_client) {
          response.set_out_frames(kClientFinished);
        }
        PopulateHandshakeResult(response.mutable_result());
      } else {
        response.mutable_status()->set_code(
            static_cast<int>(grpc::StatusCode::FAILED_PRECONDITION));
        response.mutable_status()->set_details("Missing body of handshake request.");
      }
      EXPECT_TRUE(stream->Write(response));
    }
    return grpc::Status::OK;
  }

  const bool allow_unused_bytes_;
};

class ErrorHandshakerService final : public HandshakerService::Service {
public:
  explicit ErrorHandshakerService(bool keep_stream_alive) : keep_stream_alive_(keep_stream_alive) {}

  grpc::Status
  DoHandshake(grpc::ServerContext* context,
              grpc::ServerReaderWriter<HandshakerResp, HandshakerReq>* stream) override {
    EXPECT_THAT(context, NotNull());
    HandshakerReq request;
    while (stream->Read(&request)) {
      if (keep_stream_alive_) {
        HandshakerResp response;
        response.mutable_status()->set_code(static_cast<int>(grpc::StatusCode::INTERNAL));
        response.mutable_status()->set_details("Internal error.");
        EXPECT_TRUE(stream->Write(response));
      } else {
        break;
      }
    }
    return grpc::Status(grpc::StatusCode::INTERNAL, "DoHandshake internal error.");
  }

  const bool keep_stream_alive_;
};

class TsiSocketTest : public testing::Test {
protected:
  TsiSocketTest() {
    server_.handshaker_factory_ = [this](Event::Dispatcher& dispatcher,
                                         const Network::Address::InstanceConstSharedPtr&,
                                         const Network::Address::InstanceConstSharedPtr&) {
      auto handshaker = AltsTsiHandshaker::CreateForServer(GetChannel());
      return std::make_unique<TsiHandshaker>(std::move(handshaker), dispatcher);
    };
    client_.handshaker_factory_ = [this](Event::Dispatcher& dispatcher,
                                         const Network::Address::InstanceConstSharedPtr&,
                                         const Network::Address::InstanceConstSharedPtr&) {
      auto handshaker = AltsTsiHandshaker::CreateForClient(GetChannel());
      return std::make_unique<TsiHandshaker>(std::move(handshaker), dispatcher);
    };
  }

  void TearDown() override {
    if (client_.tsi_socket_ != nullptr) {
      client_.tsi_socket_->closeSocket(Network::ConnectionEvent::LocalClose);
    }
    if (server_.tsi_socket_ != nullptr) {
      server_.tsi_socket_->closeSocket(Network::ConnectionEvent::RemoteClose);
    }
    if (handshaker_server_thread_) {
      handshaker_server_->Shutdown(std::chrono::system_clock::now()); // NO_CHECK_FORMAT(real_time)
      handshaker_server_thread_->join();
    }
  }

  void StartFakeHandshakerService(bool allow_unused_bytes = false) {
    handshaker_server_address_ = absl::StrCat("[::1]:", 0);
    absl::Notification notification;
    handshaker_server_thread_ =
        std::make_unique<std::thread>([this, allow_unused_bytes, &notification]() {
          FakeHandshakerService fake_handshaker_service(allow_unused_bytes);
          grpc::ServerBuilder server_builder;
          int listening_port = -1;
          server_builder.AddListeningPort(handshaker_server_address_,
                                          grpc::InsecureServerCredentials(), &listening_port);
          server_builder.RegisterService(&fake_handshaker_service);
          handshaker_server_ = server_builder.BuildAndStart();
          EXPECT_THAT(handshaker_server_, NotNull());
          EXPECT_NE(listening_port, -1);
          handshaker_server_address_ = absl::StrCat("[::1]:", listening_port);
          notification.Notify();
          handshaker_server_->Wait();
        });
    notification.WaitForNotification();
  }

  void StartErrorHandshakerService(bool keep_stream_alive) {
    handshaker_server_address_ = absl::StrCat("[::1]:", 0);
    absl::Notification notification;
    handshaker_server_thread_ =
        std::make_unique<std::thread>([this, keep_stream_alive, &notification]() {
          ErrorHandshakerService error_handshaker_service(keep_stream_alive);
          grpc::ServerBuilder server_builder;
          int listening_port = -1;
          server_builder.AddListeningPort(handshaker_server_address_,
                                          grpc::InsecureServerCredentials(), &listening_port);
          server_builder.RegisterService(&error_handshaker_service);
          handshaker_server_ = server_builder.BuildAndStart();
          EXPECT_THAT(handshaker_server_, NotNull());
          EXPECT_NE(listening_port, -1);
          handshaker_server_address_ = absl::StrCat("[::1]:", listening_port);
          notification.Notify();
          handshaker_server_->Wait();
        });
    notification.WaitForNotification();
  }

  std::shared_ptr<grpc::Channel> GetChannel() {
    return grpc::CreateChannel(handshaker_server_address_, grpc::InsecureChannelCredentials());
  }

  void InitializeSockets(HandshakeValidator server_validator, HandshakeValidator client_validator) {
    server_.raw_socket_ = new Network::MockTransportSocket();
    server_.tsi_socket_ =
        std::make_unique<TsiSocket>(server_.handshaker_factory_, server_validator,
                                    Network::TransportSocketPtr{server_.raw_socket_}, true);

    client_.raw_socket_ = new Network::MockTransportSocket();
    client_.tsi_socket_ =
        std::make_unique<TsiSocket>(client_.handshaker_factory_, client_validator,
                                    Network::TransportSocketPtr{client_.raw_socket_}, false);
    ON_CALL(client_.callbacks_.connection_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
    ON_CALL(server_.callbacks_.connection_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));

    ON_CALL(client_.callbacks_.connection_, id()).WillByDefault(Return(11));
    ON_CALL(server_.callbacks_.connection_, id()).WillByDefault(Return(12));

    ON_CALL(server_.callbacks_, shouldDrainReadBuffer()).WillByDefault(Return(false));

    ON_CALL(*client_.raw_socket_, doWrite(_, _))
        .WillByDefault(Invoke([&](Buffer::Instance& buffer, bool) {
          Network::IoResult result = {Network::PostIoAction::KeepOpen, buffer.length(), false};
          client_to_server_.move(buffer);
          return result;
        }));
    ON_CALL(*server_.raw_socket_, doWrite(_, _))
        .WillByDefault(Invoke([&](Buffer::Instance& buffer, bool) {
          Network::IoResult result = {Network::PostIoAction::KeepOpen, buffer.length(), false};
          server_to_client_.move(buffer);
          return result;
        }));

    ON_CALL(*client_.raw_socket_, doRead(_)).WillByDefault(Invoke([&](Buffer::Instance& buffer) {
      Network::IoResult result = {Network::PostIoAction::KeepOpen, server_to_client_.length(),
                                  false};
      buffer.move(server_to_client_);
      return result;
    }));
    ON_CALL(*server_.raw_socket_, doRead(_)).WillByDefault(Invoke([&](Buffer::Instance& buffer) {
      Network::IoResult result = {Network::PostIoAction::KeepOpen, client_to_server_.length(),
                                  false};
      buffer.move(client_to_server_);
      return result;
    }));

    EXPECT_CALL(*client_.raw_socket_, setTransportSocketCallbacks(_));
    client_.tsi_socket_->setTransportSocketCallbacks(client_.callbacks_);

    EXPECT_CALL(*server_.raw_socket_, setTransportSocketCallbacks(_));
    server_.tsi_socket_->setTransportSocketCallbacks(server_.callbacks_);

    ON_CALL(dispatcher_, post(_)).WillByDefault(WithArgs<0>(Invoke([](Event::PostCb callback) {
      callback();
    })));
  }

  void ExpectIoResult(Network::IoResult expected, Network::IoResult actual,
                      absl::string_view debug_string) {
    EXPECT_EQ(expected.action_, actual.action_) << debug_string;
    EXPECT_EQ(expected.bytes_processed_, actual.bytes_processed_) << debug_string;
    EXPECT_EQ(expected.end_stream_read_, actual.end_stream_read_) << debug_string;
  }

  void DoHandshakeAndExpectSuccess() {
    // On the client side, get the ClientInit and write it to the server.
    EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
    client_.tsi_socket_->onConnected();
    ExpectIoResult(client_.tsi_socket_->doWrite(client_.write_buffer_, /*end_stream=*/false),
                   {Envoy::Network::PostIoAction::KeepOpen, 0UL, false},
                   "While writing ClientInit.");
    EXPECT_EQ(client_to_server_.toString(), kClientInit);

    // On the server side, read the ClientInit and write the ServerInit and the
    // ServerFinished to the client.
    EXPECT_CALL(*server_.raw_socket_, doRead(_));
    EXPECT_CALL(*server_.raw_socket_, doWrite(_, false));
    ExpectIoResult(server_.tsi_socket_->doRead(server_.read_buffer_),
                   {Envoy::Network::PostIoAction::KeepOpen, 0UL, false},
                   "While reading ClientInit.");
    EXPECT_EQ(server_.read_buffer_.length(), 0L);
    EXPECT_EQ(server_to_client_.toString(), absl::StrCat(kServerInit, kServerFinished));

    // On the client side, read the ServerInit and the ServerFinished, and write
    // the ClientFinished to the server.
    EXPECT_CALL(*client_.raw_socket_, doRead(_)).Times(2);
    EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
    EXPECT_CALL(client_.callbacks_, raiseEvent(Envoy::Network::ConnectionEvent::Connected));
    ExpectIoResult({Envoy::Network::PostIoAction::KeepOpen, 0UL, false},
                   client_.tsi_socket_->doRead(client_.read_buffer_),
                   "While reading ServerInit and ServerFinished.");
    EXPECT_EQ(client_.read_buffer_.length(), 0L);
    EXPECT_EQ(client_to_server_.toString(), kClientFinished);

    // On the server side, read the ClientFinished.
    EXPECT_CALL(*server_.raw_socket_, doRead(_)).Times(2);
    EXPECT_CALL(server_.callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
    ExpectIoResult(server_.tsi_socket_->doRead(server_.read_buffer_),
                   {Envoy::Network::PostIoAction::KeepOpen, 0UL, false},
                   "While reading ClientFinished.");
    EXPECT_EQ(server_.read_buffer_.toString(), "");
  }

  void ExpectTransferDataFromClientToServer(absl::string_view data) {
    EXPECT_EQ(server_.read_buffer_.length(), 0);
    EXPECT_EQ(client_.read_buffer_.length(), 0);
    EXPECT_EQ(client_.tsi_socket_->protocol(), "");
    client_.write_buffer_.add(data);
    EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
    ExpectIoResult({Envoy::Network::PostIoAction::KeepOpen, data.size(), false},
                   client_.tsi_socket_->doWrite(client_.write_buffer_, false),
                   "While the client is writing application data.");
    EXPECT_CALL(*server_.raw_socket_, doRead(_))
        .WillOnce(Invoke([&](Envoy::Buffer::Instance& buffer) {
          Envoy::Network::IoResult result = {Envoy::Network::PostIoAction::KeepOpen,
                                             client_to_server_.length(), true};
          buffer.move(client_to_server_);
          return result;
        }));
    ExpectIoResult({Envoy::Network::PostIoAction::KeepOpen, data.size(), true},
                   server_.tsi_socket_->doRead(server_.read_buffer_),
                   "While the server is reading application data.");
    EXPECT_EQ(server_.read_buffer_.toString(), data);
  }

  struct SocketForTest {
    HandshakerFactory handshaker_factory_;
    std::unique_ptr<TsiSocket> tsi_socket_;
    Network::MockTransportSocket* raw_socket_{};
    NiceMock<Network::MockTransportSocketCallbacks> callbacks_;
    Buffer::OwnedImpl read_buffer_;
    Buffer::OwnedImpl write_buffer_;
  };

  SocketForTest client_;
  SocketForTest server_;

  Buffer::OwnedImpl client_to_server_;
  Buffer::OwnedImpl server_to_client_;

  NiceMock<Event::MockDispatcher> dispatcher_;

  std::string handshaker_server_address_;
  std::unique_ptr<grpc::Server> handshaker_server_;
  std::unique_ptr<std::thread> handshaker_server_thread_;
};

TEST_F(TsiSocketTest, DoesNotHaveSsl) {
  InitializeSockets(/*server_validator=*/nullptr, /*client_validator=*/nullptr);
  EXPECT_EQ(client_.tsi_socket_->ssl(), nullptr);
  EXPECT_FALSE(client_.tsi_socket_->canFlushClose());
  EXPECT_EQ(client_.tsi_socket_->ssl(), nullptr);
}

TEST_F(TsiSocketTest, EmptyFailureReason) {
  InitializeSockets(/*server_validator=*/nullptr, /*client_validator=*/nullptr);
  EXPECT_EQ(client_.tsi_socket_->failureReason(), "");
}

TEST_F(TsiSocketTest, UpstreamHandshakeFactoryFailure) {
  auto raw_socket = new Network::MockTransportSocket();
  auto tsi_socket = std::make_unique<TsiSocket>(
      [](Event::Dispatcher&, const Network::Address::InstanceConstSharedPtr&,
         const Network::Address::InstanceConstSharedPtr&) { return nullptr; },
      nullptr, Network::TransportSocketPtr{raw_socket}, false);
  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  EXPECT_CALL(*raw_socket, setTransportSocketCallbacks(_));
  tsi_socket->setTransportSocketCallbacks(callbacks);
  tsi_socket->onConnected();
  // TODO(matthewstevenson88): Investigate whether this should this be close instead of
  // keep open.
  ExpectIoResult({Envoy::Network::PostIoAction::KeepOpen, 0UL, false},
                 tsi_socket->doWrite(client_.write_buffer_, /*end_stream=*/false),
                 "While writing ClientInit.");
}

TEST_F(TsiSocketTest, DownstreamHandshakeFactoryFailure) {
  auto raw_socket = new Network::MockTransportSocket();
  auto tsi_socket = std::make_unique<TsiSocket>(
      [](Event::Dispatcher&, const Network::Address::InstanceConstSharedPtr&,
         const Network::Address::InstanceConstSharedPtr&) { return nullptr; },
      nullptr, Network::TransportSocketPtr{raw_socket}, false);
  NiceMock<Network::MockTransportSocketCallbacks> callbacks;
  EXPECT_CALL(*raw_socket, setTransportSocketCallbacks(_));
  tsi_socket->setTransportSocketCallbacks(callbacks);
  tsi_socket->onConnected();
  // TODO(matthewstevenson88): Investigate whether this should this be close instead of
  // keep open.
  ExpectIoResult({Envoy::Network::PostIoAction::KeepOpen, 0UL, false},
                 tsi_socket->doWrite(client_.write_buffer_, /*end_stream=*/false),
                 "While writing ClientInit.");
}

TEST_F(TsiSocketTest, HandshakeSuccessAndTransferData) {
  StartFakeHandshakerService();
  InitializeSockets(/*server_validator=*/nullptr, /*client_validator=*/nullptr);
  DoHandshakeAndExpectSuccess();
  ExpectTransferDataFromClientToServer(kApplicationData);
}

TEST_F(TsiSocketTest, HandshakeSuccessAndTransferDataWithValidation) {
  StartFakeHandshakerService();
  auto validator = [](TsiInfo&, std::string&) { return true; };
  InitializeSockets(validator, validator);
  DoHandshakeAndExpectSuccess();
  ExpectTransferDataFromClientToServer(kApplicationData);
}

TEST_F(TsiSocketTest, HandshakeSuccessAndFailToUnprotect) {
  StartFakeHandshakerService();
  InitializeSockets(/*server_validator=*/nullptr, /*client_validator=*/nullptr);
  DoHandshakeAndExpectSuccess();

  EXPECT_EQ(server_.read_buffer_.length(), 0);
  EXPECT_EQ(client_.read_buffer_.length(), 0);
  EXPECT_EQ(client_.tsi_socket_->protocol(), "");
  client_.write_buffer_.add(kApplicationData);
  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
  ExpectIoResult({Envoy::Network::PostIoAction::KeepOpen, kApplicationData.size(), false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, false),
                 "While the client is writing application data.");
  EXPECT_CALL(*server_.raw_socket_, doRead(_))
      .WillOnce(Invoke([&](Envoy::Buffer::Instance& buffer) {
        Envoy::Network::IoResult result = {Envoy::Network::PostIoAction::KeepOpen,
                                           client_to_server_.length(), true};
        buffer.move(client_to_server_);
        return result;
      }));
  client_to_server_.drain(client_to_server_.length());
  client_to_server_.add("not-an-alts-frame");
  ExpectIoResult({Envoy::Network::PostIoAction::Close, 0L, true},
                 server_.tsi_socket_->doRead(server_.read_buffer_),
                 "While the server is reading application data.");
  EXPECT_EQ(server_.read_buffer_.toString(), "");
}

// TODO(matthewstevenson88): Add test case with unused data.

TEST_F(TsiSocketTest, HandshakeErrorButStreamIsKeptAlive) {
  StartErrorHandshakerService(/*keep_stream_alive=*/true);
  InitializeSockets(/*server_validator=*/nullptr, /*client_validator=*/nullptr);
  client_.tsi_socket_->onConnected();
  // TODO(matthewstevenson88): Should this be close instead of keep open.
  ExpectIoResult({Envoy::Network::PostIoAction::KeepOpen, 0UL, false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, /*end_stream=*/false),
                 "While writing ClientInit.");
  EXPECT_EQ(client_to_server_.toString(), "");
}

TEST_F(TsiSocketTest, HandshakeErrorButStreamIsNotKeptAlive) {
  StartErrorHandshakerService(/*keep_stream_alive=*/false);
  InitializeSockets(/*server_validator=*/nullptr, /*client_validator=*/nullptr);
  client_.tsi_socket_->onConnected();
  // TODO(matthewstevenson88): Should this be close instead of keep open.
  ExpectIoResult({Envoy::Network::PostIoAction::KeepOpen, 0UL, false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, /*end_stream=*/false),
                 "While writing ClientInit.");
  EXPECT_EQ(client_to_server_.toString(), "");
}

} // namespace
} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
