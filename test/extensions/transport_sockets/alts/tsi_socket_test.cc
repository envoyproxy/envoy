#include "common/buffer/buffer_impl.h"

#include "extensions/transport_sockets/alts/tsi_socket.h"

#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/tsi/fake_transport_security.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;

class TsiSocketTest : public testing::Test {
protected:
  TsiSocketTest() {}

  void TearDown() override {
    client_.tsi_socket_->closeSocket(Network::ConnectionEvent::LocalClose);
    server_.tsi_socket_->closeSocket(Network::ConnectionEvent::RemoteClose);
  }

  void initialize(HandshakeValidator server_validator, HandshakeValidator client_validator) {
    auto server_handshaker_factory = [](Event::Dispatcher& dispatcher) {
      CHandshakerPtr handshaker{tsi_create_fake_handshaker(/*is_client=*/0)};

      return std::make_unique<TsiHandshaker>(std::move(handshaker), dispatcher);
    };

    server_.raw_socket_ = new NiceMock<Network::MockTransportSocket>();

    server_.tsi_socket_ =
        std::make_unique<TsiSocket>(server_handshaker_factory, server_validator,
                                    Network::TransportSocketPtr{server_.raw_socket_});

    auto client_handshaker_factory = [](Event::Dispatcher& dispatcher) {
      CHandshakerPtr handshaker{tsi_create_fake_handshaker(/*is_client=*/1)};

      return std::make_unique<TsiHandshaker>(std::move(handshaker), dispatcher);
    };

    client_.raw_socket_ = new NiceMock<Network::MockTransportSocket>();

    client_.tsi_socket_ =
        std::make_unique<TsiSocket>(client_handshaker_factory, client_validator,
                                    Network::TransportSocketPtr{client_.raw_socket_});

    ON_CALL(client_.callbacks_.connection_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
    ON_CALL(server_.callbacks_.connection_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));

    ON_CALL(client_.callbacks_.connection_, id()).WillByDefault(Return(11));
    ON_CALL(server_.callbacks_.connection_, id()).WillByDefault(Return(12));

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

    client_.tsi_socket_->setTransportSocketCallbacks(client_.callbacks_);
    client_.tsi_socket_->onConnected();

    server_.tsi_socket_->setTransportSocketCallbacks(server_.callbacks_);
    server_.tsi_socket_->onConnected();
  }

  void expectIoResult(Network::IoResult expected, Network::IoResult actual) {
    EXPECT_EQ(expected.action_, actual.action_);
    EXPECT_EQ(expected.bytes_processed_, actual.bytes_processed_);
    EXPECT_EQ(expected.end_stream_read_, actual.end_stream_read_);
  }

  std::string makeFakeTsiFrame(const std::string& payload) {
    uint32_t length = static_cast<uint32_t>(payload.length()) + 4;
    std::string frame;
    frame.reserve(length);
    frame.push_back(static_cast<uint8_t>(length));
    length >>= 8;
    frame.push_back(static_cast<uint8_t>(length));
    length >>= 8;
    frame.push_back(static_cast<uint8_t>(length));
    length >>= 8;
    frame.push_back(static_cast<uint8_t>(length));

    frame.append(payload);
    return frame;
  }

  void doFakeInitHandshake() {
    EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
    expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                   client_.tsi_socket_->doWrite(client_.write_buffer_, false));
    EXPECT_EQ(makeFakeTsiFrame("CLIENT_INIT"), client_to_server_.toString());

    EXPECT_CALL(*server_.raw_socket_, doRead(_));
    EXPECT_CALL(*server_.raw_socket_, doWrite(_, false));
    expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                   server_.tsi_socket_->doRead(server_.read_buffer_));
    EXPECT_EQ(makeFakeTsiFrame("SERVER_INIT"), server_to_client_.toString());
    EXPECT_EQ(0L, server_.read_buffer_.length());
  }

  void doHandshakeAndExpectSuccess() {
    doFakeInitHandshake();

    EXPECT_CALL(*client_.raw_socket_, doRead(_));
    EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
    expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                   client_.tsi_socket_->doRead(client_.read_buffer_));
    EXPECT_EQ(makeFakeTsiFrame("CLIENT_FINISHED"), client_to_server_.toString());
    EXPECT_EQ(0L, client_.read_buffer_.length());

    EXPECT_CALL(*server_.raw_socket_, doRead(_));
    EXPECT_CALL(*server_.raw_socket_, doWrite(_, false));
    EXPECT_CALL(server_.callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
    expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                   server_.tsi_socket_->doRead(server_.read_buffer_));
    EXPECT_EQ(makeFakeTsiFrame("SERVER_FINISHED"), server_to_client_.toString());

    EXPECT_CALL(*client_.raw_socket_, doRead(_));
    EXPECT_CALL(client_.callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
    expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                   client_.tsi_socket_->doRead(client_.read_buffer_));
  }

  struct SocketForTest {
    std::unique_ptr<TsiSocket> tsi_socket_;
    NiceMock<Network::MockTransportSocket>* raw_socket_{};
    NiceMock<Network::MockTransportSocketCallbacks> callbacks_;
    Buffer::OwnedImpl read_buffer_;
    Buffer::OwnedImpl write_buffer_;
  };

  SocketForTest client_;
  SocketForTest server_;

  Buffer::OwnedImpl client_to_server_;
  Buffer::OwnedImpl server_to_client_;

  NiceMock<Event::MockDispatcher> dispatcher_;
};

