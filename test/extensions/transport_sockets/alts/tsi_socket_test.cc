

#include "source/extensions/transport_sockets/alts/tsi_socket.h"

#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/tsi/fake_transport_security.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {
namespace {

using testing::InSequence;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

static const std::string ClientToServerData = "hello from client";
static const std::string ClientToServerDataFirstHalf = "hello fro";
static const std::string ClientToServerDataSecondHalf = "m client";
static const std::string ServerToClientData = "hello from server";
static const uint32_t LargeFrameSize = 100;
static const uint32_t SmallFrameSize = 13;

class TsiSocketTest : public testing::Test {
protected:
  TsiSocketTest() {
    server_.handshaker_factory_ = [](Event::Dispatcher& dispatcher,
                                     const Network::Address::InstanceConstSharedPtr&,
                                     const Network::Address::InstanceConstSharedPtr&) {
      CHandshakerPtr handshaker{tsi_create_fake_handshaker(/*is_client=*/0)};

      return std::make_unique<TsiHandshaker>(std::move(handshaker), dispatcher);
    };

    client_.handshaker_factory_ = [](Event::Dispatcher& dispatcher,
                                     const Network::Address::InstanceConstSharedPtr&,
                                     const Network::Address::InstanceConstSharedPtr&) {
      CHandshakerPtr handshaker{tsi_create_fake_handshaker(/*is_client=*/1)};

      return std::make_unique<TsiHandshaker>(std::move(handshaker), dispatcher);
    };
  }

  void TearDown() override {
    client_.tsi_socket_->closeSocket(Network::ConnectionEvent::LocalClose);
    server_.tsi_socket_->closeSocket(Network::ConnectionEvent::RemoteClose);
  }

  void initialize(HandshakeValidator server_validator, HandshakeValidator client_validator) {
    server_.raw_socket_ = new Network::MockTransportSocket();

    server_.tsi_socket_ =
        std::make_unique<TsiSocket>(server_.handshaker_factory_, server_validator,
                                    Network::TransportSocketPtr{server_.raw_socket_});

    client_.raw_socket_ = new Network::MockTransportSocket();

    client_.tsi_socket_ =
        std::make_unique<TsiSocket>(client_.handshaker_factory_, client_validator,
                                    Network::TransportSocketPtr{client_.raw_socket_});
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

    server_.tsi_socket_->setFrameOverheadSize(4);
    client_.tsi_socket_->setFrameOverheadSize(4);
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

  std::string makeInvalidTsiFrame() {
    // For fake frame protector, minimum frame size is 4 bytes.
    uint32_t length = 3;
    std::string frame;
    frame.reserve(4);
    frame.push_back(static_cast<uint8_t>(length));
    length >>= 8;
    frame.push_back(static_cast<uint8_t>(length));
    length >>= 8;
    frame.push_back(static_cast<uint8_t>(length));
    length >>= 8;
    frame.push_back(static_cast<uint8_t>(length));

    return frame;
  }

  void doFakeInitHandshake() {
    EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
    client_.tsi_socket_->onConnected();
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
    EXPECT_CALL(*server_.raw_socket_, doRead(_));
    expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                   server_.tsi_socket_->doRead(server_.read_buffer_));
    EXPECT_EQ(makeFakeTsiFrame("SERVER_FINISHED"), server_to_client_.toString());

    EXPECT_CALL(*client_.raw_socket_, doRead(_));
    EXPECT_CALL(client_.callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
    EXPECT_CALL(*client_.raw_socket_, doRead(_));
    expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                   client_.tsi_socket_->doRead(client_.read_buffer_));
  }

  void expectTransferDataFromClientToServer(const std::string& data) {
    EXPECT_EQ(0L, server_.read_buffer_.length());
    EXPECT_EQ(0L, client_.read_buffer_.length());

    EXPECT_EQ("", client_.tsi_socket_->protocol());

    client_.tsi_socket_->setActualFrameSizeToUse(LargeFrameSize);

    EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
    expectIoResult({Network::PostIoAction::KeepOpen, 17UL, false},
                   client_.tsi_socket_->doWrite(client_.write_buffer_, false));
    EXPECT_EQ(makeFakeTsiFrame(data), client_to_server_.toString());
    EXPECT_CALL(*server_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
      Network::IoResult result = {Network::PostIoAction::KeepOpen, client_to_server_.length(),
                                  true};
      buffer.move(client_to_server_);
      return result;
    }));
    expectIoResult({Network::PostIoAction::KeepOpen, 17UL, true},
                   server_.tsi_socket_->doRead(server_.read_buffer_));
    EXPECT_EQ(data, server_.read_buffer_.toString());
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
};

TEST_F(TsiSocketTest, DoesNotHaveSsl) {
  initialize(nullptr, nullptr);
  EXPECT_EQ(nullptr, client_.tsi_socket_->ssl());
  EXPECT_FALSE(client_.tsi_socket_->canFlushClose());

  const auto& socket_ = *client_.tsi_socket_;
  EXPECT_EQ(nullptr, socket_.ssl());
}

TEST_F(TsiSocketTest, HandshakeWithoutValidationAndTransferData) {
  // pass a nullptr validator to skip validation.
  initialize(nullptr, nullptr);

  InSequence s;

  client_.write_buffer_.add(ClientToServerData);

  doHandshakeAndExpectSuccess();
  expectTransferDataFromClientToServer(ClientToServerData);
}

TEST_F(TsiSocketTest, HandshakeWithSucessfulValidationAndTransferData) {
  auto validator = [](const tsi_peer&, TsiInfo&, std::string&) { return true; };
  initialize(validator, validator);

  InSequence s;

  client_.write_buffer_.add(ClientToServerData);

  doHandshakeAndExpectSuccess();
  expectTransferDataFromClientToServer(ClientToServerData);
}

TEST_F(TsiSocketTest, HandshakeWithSucessfulValidationAndTransferInvalidData) {
  auto validator = [](const tsi_peer&, TsiInfo&, std::string&) { return true; };
  initialize(validator, validator);

  InSequence s;

  doHandshakeAndExpectSuccess();
  client_to_server_.add(makeInvalidTsiFrame());

  EXPECT_CALL(*server_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    Network::IoResult result = {Network::PostIoAction::KeepOpen, 4UL, true};
    buffer.move(client_to_server_);
    return result;
  }));
  expectIoResult({Network::PostIoAction::Close, 0UL, true},
                 server_.tsi_socket_->doRead(server_.read_buffer_));
}

TEST_F(TsiSocketTest, HandshakeValidationFail) {
  auto validator = [](const tsi_peer&, TsiInfo&, std::string&) { return false; };
  initialize(validator, validator);

  InSequence s;

  client_.write_buffer_.add(ClientToServerData);

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

TEST_F(TsiSocketTest, HandshakerCreationFail) {
  client_.handshaker_factory_ =
      [](Event::Dispatcher&, const Network::Address::InstanceConstSharedPtr&,
         const Network::Address::InstanceConstSharedPtr&) { return nullptr; };
  auto validator = [](const tsi_peer&, TsiInfo&, std::string&) { return true; };
  initialize(validator, validator);

  InSequence s;

  EXPECT_CALL(*client_.raw_socket_, doWrite(_, _)).Times(0);
  EXPECT_CALL(client_.callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  client_.tsi_socket_->onConnected();
  expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, false));
  EXPECT_EQ("", client_to_server_.toString());

  EXPECT_CALL(*server_.raw_socket_, doRead(_));
  EXPECT_CALL(*server_.raw_socket_, doWrite(_, _)).Times(0);
  expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                 server_.tsi_socket_->doRead(server_.read_buffer_));
  EXPECT_EQ("", server_to_client_.toString());
}

TEST_F(TsiSocketTest, HandshakeWithUnusedData) {
  initialize(nullptr, nullptr);

  InSequence s;

  doFakeInitHandshake();
  EXPECT_CALL(*client_.raw_socket_, doRead(_));
  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
  expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                 client_.tsi_socket_->doRead(client_.read_buffer_));
  EXPECT_EQ(makeFakeTsiFrame("CLIENT_FINISHED"), client_to_server_.toString());
  EXPECT_EQ(0L, client_.read_buffer_.length());

  // Inject unused data
  client_to_server_.add(makeFakeTsiFrame(ClientToServerData));

  EXPECT_CALL(*server_.raw_socket_, doRead(_));
  EXPECT_CALL(*server_.raw_socket_, doWrite(_, false));
  EXPECT_CALL(server_.callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
  EXPECT_CALL(*server_.raw_socket_, doRead(_));
  expectIoResult({Network::PostIoAction::KeepOpen, 17UL, false},
                 server_.tsi_socket_->doRead(server_.read_buffer_));
  EXPECT_EQ(makeFakeTsiFrame("SERVER_FINISHED"), server_to_client_.toString());
  EXPECT_EQ(ClientToServerData, server_.read_buffer_.toString());

  EXPECT_CALL(*client_.raw_socket_, doRead(_));
  EXPECT_CALL(client_.callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
  EXPECT_CALL(*client_.raw_socket_, doRead(_));
  expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                 client_.tsi_socket_->doRead(client_.read_buffer_));
}

TEST_F(TsiSocketTest, HandshakeWithUnusedDataAndEndOfStream) {
  initialize(nullptr, nullptr);

  InSequence s;

  doFakeInitHandshake();
  EXPECT_CALL(*client_.raw_socket_, doRead(_));
  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
  expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                 client_.tsi_socket_->doRead(client_.read_buffer_));
  EXPECT_EQ(makeFakeTsiFrame("CLIENT_FINISHED"), client_to_server_.toString());
  EXPECT_EQ(0L, client_.read_buffer_.length());

  // Inject unused data
  client_to_server_.add(makeFakeTsiFrame(ClientToServerData));

  EXPECT_CALL(*server_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    Network::IoResult result = {Network::PostIoAction::KeepOpen, client_to_server_.length(), true};
    buffer.move(client_to_server_);
    return result;
  }));
  EXPECT_CALL(*server_.raw_socket_, doWrite(_, false));
  EXPECT_CALL(server_.callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
  expectIoResult({Network::PostIoAction::KeepOpen, 17UL, true},
                 server_.tsi_socket_->doRead(server_.read_buffer_));
  EXPECT_EQ(makeFakeTsiFrame("SERVER_FINISHED"), server_to_client_.toString());
  EXPECT_EQ(ClientToServerData, server_.read_buffer_.toString());

  EXPECT_CALL(*client_.raw_socket_, doRead(_));
  EXPECT_CALL(client_.callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
  EXPECT_CALL(*client_.raw_socket_, doRead(_));
  expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                 client_.tsi_socket_->doRead(client_.read_buffer_));
}

TEST_F(TsiSocketTest, HandshakeWithImmediateReadError) {
  initialize(nullptr, nullptr);

  InSequence s;

  EXPECT_CALL(*client_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    Network::IoResult result = {Network::PostIoAction::Close, server_to_client_.length(), false};
    buffer.move(server_to_client_);
    return result;
  }));
  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false)).Times(0);
  expectIoResult({Network::PostIoAction::Close, 0UL, false},
                 client_.tsi_socket_->doRead(client_.read_buffer_));
  EXPECT_EQ("", client_to_server_.toString());
  EXPECT_EQ(0L, client_.read_buffer_.length());
}

TEST_F(TsiSocketTest, HandshakeWithReadError) {
  initialize(nullptr, nullptr);

  InSequence s;

  doFakeInitHandshake();

  EXPECT_CALL(*client_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    Network::IoResult result = {Network::PostIoAction::Close, server_to_client_.length(), false};
    buffer.move(server_to_client_);
    return result;
  }));
  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false)).Times(0);
  EXPECT_CALL(client_.callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                 client_.tsi_socket_->doRead(client_.read_buffer_));
  EXPECT_EQ("", client_to_server_.toString());
  EXPECT_EQ(0L, client_.read_buffer_.length());
}

TEST_F(TsiSocketTest, HandshakeWithInternalError) {
  auto raw_handshaker = tsi_create_fake_handshaker(/* is_client= */ 1);
  const tsi_handshaker_vtable* vtable = raw_handshaker->vtable;
  tsi_handshaker_vtable mock_vtable = *vtable;
  mock_vtable.next = [](tsi_handshaker*, const unsigned char*, size_t, const unsigned char**,
                        size_t*, tsi_handshaker_result**, tsi_handshaker_on_next_done_cb,
                        void*) { return TSI_INTERNAL_ERROR; };
  raw_handshaker->vtable = &mock_vtable;

  client_.handshaker_factory_ = [&](Event::Dispatcher& dispatcher,
                                    const Network::Address::InstanceConstSharedPtr&,
                                    const Network::Address::InstanceConstSharedPtr&) {
    CHandshakerPtr handshaker{raw_handshaker};

    return std::make_unique<TsiHandshaker>(std::move(handshaker), dispatcher);
  };

  initialize(nullptr, nullptr);

  InSequence s;

  EXPECT_CALL(client_.callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  // doWrite won't immediately fail, but it will result connection close.
  client_.tsi_socket_->onConnected();

  raw_handshaker->vtable = vtable;
}

TEST_F(TsiSocketTest, DoReadEndOfStream) {
  initialize(nullptr, nullptr);

  InSequence s;

  client_.write_buffer_.add(ClientToServerData);

  doHandshakeAndExpectSuccess();

  client_.tsi_socket_->setActualFrameSizeToUse(LargeFrameSize);

  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
  expectIoResult({Network::PostIoAction::KeepOpen, 17UL, false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, false));

  EXPECT_CALL(*server_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    Network::IoResult result = {Network::PostIoAction::KeepOpen, client_to_server_.length(), true};
    buffer.move(client_to_server_);
    return result;
  }));
  EXPECT_CALL(server_.callbacks_, shouldDrainReadBuffer());
  expectIoResult({Network::PostIoAction::KeepOpen, 17UL, true},
                 server_.tsi_socket_->doRead(server_.read_buffer_));

  EXPECT_EQ(ClientToServerData, server_.read_buffer_.toString());
}

TEST_F(TsiSocketTest, DoReadNoData) {
  initialize(nullptr, nullptr);

  InSequence s;

  client_.write_buffer_.add(ClientToServerData);

  doHandshakeAndExpectSuccess();

  client_.tsi_socket_->setActualFrameSizeToUse(LargeFrameSize);

  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
  expectIoResult({Network::PostIoAction::KeepOpen, 17UL, false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, false));

  EXPECT_CALL(*server_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    Network::IoResult result = {Network::PostIoAction::KeepOpen, client_to_server_.length(), false};
    buffer.move(client_to_server_);
    return result;
  }));
  EXPECT_CALL(server_.callbacks_, shouldDrainReadBuffer());
  EXPECT_CALL(*server_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    Network::IoResult result = {Network::PostIoAction::KeepOpen, 0UL, false};
    buffer.move(client_to_server_);
    return result;
  }));
  expectIoResult({Network::PostIoAction::KeepOpen, 17UL, false},
                 server_.tsi_socket_->doRead(server_.read_buffer_));

  EXPECT_EQ(ClientToServerData, server_.read_buffer_.toString());
}

TEST_F(TsiSocketTest, DoReadTwiceError) {
  initialize(nullptr, nullptr);

  client_.write_buffer_.add(ClientToServerData);

  InSequence s;

  doHandshakeAndExpectSuccess();

  client_.tsi_socket_->setActualFrameSizeToUse(LargeFrameSize);

  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
  expectIoResult({Network::PostIoAction::KeepOpen, 17UL, false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, false));

  EXPECT_CALL(*server_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    Network::IoResult result = {Network::PostIoAction::KeepOpen, client_to_server_.length(), false};
    buffer.move(client_to_server_);
    return result;
  }));
  EXPECT_CALL(server_.callbacks_, shouldDrainReadBuffer());
  EXPECT_CALL(*server_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    Network::IoResult result = {Network::PostIoAction::Close, 0UL, false};
    buffer.move(client_to_server_);
    return result;
  }));
  expectIoResult({Network::PostIoAction::Close, 17UL, false},
                 server_.tsi_socket_->doRead(server_.read_buffer_));

  EXPECT_EQ(ClientToServerData, server_.read_buffer_.toString());
}

TEST_F(TsiSocketTest, DoReadOnceError) {
  initialize(nullptr, nullptr);

  InSequence s;

  client_.write_buffer_.add(ClientToServerData);

  doHandshakeAndExpectSuccess();

  client_.tsi_socket_->setActualFrameSizeToUse(LargeFrameSize);

  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
  expectIoResult({Network::PostIoAction::KeepOpen, 17UL, false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, false));

  EXPECT_CALL(*server_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    Network::IoResult result = {Network::PostIoAction::Close, client_to_server_.length(), false};
    buffer.move(client_to_server_);
    return result;
  }));
  EXPECT_CALL(server_.callbacks_, shouldDrainReadBuffer());
  expectIoResult({Network::PostIoAction::Close, 17UL, false},
                 server_.tsi_socket_->doRead(server_.read_buffer_));

  EXPECT_EQ(ClientToServerData, server_.read_buffer_.toString());
}

TEST_F(TsiSocketTest, DoReadDrainBuffer) {
  initialize(nullptr, nullptr);

  InSequence s;

  client_.write_buffer_.add(ClientToServerData);

  doHandshakeAndExpectSuccess();

  client_.tsi_socket_->setActualFrameSizeToUse(LargeFrameSize);

  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
  expectIoResult({Network::PostIoAction::KeepOpen, 17UL, false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, false));

  EXPECT_CALL(*server_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    Network::IoResult result = {Network::PostIoAction::KeepOpen, client_to_server_.length(), false};
    buffer.move(client_to_server_);
    return result;
  }));
  EXPECT_CALL(server_.callbacks_, shouldDrainReadBuffer()).WillOnce(Return(true));
  expectIoResult({Network::PostIoAction::KeepOpen, 17UL, false},
                 server_.tsi_socket_->doRead(server_.read_buffer_));
  EXPECT_EQ(ClientToServerData, server_.read_buffer_.toString());
}

TEST_F(TsiSocketTest, DoReadDrainBufferTwice) {
  initialize(nullptr, nullptr);

  InSequence s;

  client_.write_buffer_.add(ClientToServerData);

  doHandshakeAndExpectSuccess();

  client_.tsi_socket_->setActualFrameSizeToUse(LargeFrameSize);

  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
  expectIoResult({Network::PostIoAction::KeepOpen, 17UL, false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, false));

  EXPECT_CALL(*server_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    Network::IoResult result = {Network::PostIoAction::KeepOpen, client_to_server_.length(), false};
    buffer.move(client_to_server_);
    return result;
  }));
  EXPECT_CALL(server_.callbacks_, shouldDrainReadBuffer()).WillOnce(Return(true));
  expectIoResult({Network::PostIoAction::KeepOpen, 17UL, false},
                 server_.tsi_socket_->doRead(server_.read_buffer_));
  EXPECT_EQ(ClientToServerData, server_.read_buffer_.toString());

  // Client sends data again.
  server_.read_buffer_.drain(server_.read_buffer_.length());
  client_.write_buffer_.add(ClientToServerData);
  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
  expectIoResult({Network::PostIoAction::KeepOpen, 17UL, false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, false));

  EXPECT_CALL(*server_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    Network::IoResult result = {Network::PostIoAction::KeepOpen, client_to_server_.length(), false};
    buffer.move(client_to_server_);
    return result;
  }));
  EXPECT_CALL(server_.callbacks_, shouldDrainReadBuffer());
  EXPECT_CALL(*server_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    Network::IoResult result = {Network::PostIoAction::KeepOpen, 0UL, false};
    buffer.move(client_to_server_);
    return result;
  }));
  expectIoResult({Network::PostIoAction::KeepOpen, 17UL, false},
                 server_.tsi_socket_->doRead(server_.read_buffer_));

  EXPECT_EQ(ClientToServerData, server_.read_buffer_.toString());
}

