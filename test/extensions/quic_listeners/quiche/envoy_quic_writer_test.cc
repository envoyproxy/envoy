#include <memory>
#include <string>

#include "common/network/io_socket_error_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_packet_writer.h"

#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicWriterTest : public ::testing::Test {
public:
  EnvoyQuicWriterTest() : envoy_quic_writer_(udp_listener_) {
    self_address_.FromString("0.0.0.0");
    quic::QuicIpAddress peer_ip;
    peer_ip.FromString("127.0.0.1");
    peer_address_ = quic::QuicSocketAddress(peer_ip, /*port=*/123);
    EXPECT_CALL(udp_listener_, onDestroy());
    ON_CALL(udp_listener_, send(_))
        .WillByDefault(testing::Invoke([](const Network::UdpSendData& send_data) {
          return Api::IoCallUint64Result(
              send_data.buffer_.length(),
              Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError));
        }));
  }

  void verifySendData(const std::string& content, const Network::UdpSendData send_data) {
    EXPECT_EQ(peer_address_.ToString(), send_data.peer_address_.asString());
    EXPECT_EQ(self_address_.ToString(), send_data.local_ip_->addressAsString());
    EXPECT_EQ(content, send_data.buffer_.toString());
  }

protected:
  testing::NiceMock<Network::MockUdpListener> udp_listener_;
  quic::QuicIpAddress self_address_;
  quic::QuicSocketAddress peer_address_;
  EnvoyQuicPacketWriter envoy_quic_writer_;
};

TEST_F(EnvoyQuicWriterTest, AssertOnNonNullPacketOption) {
  std::string str("Hello World!");
  EXPECT_DEBUG_DEATH(envoy_quic_writer_.WritePacket(str.data(), str.length(), self_address_,
                                                    peer_address_,
                                                    reinterpret_cast<quic::PerPacketOptions*>(0x1)),
                     "Per packet option is not supported yet.");
}

TEST_F(EnvoyQuicWriterTest, SendSuccessfully) {
  std::string str("Hello World!");
  EXPECT_CALL(udp_listener_, send(_))
      .WillOnce(testing::Invoke([this, str](const Network::UdpSendData& send_data) {
        verifySendData(str, send_data);
        return Api::IoCallUint64Result(
            str.length(), Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError));
      }));
  quic::WriteResult result = envoy_quic_writer_.WritePacket(str.data(), str.length(), self_address_,
                                                            peer_address_, nullptr);
  EXPECT_EQ(quic::WRITE_STATUS_OK, result.status);
  EXPECT_EQ(str.length(), result.bytes_written);
  EXPECT_FALSE(envoy_quic_writer_.IsWriteBlocked());
}

TEST_F(EnvoyQuicWriterTest, SendBlocked) {
  std::string str("Hello World!");
  EXPECT_CALL(udp_listener_, send(_))
      .WillOnce(testing::Invoke([this, str](const Network::UdpSendData& send_data) {
        verifySendData(str, send_data);
        return Api::IoCallUint64Result(
            0u, Api::IoErrorPtr(Network::IoSocketError::getIoSocketEagainInstance(),
                                Network::IoSocketError::deleteIoError));
      }));
  quic::WriteResult result = envoy_quic_writer_.WritePacket(str.data(), str.length(), self_address_,
                                                            peer_address_, nullptr);
  EXPECT_EQ(quic::WRITE_STATUS_BLOCKED, result.status);
  EXPECT_EQ(static_cast<int>(Api::IoError::IoErrorCode::Again), result.error_code);
  EXPECT_TRUE(envoy_quic_writer_.IsWriteBlocked());
  // Writing while blocked is not allowed.
#ifdef NDEBUG
  EXPECT_CALL(udp_listener_, send(_))
      .WillOnce(testing::Invoke([this, str](const Network::UdpSendData& send_data) {
        verifySendData(str, send_data);
        return Api::IoCallUint64Result(
            0u, Api::IoErrorPtr(Network::IoSocketError::getIoSocketEagainInstance(),
                                Network::IoSocketError::deleteIoError));
      }));
#endif
  EXPECT_DEBUG_DEATH(envoy_quic_writer_.WritePacket(str.data(), str.length(), self_address_,
                                                    peer_address_, nullptr),
                     "Cannot write while IO handle is blocked.");
  envoy_quic_writer_.SetWritable();
  EXPECT_FALSE(envoy_quic_writer_.IsWriteBlocked());
}

TEST_F(EnvoyQuicWriterTest, SendFailure) {
  std::string str("Hello World!");
  EXPECT_CALL(udp_listener_, send(_))
      .WillOnce(testing::Invoke(
          [this, str](const Network::UdpSendData& send_data) -> Api::IoCallUint64Result {
            verifySendData(str, send_data);
            return Api::IoCallUint64Result(0u,
                                           Api::IoErrorPtr(new Network::IoSocketError(ENOTSUP),
                                                           Network::IoSocketError::deleteIoError));
          }));
  quic::WriteResult result = envoy_quic_writer_.WritePacket(str.data(), str.length(), self_address_,
                                                            peer_address_, nullptr);
  EXPECT_EQ(quic::WRITE_STATUS_ERROR, result.status);
  EXPECT_EQ(static_cast<int>(Api::IoError::IoErrorCode::NoSupport), result.error_code);
  EXPECT_FALSE(envoy_quic_writer_.IsWriteBlocked());
}

TEST_F(EnvoyQuicWriterTest, SendFailureMessageTooBig) {
  std::string str("Hello World!");
  EXPECT_CALL(udp_listener_, send(_))
      .WillOnce(testing::Invoke([this, str](const Network::UdpSendData& send_data) {
        verifySendData(str, send_data);
        return Api::IoCallUint64Result(0u, Api::IoErrorPtr(new Network::IoSocketError(EMSGSIZE),
                                                           Network::IoSocketError::deleteIoError));
      }));
  quic::WriteResult result = envoy_quic_writer_.WritePacket(str.data(), str.length(), self_address_,
                                                            peer_address_, nullptr);
  // Currently MessageSize should be propagated through error_code. This test
  // would fail if QUICHE changes to propagate through status in the future.
  EXPECT_EQ(quic::WRITE_STATUS_ERROR, result.status);
  EXPECT_EQ(static_cast<int>(Api::IoError::IoErrorCode::MessageTooBig), result.error_code);
  EXPECT_FALSE(envoy_quic_writer_.IsWriteBlocked());
}

} // namespace Quic
} // namespace Envoy
