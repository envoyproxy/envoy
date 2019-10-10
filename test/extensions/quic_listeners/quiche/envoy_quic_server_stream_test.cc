#include "extensions/quic_listeners/quiche/envoy_quic_server_stream.h"

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/core/http/quic_server_session_base.h"
#include "quiche/quic/test_tools/quic_test_utils.h"
#include "quiche/quic/core/quic_utils.h"

#pragma GCC diagnostic pop

#include <string>

#include "common/event/libevent_scheduler.h"
#include "common/http/headers.h"
#include "test/test_common/utility.h"
#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"
#include "extensions/quic_listeners/quiche/envoy_quic_server_session.h"
#include "extensions/quic_listeners/quiche/envoy_quic_server_connection.h"
#include "extensions/quic_listeners/quiche/envoy_quic_connection_helper.h"
#include "test/test_common/utility.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Quic {

class MockQuicServerSession : public EnvoyQuicServerSession {
public:
  MockQuicServerSession(const quic::QuicConfig& config,
                        const quic::ParsedQuicVersionVector& supported_versions,
                        std::unique_ptr<EnvoyQuicServerConnection> connection,
                        quic::QuicSession::Visitor* visitor,
                        quic::QuicCryptoServerStream::Helper* helper,
                        const quic::QuicCryptoServerConfig* crypto_config,
                        quic::QuicCompressedCertsCache* compressed_certs_cache,
                        Event::Dispatcher& dispatcher, uint32_t send_buffer_limit)
      : EnvoyQuicServerSession(config, supported_versions, std::move(connection), visitor, helper,
                               crypto_config, compressed_certs_cache, dispatcher,
                               send_buffer_limit) {}

  MOCK_METHOD1(CreateIncomingStream, quic::QuicSpdyStream*(quic::QuicStreamId id));
  MOCK_METHOD1(CreateIncomingStream, quic::QuicSpdyStream*(quic::PendingStream* pending));
  MOCK_METHOD0(CreateOutgoingBidirectionalStream, quic::QuicSpdyStream*());
  MOCK_METHOD0(CreateOutgoingUnidirectionalStream, quic::QuicSpdyStream*());
  MOCK_METHOD1(ShouldCreateIncomingStream, bool(quic::QuicStreamId id));
  MOCK_METHOD0(ShouldCreateOutgoingBidirectionalStream, bool());
  MOCK_METHOD0(ShouldCreateOutgoingUnidirectionalStream, bool());
  MOCK_METHOD1(ShouldYield, bool(quic::QuicStreamId stream_id));
  MOCK_METHOD5(WritevData,
               quic::QuicConsumedData(quic::QuicStream* stream, quic::QuicStreamId id,
                                      size_t write_length, quic::QuicStreamOffset offset,
                                      quic::StreamSendingState state));

  quic::QuicCryptoServerStream*
  CreateQuicCryptoServerStream(const quic::QuicCryptoServerConfig* crypto_config,
                               quic::QuicCompressedCertsCache* compressed_certs_cache) override {
    return new quic::QuicCryptoServerStream(crypto_config, compressed_certs_cache, this,
                                            stream_helper());
  }

  using quic::QuicServerSessionBase::ActivateStream;
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
        quic_connection_(new EnvoyQuicServerConnection(
            quic::test::TestConnectionId(),
            quic::QuicSocketAddress(quic::QuicIpAddress::Any6(), 12345), connection_helper_,
            alarm_factory_, &writer_,
            /*owns_writer=*/false, {quic_version_}, listener_config_, listener_stats_)),
        quic_session_(quic_config_, {quic_version_},
                      std::unique_ptr<EnvoyQuicServerConnection>(quic_connection_),
                      /*visitor=*/nullptr,
                      /*helper=*/nullptr, /*crypto_config=*/nullptr,
                      /*compressed_certs_cache=*/nullptr, *dispatcher_,
                      quic_config_.GetInitialStreamFlowControlWindowToSend()),
        stream_id_(quic_version_.transport_version == quic::QUIC_VERSION_99 ? 4u : 5u),
        quic_stream_(new EnvoyQuicServerStream(stream_id_, &quic_session_, quic::BIDIRECTIONAL)),
        response_headers_{{":status", "200"}} {
    quic::SetVerbosityLogThreshold(3);
    quic_stream_->setDecoder(stream_decoder_);
    quic_stream_->addCallbacks(stream_callbacks_);
    quic_session_.ActivateStream(std::unique_ptr<EnvoyQuicServerStream>(quic_stream_));
    EXPECT_CALL(quic_session_, ShouldYield(_)).WillRepeatedly(Return(false));
    EXPECT_CALL(quic_session_, WritevData(_, _, _, _, _))
        .WillRepeatedly(Invoke([](quic::QuicStream*, quic::QuicStreamId, size_t write_length,
                                  quic::QuicStreamOffset, quic::StreamSendingState) {
          return quic::QuicConsumedData{write_length, true};
        }));
    EXPECT_CALL(writer_, WritePacket(_, _, _, _, _))
        .WillRepeatedly(Invoke([](const char*, size_t buf_len, const quic::QuicIpAddress&,
                                  const quic::QuicSocketAddress&, quic::PerPacketOptions*) {
          return quic::WriteResult{quic::WRITE_STATUS_OK, static_cast<int>(buf_len)};
        }));
  }