TEST_F(TsiSocketTest, DoWriteSmallFrameSize) {
  auto validator = [](const tsi_peer&, TsiInfo&, std::string&) { return true; };
  initialize(validator, validator);

  InSequence s;
  client_.write_buffer_.add(ClientToServerData);

  doHandshakeAndExpectSuccess();

  EXPECT_EQ(0L, server_.read_buffer_.length());
  EXPECT_EQ(0L, client_.read_buffer_.length());

  EXPECT_EQ("", client_.tsi_socket_->protocol());
  client_.tsi_socket_->setActualFrameSizeToUse(SmallFrameSize);
  // Since we use a small frame size, original data is divided into two parts,
  // and written to network in two iterations.
  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) {
        Network::IoResult result = {Network::PostIoAction::KeepOpen, buffer.length(), false};
        client_to_server_.move(buffer);
        return result;
      }));
  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) {
        Network::IoResult result = {Network::PostIoAction::KeepOpen, buffer.length(), false};
        client_to_server_.move(buffer);
        return result;
      }));
  expectIoResult({Network::PostIoAction::KeepOpen, 17UL, false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, false));

  EXPECT_EQ(makeFakeTsiFrame(ClientToServerDataFirstHalf) +
                makeFakeTsiFrame(ClientToServerDataSecondHalf),
            client_to_server_.toString());

  EXPECT_CALL(*server_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    Network::IoResult result = {Network::PostIoAction::Close, client_to_server_.length(), false};
    buffer.move(client_to_server_);
    return result;
  }));
  expectIoResult({Network::PostIoAction::Close, 17UL, false},
                 server_.tsi_socket_->doRead(server_.read_buffer_));
  EXPECT_EQ(ClientToServerData, server_.read_buffer_.toString());
}

TEST_F(TsiSocketTest, DoWriteSingleShortWrite) {
  auto validator = [](const tsi_peer&, TsiInfo&, std::string&) { return true; };
  initialize(validator, validator);

  InSequence s;
  client_.write_buffer_.add(ClientToServerData);

  doHandshakeAndExpectSuccess();

  client_.tsi_socket_->setActualFrameSizeToUse(LargeFrameSize);

  // Write the whole data except for the last byte.
  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) {
        Network::IoResult result = {Network::PostIoAction::KeepOpen, buffer.length() - 1, false};
        client_to_server_.add(buffer.linearize(0), buffer.length() - 1);
        return result;
      }));
  expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, false));

  EXPECT_EQ(makeFakeTsiFrame(ClientToServerData).substr(0, 20), client_to_server_.toString());

  // TSI frame is invalid, return with Close action.
  EXPECT_CALL(*server_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    Network::IoResult result = {Network::PostIoAction::Close, 20UL, true};
    buffer.move(client_to_server_);
    return result;
  }));
  expectIoResult({Network::PostIoAction::Close, 0UL, true},
                 server_.tsi_socket_->doRead(server_.read_buffer_));
}