TEST_F(TsiSocketTest, DoesNotHaveSsl) {
  initialize(nullptr, nullptr);
  EXPECT_EQ(nullptr, client_.tsi_socket_->ssl());

  const auto& socket_ = *client_.tsi_socket_;
  EXPECT_EQ(nullptr, socket_.ssl());
}

TEST_F(TsiSocketTest, ProxyCallbacks) {
  initialize(nullptr, nullptr);

  EXPECT_CALL(client_.callbacks_, fd()).WillOnce(Return(111));
  EXPECT_EQ(111, client_.raw_socket_->callbacks_->fd());

  EXPECT_EQ(&client_.callbacks_.connection_, &client_.raw_socket_->callbacks_->connection());

  EXPECT_FALSE(client_.raw_socket_->callbacks_->shouldDrainReadBuffer());
}

TEST_F(TsiSocketTest, HandshakeWithoutValidationAndTransferData) {
  initialize(nullptr, nullptr);

  client_.write_buffer_.add("hello from client");

  doHandshakeAndExpectSuccess();
  EXPECT_EQ(0L, server_.read_buffer_.length());
  EXPECT_EQ(0L, client_.read_buffer_.length());

  EXPECT_EQ("", client_.tsi_socket_->protocol());

  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
  expectIoResult({Network::PostIoAction::KeepOpen, 21UL, false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, false));
  EXPECT_EQ(makeFakeTsiFrame("hello from client"), client_to_server_.toString());

  EXPECT_CALL(*server_.raw_socket_, doRead(_));
  expectIoResult({Network::PostIoAction::KeepOpen, 21UL, false},
                 server_.tsi_socket_->doRead(server_.read_buffer_));
  EXPECT_EQ("hello from client", server_.read_buffer_.toString());
}

TEST_F(TsiSocketTest, HandshakeWithSucessfulValidationAndTransferData) {
  auto validator = [](const tsi_peer&, std::string&) { return true; };
  initialize(validator, validator);

  client_.write_buffer_.add("hello from client");

  doHandshakeAndExpectSuccess();
  EXPECT_EQ(0L, server_.read_buffer_.length());
  EXPECT_EQ(0L, client_.read_buffer_.length());

  EXPECT_EQ("", client_.tsi_socket_->protocol());

  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
  expectIoResult({Network::PostIoAction::KeepOpen, 21UL, false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, false));
  EXPECT_EQ(makeFakeTsiFrame("hello from client"), client_to_server_.toString());

  EXPECT_CALL(*server_.raw_socket_, doRead(_));
  expectIoResult({Network::PostIoAction::KeepOpen, 21UL, false},
                 server_.tsi_socket_->doRead(server_.read_buffer_));
  EXPECT_EQ("hello from client", server_.read_buffer_.toString());
}

TEST_F(TsiSocketTest, HandshakeValidationFail) {
  auto validator = [](const tsi_peer&, std::string&) { return false; };
  initialize(validator, validator);

  client_.write_buffer_.add("hello from client");

  doFakeInitHandshake();

  EXPECT_CALL(*client_.raw_socket_, doRead(_));
  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
  expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                 client_.tsi_socket_->doRead(client_.read_buffer_));
  EXPECT_EQ(makeFakeTsiFrame("CLIENT_FINISHED"), client_to_server_.toString());
  EXPECT_EQ(0L, client_.read_buffer_.length());

  EXPECT_CALL(*server_.raw_socket_, doRead(_));
  EXPECT_CALL(server_.callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  // doRead won't immediately fail, but it will result connection close.
  expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                 server_.tsi_socket_->doRead(server_.read_buffer_));
  EXPECT_EQ(0, server_to_client_.length());
}

class TsiSocketFactoryTest : public testing::Test {
protected:
  void SetUp() override {
    auto handshaker_factory = [](Event::Dispatcher& dispatcher) {
      CHandshakerPtr handshaker{tsi_create_fake_handshaker(/*is_client=*/0)};

      return std::make_unique<TsiHandshaker>(std::move(handshaker), dispatcher);
    };

    socket_factory_ = std::make_unique<TsiSocketFactory>(handshaker_factory, nullptr);
  }
  Network::TransportSocketFactoryPtr socket_factory_;
};

TEST_F(TsiSocketFactoryTest, CreateTransportSocket) {
  EXPECT_NE(nullptr, socket_factory_->createTransportSocket());
}

TEST_F(TsiSocketFactoryTest, ImplementsSecureTransport) {
  EXPECT_TRUE(socket_factory_->implementsSecureTransport());
}

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy