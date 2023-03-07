#include "envoy/http/metadata_interface.h"

#include "source/common/quic/capsule_protocol_handler.h"

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
};

class CapsuleProtocolHandlerTest : public ::testing::Test {
public:
  CapsuleProtocolHandlerTest() { capsule_protocol_handler_.setStreamDecoder(&stream_decoder_); };

protected:
  testing::NiceMock<quic::test::MockQuicConnectionHelper> connection_helper_;
  testing::NiceMock<quic::test::MockAlarmFactory> alarm_factory_;
  testing::NiceMock<MockSession> session_{new testing::NiceMock<quic::test::MockQuicConnection>(
      &connection_helper_, &alarm_factory_, quic::Perspective::IS_SERVER)};
  testing::StrictMock<MockStreamDecoder> stream_decoder_;
  testing::StrictMock<MockStream> stream_{&session_};
  CapsuleProtocolHandler capsule_protocol_handler_{&stream_};

  std::string datagram_payload_ = absl::HexStringToBytes("a1a2a3a4a5a6a7a8");
  std::string capsule_fragment_ = absl::HexStringToBytes("00"               // DATAGRAM capsule type
                                                         "08"               // capsule length
                                                         "a1a2a3a4a5a6a7a8" // HTTP Datagram payload
  );
};

TEST_F(CapsuleProtocolHandlerTest, Http3DatagramToCapsule) {
  EXPECT_CALL(stream_decoder_,
              decodeData(BufferStringEqual(capsule_fragment_), /*end_stream=*/false));
  capsule_protocol_handler_.OnHttp3Datagram(kStreamId, datagram_payload_);
}

TEST_F(CapsuleProtocolHandlerTest, CapsuleToHttp3Datagram) {
  EXPECT_CALL(stream_, SendHttp3Datagram(testing::Eq(datagram_payload_)))
      .WillOnce(testing::Return(quic::MessageStatus::MESSAGE_STATUS_SUCCESS));
  EXPECT_TRUE(capsule_protocol_handler_.encodeCapsule(capsule_fragment_));
}

TEST_F(CapsuleProtocolHandlerTest, IncorrectCapsuleTypes) {
  std::string invalid_capsule_fragment =
      absl::HexStringToBytes("aaaaaaaa"         // invalid capsule type
                             "08"               // capsule length
                             "a1a2a3a4a5a6a7a8" // HTTP Datagram payload
      );
  std::string legacy_capsule_fragment =
      absl::HexStringToBytes("80ff37a0"         // LEGACY_DATAGRAM capsule type
                             "08"               // capsule length
                             "a1a2a3a4a5a6a7a8" // HTTP Datagram payload
      );
  std::string legacy_without_context_capsule_fragment =
      absl::HexStringToBytes("80ff37a5"         // LEGACY_DATAGRAM_WITHOUT_CONTEXT capsule type
                             "08"               // capsule length
                             "a1a2a3a4a5a6a7a8" // HTTP Datagram payload
      );

  // EXPECT never call SendHttp3Datagram (stream_ is a StrictMock object).
  EXPECT_FALSE(capsule_protocol_handler_.encodeCapsule(invalid_capsule_fragment));
  EXPECT_FALSE(capsule_protocol_handler_.encodeCapsule(legacy_capsule_fragment));
  EXPECT_FALSE(capsule_protocol_handler_.encodeCapsule(legacy_without_context_capsule_fragment));
}

TEST_F(CapsuleProtocolHandlerTest, SendHttp3DatagramError) {
  EXPECT_CALL(stream_, SendHttp3Datagram(_))
      .WillOnce(testing::Return(quic::MessageStatus::MESSAGE_STATUS_INTERNAL_ERROR));
  EXPECT_FALSE(capsule_protocol_handler_.encodeCapsule(capsule_fragment_));
}

} // namespace Quic
} // namespace Envoy
