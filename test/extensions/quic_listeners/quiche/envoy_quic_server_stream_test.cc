#include "extensions/quic_listeners/quiche/envoy_quic_server_stream.h"

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/core/http/quic_server_session_base.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

#pragma GCC diagnostic pop

#include <string>

#include "common/event/libevent_scheduler.h"
#include "common/http/headers.h"
#include "test/test_common/utility.h"
#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"
#include "extensions/quic_listeners/quiche/envoy_quic_server_connection.h"
#include "extensions/quic_listeners/quiche/envoy_quic_connection_helper.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/network/mocks.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Quic {

class MockQuicServerSession : public quic::QuicServerSessionBase {
public:
  MockQuicServerSession(const quic::QuicConfig& config,
                        const quic::ParsedQuicVersionVector& supported_versions,
                        quic::QuicConnection* connection, quic::QuicSession::Visitor* visitor,
                        quic::QuicCryptoServerStream::Helper* helper,
                        const quic::QuicCryptoServerConfig* crypto_config,
                        quic::QuicCompressedCertsCache* compressed_certs_cache)
      : quic::QuicServerSessionBase(config, supported_versions, connection, visitor, helper,
                                    crypto_config, compressed_certs_cache) {}

  MOCK_METHOD1(CreateIncomingStream, quic::QuicSpdyStream*(quic::QuicStreamId id));
  MOCK_METHOD1(CreateIncomingStream, quic::QuicSpdyStream*(quic::PendingStream* pending));
  MOCK_METHOD0(CreateOutgoingBidirectionalStream, quic::QuicSpdyStream*());
  MOCK_METHOD0(CreateOutgoingUnidirectionalStream, quic::QuicSpdyStream*());
  MOCK_METHOD1(ShouldCreateIncomingStream, bool(quic::QuicStreamId id));
  MOCK_METHOD0(ShouldCreateOutgoingBidirectionalStream, bool());
  MOCK_METHOD0(ShouldCreateOutgoingUnidirectionalStream, bool());
  MOCK_METHOD2(CreateQuicCryptoServerStream,
               quic::QuicCryptoServerStream*(const quic::QuicCryptoServerConfig*,
                                             quic::QuicCompressedCertsCache*));
};

class EnvoyQuicServerStreamTest : public testing::TestWithParam<bool> {
public:
  EnvoyQuicServerStreamTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher()),
        connection_helper_(*dispatcher_),
        alarm_factory_(*dispatcher_, *connection_helper_.GetClock()), quic_version_([]() {
          SetQuicReloadableFlag(quic_enable_version_99, GetParam());
          return quic::CurrentSupportedVersions()[0];
        }()),
        listener_stats_({ALL_LISTENER_STATS(POOL_COUNTER(listener_config_.listenerScope()),
                                            POOL_GAUGE(listener_config_.listenerScope()),
                                            POOL_HISTOGRAM(listener_config_.listenerScope()))}),
        quic_connection_(quic::test::TestConnectionId(),
                         quic::QuicSocketAddress(quic::QuicIpAddress::Any6(), 12345),
                         connection_helper_, alarm_factory_, &writer_,
                         /*owns_writer=*/false, {quic_version_}, listener_config_, listener_stats_),
        quic_session_(quic_config_, {quic_version_}, &quic_connection_, /*visitor=*/nullptr,
                      /*helper=*/nullptr, /*crypto_config=*/nullptr,
                      /*compressed_certs_cache=*/nullptr),
        stream_id_(quic_version_.transport_version == quic::QUIC_VERSION_99 ? 4u : 5u),
        quic_stream_(stream_id_, &quic_session_, quic::BIDIRECTIONAL) {
    quic::SetVerbosityLogThreshold(3);

    quic_stream_.setDecoder(stream_decoder_);
  }

  void SetUp() override {
    headers_.OnHeaderBlockStart();
    headers_.OnHeader(":authority", host_);
    headers_.OnHeader(":method", "GET");
    headers_.OnHeader(":path", "/");
    headers_.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0, /*compressed_header_bytes=*/0);

    trailers_.OnHeaderBlockStart();
    trailers_.OnHeader("key1", "value1");
    if (quic_version_.transport_version != quic::QUIC_VERSION_99) {
      // ":final-offset" is required and stripped off by quic.
      trailers_.OnHeader(":final-offset", absl::StrCat("", request_body_.length()));
    }
    trailers_.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0, /*compressed_header_bytes=*/0);
  }

protected:
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  EnvoyQuicConnectionHelper connection_helper_;
  EnvoyQuicAlarmFactory alarm_factory_;
  testing::NiceMock<quic::test::MockPacketWriter> writer_;
  quic::ParsedQuicVersion quic_version_;
  quic::QuicConfig quic_config_;
  testing::NiceMock<Network::MockListenerConfig> listener_config_;
  Server::ListenerStats listener_stats_;
  EnvoyQuicServerConnection quic_connection_;
  MockQuicServerSession quic_session_;
  quic::QuicStreamId stream_id_;
  EnvoyQuicServerStream quic_stream_;
  Http::MockStreamDecoder stream_decoder_;
  quic::QuicHeaderList headers_;
  quic::QuicHeaderList trailers_;
  std::string host_{"www.abc.com"};
  std::string request_body_{"Hello world"};
};

INSTANTIATE_TEST_SUITE_P(EnvoyQuicServerStreamTests, EnvoyQuicServerStreamTest,
                         testing::ValuesIn({true, false}));

TEST_P(EnvoyQuicServerStreamTest, DecodeHeadersAndBody) {
  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([this](const Http::HeaderMapPtr& headers, bool) {
        EXPECT_EQ(host_, headers->Host()->value().getStringView());
        EXPECT_EQ("/", headers->Path()->value().getStringView());
        EXPECT_EQ(Http::Headers::get().MethodValues.Get,
                  headers->Method()->value().getStringView());
      }));
  if (quic_version_.transport_version == quic::QUIC_VERSION_99) {
    quic_stream_.OnHeadersDecoded(headers_);
  } else {
    quic_stream_.OnStreamHeaderList(/*fin=*/false, headers_.uncompressed_header_bytes(), headers_);
  }
  EXPECT_TRUE(quic_stream_.FinishedReadingHeaders());

  EXPECT_CALL(stream_decoder_, decodeData(_, _))
      .WillOnce(Invoke([this](Buffer::Instance& buffer, bool finished_reading) {
        EXPECT_EQ(request_body_, buffer.toString());
        EXPECT_TRUE(finished_reading);
      }));
  std::string data = request_body_;
  if (quic_version_.transport_version == quic::QUIC_VERSION_99) {
    std::unique_ptr<char[]> data_buffer;
    quic::HttpEncoder encoder;
    quic::QuicByteCount data_frame_header_length =
        encoder.SerializeDataFrameHeader(request_body_.length(), &data_buffer);
    quic::QuicStringPiece data_frame_header(data_buffer.get(), data_frame_header_length);
    data = absl::StrCat(data_frame_header, request_body_);
  }
  quic::QuicStreamFrame frame(stream_id_, true, 0, data);
  quic_stream_.OnStreamFrame(frame);
}

TEST_P(EnvoyQuicServerStreamTest, DecodeHeadersBodyAndTrailers) {
  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([this](const Http::HeaderMapPtr& headers, bool) {
        EXPECT_EQ(host_, headers->Host()->value().getStringView());
        EXPECT_EQ("/", headers->Path()->value().getStringView());
        EXPECT_EQ(Http::Headers::get().MethodValues.Get,
                  headers->Method()->value().getStringView());
      }));
  quic_stream_.OnStreamHeaderList(/*fin=*/false, headers_.uncompressed_header_bytes(), headers_);
  EXPECT_TRUE(quic_stream_.FinishedReadingHeaders());

  std::string data = request_body_;
  if (quic_version_.transport_version == quic::QUIC_VERSION_99) {
    std::unique_ptr<char[]> data_buffer;
    quic::HttpEncoder encoder;
    quic::QuicByteCount data_frame_header_length =
        encoder.SerializeDataFrameHeader(request_body_.length(), &data_buffer);
    quic::QuicStringPiece data_frame_header(data_buffer.get(), data_frame_header_length);
    data = absl::StrCat(data_frame_header, request_body_);
  }
  quic::QuicStreamFrame frame(stream_id_, false, 0, data);
  EXPECT_CALL(stream_decoder_, decodeData(_, _))
      .Times(testing::AtMost(2))
      .WillOnce(Invoke([this](Buffer::Instance& buffer, bool finished_reading) {
        EXPECT_EQ(request_body_, buffer.toString());
        EXPECT_FALSE(finished_reading);
      }))
      // Depends on QUIC version, there may be an empty STREAM_FRAME with FIN. But
      // since there is trailers, finished_reading should always be false.
      .WillOnce(Invoke([](Buffer::Instance& buffer, bool finished_reading) {
        EXPECT_FALSE(finished_reading);
        EXPECT_EQ(0, buffer.length());
      }));
  quic_stream_.OnStreamFrame(frame);

  EXPECT_CALL(stream_decoder_, decodeTrailers_(_))
      .WillOnce(Invoke([](const Http::HeaderMapPtr& headers) {
        Http::LowerCaseString key1("key1");
        Http::LowerCaseString key2(":final-offset");
        EXPECT_EQ("value1", headers->get(key1)->value().getStringView());
        EXPECT_EQ(nullptr, headers->get(key2));
      }));
  quic_stream_.OnStreamHeaderList(/*fin=*/true, trailers_.uncompressed_header_bytes(), trailers_);
}

TEST_P(EnvoyQuicServerStreamTest, OutOfOrderTrailers) {
  if (quic::VersionUsesQpack(quic_version_.transport_version)) {
    return;
  }
  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([this](const Http::HeaderMapPtr& headers, bool) {
        EXPECT_EQ(host_, headers->Host()->value().getStringView());
        EXPECT_EQ("/", headers->Path()->value().getStringView());
        EXPECT_EQ(Http::Headers::get().MethodValues.Get,
                  headers->Method()->value().getStringView());
      }));
  quic_stream_.OnStreamHeaderList(/*fin=*/false, headers_.uncompressed_header_bytes(), headers_);
  EXPECT_TRUE(quic_stream_.FinishedReadingHeaders());

  // Trailer should be delivered to HCM later after body arrives.
  quic_stream_.OnStreamHeaderList(/*fin=*/true, trailers_.uncompressed_header_bytes(), trailers_);

  std::string data = request_body_;
  if (quic_version_.transport_version == quic::QUIC_VERSION_99) {
    std::unique_ptr<char[]> data_buffer;
    quic::HttpEncoder encoder;
    quic::QuicByteCount data_frame_header_length =
        encoder.SerializeDataFrameHeader(request_body_.length(), &data_buffer);
    quic::QuicStringPiece data_frame_header(data_buffer.get(), data_frame_header_length);
    data = absl::StrCat(data_frame_header, request_body_);
  }
  quic::QuicStreamFrame frame(stream_id_, false, 0, data);
  EXPECT_CALL(stream_decoder_, decodeData(_, _))
      .Times(testing::AtMost(2))
      .WillOnce(Invoke([this](Buffer::Instance& buffer, bool finished_reading) {
        EXPECT_EQ(request_body_, buffer.toString());
        EXPECT_FALSE(finished_reading);
      }))
      // Depends on QUIC version, there may be an empty STREAM_FRAME with FIN. But
      // since there is trailers, finished_reading should always be false.
      .WillOnce(Invoke([](Buffer::Instance& buffer, bool finished_reading) {
        EXPECT_FALSE(finished_reading);
        EXPECT_EQ(0, buffer.length());
      }));

  EXPECT_CALL(stream_decoder_, decodeTrailers_(_))
      .WillOnce(Invoke([](const Http::HeaderMapPtr& headers) {
        Http::LowerCaseString key1("key1");
        Http::LowerCaseString key2(":final-offset");
        EXPECT_EQ("value1", headers->get(key1)->value().getStringView());
        EXPECT_EQ(nullptr, headers->get(key2));
      }));
  quic_stream_.OnStreamFrame(frame);
}

} // namespace Quic
} // namespace Envoy