TEST_F(TsiSocketTest, DoWriteMultipleShortWrites) {
  auto validator = [](const tsi_peer&, TsiInfo&, std::string&) { return true; };
  initialize(validator, validator);

  InSequence s;
  client_.write_buffer_.add(ClientToServerData);

  doHandshakeAndExpectSuccess();

  client_.tsi_socket_->setActualFrameSizeToUse(LargeFrameSize);

  // Write the whole data except for the last byte.
  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) {
        Network::IoResult result = {Network::PostIoAction::KeepOpen, buffer.length() - 1, false};
        client_to_server_.add(buffer.linearize(0), buffer.length() - 1);
        buffer.drain(buffer.length() - 1);
        return result;
      }));
  expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, false));

  EXPECT_EQ(makeFakeTsiFrame(ClientToServerData).substr(0, 20), client_to_server_.toString());

  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) {
        Network::IoResult result = {Network::PostIoAction::KeepOpen, 1, false};
        client_to_server_.add(buffer.linearize(buffer.length() - 1), 1);
        return result;
      }));
  expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, false));
  EXPECT_EQ(makeFakeTsiFrame(ClientToServerData), client_to_server_.toString());

  EXPECT_CALL(*server_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    Network::IoResult result = {Network::PostIoAction::Close, client_to_server_.length(), false};
    buffer.move(client_to_server_);
    return result;
  }));
  expectIoResult({Network::PostIoAction::Close, 17UL, false},
                 server_.tsi_socket_->doRead(server_.read_buffer_));
  EXPECT_EQ(ClientToServerData, server_.read_buffer_.toString());
}

TEST_F(TsiSocketTest, DoWriteMixShortFullWrites) {
  auto validator = [](const tsi_peer&, TsiInfo&, std::string&) { return true; };
  initialize(validator, validator);

  InSequence s;
  client_.write_buffer_.add(ClientToServerData);

  doHandshakeAndExpectSuccess();

  client_.tsi_socket_->setActualFrameSizeToUse(SmallFrameSize);

  // Short write occurred when writing the first half of data.
  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) {
        Network::IoResult result = {Network::PostIoAction::KeepOpen, buffer.length() - 1, false};
        client_to_server_.add(buffer.linearize(0), buffer.length() - 1);
        buffer.drain(buffer.length() - 1);
        return result;
      }));
  expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, false));
  EXPECT_EQ(makeFakeTsiFrame(ClientToServerDataFirstHalf).substr(0, 12),
            client_to_server_.toString());

  // In the next write, we first finish the remaining data that has not been
  // written in the previous write and then write the second half of data.
  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) {
        Network::IoResult result = {Network::PostIoAction::KeepOpen, 1, false};
        client_to_server_.move(buffer);
        return result;
      }));
  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) {
        Network::IoResult result = {Network::PostIoAction::KeepOpen, buffer.length(), false};
        client_to_server_.move(buffer);
        return result;
      }));
  expectIoResult({Network::PostIoAction::KeepOpen, 17UL, false},
                 client_.tsi_socket_->doWrite(client_.write_buffer_, false));
  EXPECT_EQ(makeFakeTsiFrame(ClientToServerDataFirstHalf) +
                makeFakeTsiFrame(ClientToServerDataSecondHalf),
            client_to_server_.toString());

  EXPECT_CALL(*server_.raw_socket_, doRead(_)).WillOnce(Invoke([&](Buffer::Instance& buffer) {
    Network::IoResult result = {Network::PostIoAction::Close, client_to_server_.length(), false};
    buffer.move(client_to_server_);
    return result;
  }));
  expectIoResult({Network::PostIoAction::Close, 17UL, false},
                 server_.tsi_socket_->doRead(server_.read_buffer_));
  EXPECT_EQ(ClientToServerData, server_.read_buffer_.toString());
}

