#include "envoy/http/metadata_interface.h"

#include "source/common/quic/http_datagram_handler.h"

#include "test/mocks/buffer/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

namespace Envoy {
namespace Quic {

namespace {

constexpr quic::QuicStreamId kStreamId = UINT64_C(0x01020304);

} // namespace

class MockSession : public quic::test::MockQuicSpdySession {
public:
  explicit MockSession(quic::QuicConnection* connection)
      : quic::test::MockQuicSpdySession(connection){};

  MOCK_METHOD(void, OnStreamClosed, (quic::QuicStreamId stream_id), (override));
};

class MockStreamDecoder : public Http::StreamDecoder {
public:
  MockStreamDecoder() = default;

  MOCK_METHOD(void, decodeMetadata, (std::unique_ptr<Http::MetadataMap> && metadata_map),
              (override));
  MOCK_METHOD(void, decodeData, (Buffer::Instance & data, bool end_stream), (override));
};

class MockStream : public quic::QuicSpdyStream {
public:
  explicit MockStream(quic::QuicSpdySession* spdy_session)
      : quic::QuicSpdyStream(kStreamId, spdy_session, quic::BIDIRECTIONAL) {}

  MOCK_METHOD(void, OnBodyAvailable, (), (override));
  MOCK_METHOD(quic::MessageStatus, SendHttp3Datagram, (absl::string_view data), (override));
  MOCK_METHOD(void, WriteOrBufferBody, (absl::string_view data, bool fin), (override));
};

class HttpDatagramHandlerTest : public ::testing::Test {
public:
  HttpDatagramHandlerTest() { http_datagram_handler_.setStreamDecoder(&stream_decoder_); };

protected:
  testing::NiceMock<quic::test::MockQuicConnectionHelper> connection_helper_;
  testing::NiceMock<quic::test::MockAlarmFactory> alarm_factory_;
  testing::NiceMock<MockSession> session_{new testing::NiceMock<quic::test::MockQuicConnection>(
      &connection_helper_, &alarm_factory_, quic::Perspective::IS_SERVER)};
  testing::StrictMock<MockStreamDecoder> stream_decoder_;
  testing::StrictMock<MockStream> stream_{&session_};
  HttpDatagramHandler http_datagram_handler_{stream_};

  std::string datagram_payload_ = absl::HexStringToBytes("a1a2a3a4a5a6a7a8");
  std::string capsule_fragment_ = absl::HexStringToBytes("00"               // DATAGRAM capsule type
                                                         "08"               // capsule length
                                                         "a1a2a3a4a5a6a7a8" // HTTP Datagram payload
  );
  std::string unknown_capsule_fragment_ =
      absl::HexStringToBytes("17"               // DATAGRAM capsule type
                             "08"               // capsule length
                             "a1a2a3a4a5a6a7a8" // HTTP Datagram payload
      );
};

TEST_F(HttpDatagramHandlerTest, Http3DatagramToCapsule) {
  EXPECT_CALL(stream_decoder_,
              decodeData(BufferStringEqual(capsule_fragment_), /*end_stream=*/false));
  http_datagram_handler_.OnHttp3Datagram(kStreamId, datagram_payload_);
}

TEST_F(HttpDatagramHandlerTest, CapsuleToHttp3Datagram) {
  EXPECT_CALL(stream_, SendHttp3Datagram(testing::Eq(datagram_payload_)))
      .WillOnce(testing::Return(quic::MessageStatus::MESSAGE_STATUS_SUCCESS))
      .WillOnce(testing::Return(quic::MessageStatus::MESSAGE_STATUS_BLOCKED));
  EXPECT_TRUE(
      http_datagram_handler_.encodeCapsuleFragment(capsule_fragment_, /*end_stream=*/false));
  EXPECT_TRUE(
      http_datagram_handler_.encodeCapsuleFragment(capsule_fragment_, /*end_stream=*/false));
}

TEST_F(HttpDatagramHandlerTest, ReceiveCapsuleWithUnknownType) {
  EXPECT_CALL(stream_decoder_,
              decodeData(BufferStringEqual(unknown_capsule_fragment_), /*end_stream=*/false));
  std::string payload = absl::HexStringToBytes("a1a2a3a4a5a6a7a8");
  quiche::UnknownCapsule capsule{0x17u, payload};
  http_datagram_handler_.OnUnknownCapsule(kStreamId, capsule);
}

TEST_F(HttpDatagramHandlerTest, SendCapsulesWithUnknownType) {
  EXPECT_CALL(stream_,
              WriteOrBufferBody(testing::Eq(unknown_capsule_fragment_), /*end_stream=*/false));
  EXPECT_TRUE(http_datagram_handler_.encodeCapsuleFragment(unknown_capsule_fragment_,
                                                           /*end_stream=*/false));
}

TEST_F(HttpDatagramHandlerTest, SendHttp3DatagramInternalError) {
  EXPECT_CALL(stream_, SendHttp3Datagram(_))
      .WillOnce(testing::Return(quic::MessageStatus::MESSAGE_STATUS_INTERNAL_ERROR));
  EXPECT_FALSE(
      http_datagram_handler_.encodeCapsuleFragment(capsule_fragment_, /*end_stream*/ false));
}

TEST_F(HttpDatagramHandlerTest, SendHttp3DatagramTooEarly) {
  // If SendHttp3Datagram is called before receiving SETTINGS from a peer, HttpDatagramHandler
  // drops the datagram without resetting the stream.
  EXPECT_CALL(stream_, SendHttp3Datagram(_))
      .WillOnce(testing::Return(quic::MessageStatus::MESSAGE_STATUS_SETTINGS_NOT_RECEIVED));
  EXPECT_TRUE(
      http_datagram_handler_.encodeCapsuleFragment(capsule_fragment_, /*end_stream*/ false));
}

} // namespace Quic
} // namespace Envoy