  void SetUp() override {
    quic_session_.Initialize();
    request_headers_.OnHeaderBlockStart();
    request_headers_.OnHeader(":authority", host_);
    request_headers_.OnHeader(":method", "GET");
    request_headers_.OnHeader(":path", "/");
    request_headers_.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0,
                                      /*compressed_header_bytes=*/0);

    trailers_.OnHeaderBlockStart();
    trailers_.OnHeader("key1", "value1");
    if (quic_version_.transport_version != quic::QUIC_VERSION_99) {
      // ":final-offset" is required and stripped off by quic.
      trailers_.OnHeader(":final-offset", absl::StrCat("", request_body_.length()));
    }
    trailers_.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0, /*compressed_header_bytes=*/0);
  }

  void TearDown() override {
    if (quic_connection_->connected()) {
      quic_session_.close(Network::ConnectionCloseType::NoFlush);
    }
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
  EnvoyQuicServerConnection* quic_connection_;
  MockQuicServerSession quic_session_;
  quic::QuicStreamId stream_id_;
  EnvoyQuicServerStream* quic_stream_;
  Http::MockStreamDecoder stream_decoder_;
  Http::MockStreamCallbacks stream_callbacks_;
  quic::QuicHeaderList request_headers_;
  Http::TestHeaderMapImpl response_headers_;
  quic::QuicHeaderList trailers_;
  std::string host_{"www.abc.com"};
  std::string request_body_{"Hello world"};
};

INSTANTIATE_TEST_SUITE_P(EnvoyQuicServerStreamTests, EnvoyQuicServerStreamTest,
                         testing::ValuesIn({true, false}));

TEST_P(EnvoyQuicServerStreamTest, PostRequestAndResponse) {
  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([this](const Http::HeaderMapPtr& headers, bool) {
        EXPECT_EQ(host_, headers->Host()->value().getStringView());
        EXPECT_EQ("/", headers->Path()->value().getStringView());
        EXPECT_EQ(Http::Headers::get().MethodValues.Get,
                  headers->Method()->value().getStringView());
      }));
  if (quic_version_.transport_version == quic::QUIC_VERSION_99) {
    quic_stream_->OnHeadersDecoded(request_headers_);
  } else {
    quic_stream_->OnStreamHeaderList(/*fin=*/false, request_headers_.uncompressed_header_bytes(),
                                     request_headers_);
  }
  EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());

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
  quic_stream_->OnStreamFrame(frame);

  quic_stream_->encodeHeaders(response_headers_, /*end_stream=*/true);
}

TEST_P(EnvoyQuicServerStreamTest, DecodeHeadersBodyAndTrailers) {
  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([this](const Http::HeaderMapPtr& headers, bool) {
        EXPECT_EQ(host_, headers->Host()->value().getStringView());
        EXPECT_EQ("/", headers->Path()->value().getStringView());
        EXPECT_EQ(Http::Headers::get().MethodValues.Get,
                  headers->Method()->value().getStringView());
      }));
  quic_stream_->OnStreamHeaderList(/*fin=*/false, request_headers_.uncompressed_header_bytes(),
                                   request_headers_);
  EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());

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
  quic_stream_->OnStreamFrame(frame);

  EXPECT_CALL(stream_decoder_, decodeTrailers_(_))
      .WillOnce(Invoke([](const Http::HeaderMapPtr& headers) {
        Http::LowerCaseString key1("key1");
        Http::LowerCaseString key2(":final-offset");
        EXPECT_EQ("value1", headers->get(key1)->value().getStringView());
        EXPECT_EQ(nullptr, headers->get(key2));
      }));
  quic_stream_->OnStreamHeaderList(/*fin=*/true, trailers_.uncompressed_header_bytes(), trailers_);
  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
}

TEST_P(EnvoyQuicServerStreamTest, OutOfOrderTrailers) {
  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
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
  quic_stream_->OnStreamHeaderList(/*fin=*/false, request_headers_.uncompressed_header_bytes(),
                                   request_headers_);
  EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());

  // Trailer should be delivered to HCM later after body arrives.
  quic_stream_->OnStreamHeaderList(/*fin=*/true, trailers_.uncompressed_header_bytes(), trailers_);

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
  quic_stream_->OnStreamFrame(frame);
}

TEST_P(EnvoyQuicServerStreamTest, WatermarkSendBuffer) {
  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([this](const Http::HeaderMapPtr& headers, bool) {
        EXPECT_EQ(host_, headers->Host()->value().getStringView());
        EXPECT_EQ("/", headers->Path()->value().getStringView());
        EXPECT_EQ(Http::Headers::get().MethodValues.Get,
                  headers->Method()->value().getStringView());
      }));
  if (quic_version_.transport_version == quic::QUIC_VERSION_99) {
    quic_stream_->OnHeadersDecoded(request_headers_);
  } else {
    quic_stream_->OnStreamHeaderList(/*fin=*/false, request_headers_.uncompressed_header_bytes(),
                                     request_headers_);
  }
  EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());

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
  quic_stream_->OnStreamFrame(frame);

  response_headers_.addCopy(":content-length", "32770"); // 32KB + 2 byte
  quic_stream_->encodeHeaders(response_headers_, /*end_stream=*/false);
  // encode 32kB response body. first 16KB should be written out right away. The
  // rest should be buffered. The high watermark is 16KB, so this call should
  // make the send buffer reach its high watermark.
  std::string response(32 * 1024 + 1, 'a');
  Buffer::OwnedImpl buffer(response);
  EXPECT_CALL(stream_callbacks_, onAboveWriteBufferHighWatermark());
  quic_stream_->encodeData(buffer, false);

  EXPECT_EQ(0u, buffer.length());
  EXPECT_TRUE(quic_stream_->flow_controller()->IsBlocked());
  // Bump connection flow control window large enough not to cause connection
  // level flow control blocked.
  quic::QuicWindowUpdateFrame window_update(
      quic::kInvalidControlFrameId,
      quic::QuicUtils::GetInvalidStreamId(quic_version_.transport_version), 1024 * 1024);
  quic_session_.OnWindowUpdateFrame(window_update);

  // Receive a WINDOW_UPDATE frame not large enough to drain half of the send
  // buffer.
  quic::QuicWindowUpdateFrame window_update1(quic::kInvalidControlFrameId, quic_stream_->id(),
                                             16 * 1024 + 8 * 1024);
  quic_stream_->OnWindowUpdateFrame(window_update1);
  EXPECT_FALSE(quic_stream_->flow_controller()->IsBlocked());
  quic_session_.OnCanWrite();
  EXPECT_TRUE(quic_stream_->flow_controller()->IsBlocked());

  // Receive another WINDOW_UPDATE frame to drain the send buffer till below low
  // watermark.
  quic::QuicWindowUpdateFrame window_update2(quic::kInvalidControlFrameId, quic_stream_->id(),
                                             16 * 1024 + 8 * 1024 + 1024);
  quic_stream_->OnWindowUpdateFrame(window_update2);
  EXPECT_FALSE(quic_stream_->flow_controller()->IsBlocked());
  EXPECT_CALL(stream_callbacks_, onBelowWriteBufferLowWatermark()).WillOnce(Invoke([this]() {
    std::string rest_response(1, 'a');
    Buffer::OwnedImpl buffer(rest_response);
    quic_stream_->encodeData(buffer, true);
  }));
  quic_session_.OnCanWrite();
  EXPECT_TRUE(quic_stream_->flow_controller()->IsBlocked());

  quic::QuicWindowUpdateFrame window_update3(quic::kInvalidControlFrameId, quic_stream_->id(),
                                             32 * 1024 + 1024);
  quic_stream_->OnWindowUpdateFrame(window_update3);
  quic_session_.OnCanWrite();

  EXPECT_TRUE(quic_stream_->local_end_stream_);
  EXPECT_TRUE(quic_stream_->write_side_closed());
}

} // namespace Quic
} // namespace Envoy