TEST_F(TsiSocketTest, DoWriteOutstandingHandshakeData) {
  auto validator = [](const tsi_peer&, TsiInfo&, std::string&) { return true; };
  initialize(validator, validator);

  InSequence s;
  doFakeInitHandshake();

  EXPECT_CALL(*client_.raw_socket_, doRead(_));
  EXPECT_CALL(*client_.raw_socket_, doWrite(_, false));
  expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                 client_.tsi_socket_->doRead(client_.read_buffer_));
  EXPECT_EQ(makeFakeTsiFrame("CLIENT_FINISHED"), client_to_server_.toString());
  EXPECT_EQ(0L, client_.read_buffer_.length());

  EXPECT_CALL(*server_.raw_socket_, doRead(_));

  // Write the first part of handshake data (14 bytes).
  EXPECT_CALL(*server_.raw_socket_, doWrite(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) {
        Network::IoResult result = {Network::PostIoAction::KeepOpen, buffer.length() - 5, false};
        server_to_client_.move(buffer, 14);
        return result;
      }));
  EXPECT_CALL(server_.callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
  EXPECT_CALL(*server_.raw_socket_, doRead(_));
  expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                 server_.tsi_socket_->doRead(server_.read_buffer_));

  EXPECT_EQ(makeFakeTsiFrame("SERVER_FINISHED").length(), 19);
  EXPECT_EQ(makeFakeTsiFrame("SERVER_FINISHED").substr(0, 14), server_to_client_.toString());

  server_.write_buffer_.add(ServerToClientData);
  server_.tsi_socket_->setActualFrameSizeToUse(LargeFrameSize);

  // Write the second part of handshake data (4 bytes).
  EXPECT_CALL(*server_.raw_socket_, doWrite(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) {
        Network::IoResult result = {Network::PostIoAction::KeepOpen, 4, false};
        server_to_client_.move(buffer, 4);
        return result;
      }));
  expectIoResult({Network::PostIoAction::KeepOpen, 0UL, false},
                 server_.tsi_socket_->doWrite(server_.write_buffer_, false));
  EXPECT_EQ(makeFakeTsiFrame("SERVER_FINISHED").substr(0, 18), server_to_client_.toString());

  // Write the last part of handshake data (1 byte) and frame data.
  EXPECT_CALL(*server_.raw_socket_, doWrite(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) {
        Network::IoResult result = {Network::PostIoAction::KeepOpen, 1, false};
        server_to_client_.move(buffer);
        return result;
      }));
  EXPECT_CALL(*server_.raw_socket_, doWrite(_, false));
  expectIoResult({Network::PostIoAction::KeepOpen, 17UL, false},
                 server_.tsi_socket_->doWrite(server_.write_buffer_, false));
  EXPECT_EQ(makeFakeTsiFrame("SERVER_FINISHED") + makeFakeTsiFrame(ServerToClientData),
            server_to_client_.toString());

  // Check client side (handshake completes + receive unused data).
  EXPECT_CALL(*client_.raw_socket_, doRead(_));
  EXPECT_CALL(client_.callbacks_, raiseEvent(Network::ConnectionEvent::Connected));
  EXPECT_CALL(*client_.raw_socket_, doRead(_));
  expectIoResult({Network::PostIoAction::KeepOpen, 17UL, false},
                 client_.tsi_socket_->doRead(client_.read_buffer_));
  EXPECT_EQ(ServerToClientData, client_.read_buffer_.toString());
}

class TsiSocketFactoryTest : public testing::Test {
protected:
  void SetUp() override {
    auto handshaker_factory = [](Event::Dispatcher& dispatcher,
                                 const Network::Address::InstanceConstSharedPtr&,
                                 const Network::Address::InstanceConstSharedPtr&) {
      CHandshakerPtr handshaker{tsi_create_fake_handshaker(/*is_client=*/0)};

      return std::make_unique<TsiHandshaker>(std::move(handshaker), dispatcher);
    };

    socket_factory_ = std::make_unique<TsiSocketFactory>(handshaker_factory, nullptr);
  }
  Network::TransportSocketFactoryPtr socket_factory_;
};

TEST_F(TsiSocketFactoryTest, CreateTransportSocket) {
  EXPECT_NE(nullptr, socket_factory_->createTransportSocket(nullptr));
}

TEST_F(TsiSocketFactoryTest, ImplementsSecureTransport) {
  EXPECT_TRUE(socket_factory_->implementsSecureTransport());
}

TEST_F(TsiSocketFactoryTest, UsesProxyProtocolOptions) {
  EXPECT_FALSE(socket_factory_->usesProxyProtocolOptions());
}

} // namespace
} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
