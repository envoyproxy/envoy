#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"

#include "extensions/transport_sockets/permissive/permissive_socket.h"

#include "test/mocks/network/mocks.h"

using testing::_;
using testing::AtMost;
using testing::InSequence;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Permissive {
namespace {

class MockSslTransportSocket : public Network::MockTransportSocket,
                               public Envoy::Ssl::ConnectionInfo {
public:
  ~MockSslTransportSocket() {}

  // Ssl::ConnectionInfo
  MOCK_CONST_METHOD0(peerCertificatePresented, bool());
  MOCK_CONST_METHOD0(uriSanLocalCertificate, std::vector<std::string>());
  MOCK_CONST_METHOD0(sha256PeerCertificateDigest, const std::string&());
  MOCK_CONST_METHOD0(serialNumberPeerCertificate, std::string());
  MOCK_CONST_METHOD0(issuerPeerCertificate, std::string());
  MOCK_CONST_METHOD0(subjectPeerCertificate, std::string());
  MOCK_CONST_METHOD0(subjectLocalCertificate, std::string());
  MOCK_CONST_METHOD0(uriSanPeerCertificate, std::vector<std::string>());
  MOCK_CONST_METHOD0(urlEncodedPemEncodedPeerCertificate, const std::string&());
  MOCK_CONST_METHOD0(urlEncodedPemEncodedPeerCertificateChain, const std::string&());
  MOCK_CONST_METHOD0(dnsSansPeerCertificate, std::vector<std::string>());
  MOCK_CONST_METHOD0(dnsSansLocalCertificate, std::vector<std::string>());
  MOCK_CONST_METHOD0(validFromPeerCertificate, absl::optional<SystemTime>());
  MOCK_CONST_METHOD0(expirationPeerCertificate, absl::optional<SystemTime>());
  MOCK_CONST_METHOD0(sessionId, std::string());
  MOCK_CONST_METHOD0(ciphersuiteId, uint16_t());
  MOCK_CONST_METHOD0(ciphersuiteString, std::string());
  MOCK_CONST_METHOD0(tlsVersion, std::string());
};

class PermissiveSocketTest : public testing::Test {
protected:
  PermissiveSocketTest()
      : raw_buffer_transport_socket_(new NiceMock<Network::MockTransportSocket>),
        ssl_transport_socket_(new NiceMock<MockSslTransportSocket>) {}

  void initialize(bool allow_fallback) {
    auto unique_raw_buffer_transport_socket =
        std::unique_ptr<Network::MockTransportSocket>(raw_buffer_transport_socket_);
    auto unique_ssl_transport_socket =
        std::unique_ptr<MockSslTransportSocket>(ssl_transport_socket_);
    permissive_transport_socket_ = std::make_unique<PermissiveSocket>(
        std::move(unique_ssl_transport_socket), std::move(unique_raw_buffer_transport_socket),
        allow_fallback);

    EXPECT_CALL(*ssl_transport_socket_, setTransportSocketCallbacks(_)).Times(1);
    permissive_transport_socket_->setTransportSocketCallbacks(callbacks_);
  }

  void fallback() {
    EXPECT_FALSE(permissive_transport_socket_->isFallback());
    Network::IoResult io_result = {Network::PostIoAction::Close, 0, false};
    EXPECT_CALL(*ssl_transport_socket_, doRead(_)).WillOnce(Return(io_result));
    EXPECT_CALL(*ssl_transport_socket_, canFlushClose()).WillOnce(Return(false));

    EXPECT_CALL(callbacks_, connection()).Times(AtMost(1));
    permissive_transport_socket_->doRead(read_buffer_);
  }

  std::unique_ptr<PermissiveSocket> permissive_transport_socket_;
  NiceMock<Network::MockTransportSocket>* raw_buffer_transport_socket_;
  NiceMock<MockSslTransportSocket>* ssl_transport_socket_;
  NiceMock<Network::MockTransportSocketCallbacks> callbacks_;

  Buffer::OwnedImpl write_buffer_;
  Buffer::OwnedImpl read_buffer_;
};

TEST_F(PermissiveSocketTest, FallbackOnWrite) {
  InSequence s;

  initialize(true);

  EXPECT_FALSE(permissive_transport_socket_->isFallback());

  Network::IoResult io_result = {Network::PostIoAction::Close, 0, false};
  EXPECT_CALL(*ssl_transport_socket_, doWrite(BufferStringEqual("hello"), false))
      .WillOnce(Return(io_result));
  EXPECT_CALL(*ssl_transport_socket_, canFlushClose()).WillOnce(Return(false));

  write_buffer_.add("hello");
  EXPECT_CALL(callbacks_, connection()).Times(AtMost(1));

  io_result = permissive_transport_socket_->doWrite(write_buffer_, false);

  EXPECT_EQ(Network::PostIoAction::Reconnect, io_result.action_);
  EXPECT_TRUE(permissive_transport_socket_->isFallback());
}

TEST_F(PermissiveSocketTest, FallbackOnRead) {
  InSequence s;

  initialize(true);

  EXPECT_FALSE(permissive_transport_socket_->isFallback());

  Network::IoResult io_result = {Network::PostIoAction::Close, 0, false};
  EXPECT_CALL(*ssl_transport_socket_, doRead(_)).WillOnce(Return(io_result));
  EXPECT_CALL(*ssl_transport_socket_, canFlushClose()).WillOnce(Return(false));

  EXPECT_CALL(callbacks_, connection()).Times(AtMost(1));
  io_result = permissive_transport_socket_->doRead(read_buffer_);

  EXPECT_EQ(Network::PostIoAction::Reconnect, io_result.action_);
  EXPECT_TRUE(permissive_transport_socket_->isFallback());
}

TEST_F(PermissiveSocketTest, DoNotFallbackWhenHandShakeNotCompleteSocketKeepOpen) {
  InSequence s;

  initialize(true);

  EXPECT_FALSE(permissive_transport_socket_->isFallback());

  Network::IoResult io_result = {Network::PostIoAction::KeepOpen, 0, false};

  EXPECT_CALL(*ssl_transport_socket_, doRead(_)).WillOnce(Return(io_result));
  EXPECT_CALL(*ssl_transport_socket_, canFlushClose()).WillOnce(Return(false));

  EXPECT_CALL(callbacks_, connection()).Times(AtMost(1));
  io_result = permissive_transport_socket_->doRead(read_buffer_);

  EXPECT_EQ(Network::PostIoAction::KeepOpen, io_result.action_);
  EXPECT_FALSE(permissive_transport_socket_->isFallback());
}

TEST_F(PermissiveSocketTest, DoNotFallbackWhenHandShakeCompleteSocketClosed) {
  InSequence s;

  initialize(true);

  EXPECT_FALSE(permissive_transport_socket_->isFallback());

  Network::IoResult io_result = {Network::PostIoAction::Close, 0, false};

  EXPECT_CALL(*ssl_transport_socket_, doRead(_)).WillOnce(Return(io_result));
  EXPECT_CALL(*ssl_transport_socket_, canFlushClose()).WillOnce(Return(true));

  EXPECT_CALL(callbacks_, connection()).Times(AtMost(1));
  io_result = permissive_transport_socket_->doRead(read_buffer_);

  EXPECT_EQ(Network::PostIoAction::Close, io_result.action_);
  EXPECT_FALSE(permissive_transport_socket_->isFallback());
}

TEST_F(PermissiveSocketTest, ProtocolBeforeAndAfterFallback) {
  InSequence s;

  initialize(true);

  ON_CALL(*ssl_transport_socket_, protocol()).WillByDefault(Return("h2"));
  ON_CALL(*raw_buffer_transport_socket_, protocol()).WillByDefault(Return(EMPTY_STRING));

  EXPECT_EQ("h2", permissive_transport_socket_->protocol());
  fallback();
  EXPECT_EQ(EMPTY_STRING, permissive_transport_socket_->protocol());
}

TEST_F(PermissiveSocketTest, FailureReasonBeforeAndAfterFallback) {
  InSequence s;

  initialize(true);

  std::string primary_failure = "primary failure";
  std::string secondary_failure = "secondary failure";
  ON_CALL(*ssl_transport_socket_, failureReason()).WillByDefault(Return(primary_failure));
  ON_CALL(*raw_buffer_transport_socket_, failureReason()).WillByDefault(Return(secondary_failure));

  EXPECT_EQ(primary_failure, permissive_transport_socket_->failureReason());
  fallback();
  EXPECT_EQ(secondary_failure, permissive_transport_socket_->failureReason());
}

TEST_F(PermissiveSocketTest, OnConnectedBeforeAndAfterFallback) {
  InSequence s;

  initialize(true);

  EXPECT_CALL(*ssl_transport_socket_, onConnected()).Times(1);
  permissive_transport_socket_->onConnected();

  fallback();

  EXPECT_CALL(*raw_buffer_transport_socket_, onConnected()).Times(1);
  permissive_transport_socket_->onConnected();
}

TEST_F(PermissiveSocketTest, CanFlushCloseBeforeAndAfterFallback) {
  InSequence s;

  initialize(true);

  EXPECT_CALL(*ssl_transport_socket_, canFlushClose()).Times(1);
  permissive_transport_socket_->canFlushClose();

  fallback();

  EXPECT_CALL(*raw_buffer_transport_socket_, canFlushClose()).Times(1);
  permissive_transport_socket_->canFlushClose();
}

TEST_F(PermissiveSocketTest, CloseSocketBeforeAndAfterFallback) {
  InSequence s;

  initialize(true);

  EXPECT_CALL(*ssl_transport_socket_, closeSocket(Network::ConnectionEvent::LocalClose)).Times(1);
  permissive_transport_socket_->closeSocket(Network::ConnectionEvent::LocalClose);

  fallback();

  EXPECT_CALL(*raw_buffer_transport_socket_, closeSocket(Network::ConnectionEvent::LocalClose))
      .Times(1);
  permissive_transport_socket_->closeSocket(Network::ConnectionEvent::LocalClose);
}

TEST_F(PermissiveSocketTest, CheckSslBeforeAndAfterFallback) {
  InSequence s;

  initialize(true);

  ON_CALL(*ssl_transport_socket_, ssl()).WillByDefault(Return(ssl_transport_socket_));
  ON_CALL(*raw_buffer_transport_socket_, ssl()).WillByDefault(Return(nullptr));

  EXPECT_NE(nullptr, permissive_transport_socket_->ssl());
  fallback();
  EXPECT_EQ(nullptr, permissive_transport_socket_->ssl());
}

TEST_F(PermissiveSocketTest, FallbackIsNotAllowed) {
  InSequence s;

  initialize(false);

  fallback();

  // Failed to fall back.
  EXPECT_FALSE(permissive_transport_socket_->isFallback());
}

} // namespace
} // namespace Permissive
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
