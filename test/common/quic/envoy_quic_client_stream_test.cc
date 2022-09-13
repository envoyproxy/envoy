#include "source/common/quic/envoy_quic_alarm_factory.h"
#include "source/common/quic/envoy_quic_client_connection.h"
#include "source/common/quic/envoy_quic_client_stream.h"
#include "source/common/quic/envoy_quic_connection_helper.h"
#include "source/common/quic/envoy_quic_utils.h"

#include "test/common/quic/test_utils.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/quic/core/crypto/null_encrypter.h"
#include "quiche/quic/core/deterministic_connection_id_generator.h"

namespace Envoy {
namespace Quic {

using testing::_;
using testing::Invoke;

class MockDelegate : public PacketsToReadDelegate {
public:
  MOCK_METHOD(size_t, numPacketsExpectedPerEventLoop, ());
};

class EnvoyQuicClientStreamTest : public testing::Test {
public:
  EnvoyQuicClientStreamTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")),
        connection_helper_(*dispatcher_),
        alarm_factory_(*dispatcher_, *connection_helper_.GetClock()),
        quic_version_(quic::CurrentSupportedHttp3Versions()[0]),
        peer_addr_(Network::Utility::getAddressWithPort(*Network::Utility::getIpv6LoopbackAddress(),
                                                        12345)),
        self_addr_(Network::Utility::getAddressWithPort(*Network::Utility::getIpv6LoopbackAddress(),
                                                        54321)),
        quic_connection_(new EnvoyQuicClientConnection(
            quic::test::TestConnectionId(), connection_helper_, alarm_factory_, &writer_,
            /*owns_writer=*/false, {quic_version_}, *dispatcher_,
            createConnectionSocket(peer_addr_, self_addr_, nullptr), connection_id_generator_)),
        quic_session_(quic_config_, {quic_version_},
                      std::unique_ptr<EnvoyQuicClientConnection>(quic_connection_), *dispatcher_,
                      quic_config_.GetInitialStreamFlowControlWindowToSend() * 2,
                      crypto_stream_factory_),
        stats_({ALL_HTTP3_CODEC_STATS(POOL_COUNTER_PREFIX(scope_, "http3."),
                                      POOL_GAUGE_PREFIX(scope_, "http3."))}),
        quic_stream_(new EnvoyQuicClientStream(stream_id_, &quic_session_, quic::BIDIRECTIONAL,
                                               stats_, http3_options_)),
        request_headers_{{":authority", host_}, {":method", "POST"}, {":path", "/"}},
        request_trailers_{{"trailer-key", "trailer-value"}} {
    quic_stream_->setResponseDecoder(stream_decoder_);
    quic_stream_->addCallbacks(stream_callbacks_);
    quic_session_.ActivateStream(std::unique_ptr<EnvoyQuicClientStream>(quic_stream_));
    EXPECT_CALL(quic_session_, ShouldYield(_)).WillRepeatedly(testing::Return(false));
    EXPECT_CALL(quic_session_, WritevData(_, _, _, _, _, _))
        .WillRepeatedly(
            Invoke([](quic::QuicStreamId, size_t write_length, quic::QuicStreamOffset,
                      quic::StreamSendingState state, bool, absl::optional<quic::EncryptionLevel>) {
              return quic::QuicConsumedData{write_length, state != quic::NO_FIN};
            }));
    EXPECT_CALL(writer_, WritePacket(_, _, _, _, _))
        .WillRepeatedly(Invoke([](const char*, size_t buf_len, const quic::QuicIpAddress&,
                                  const quic::QuicSocketAddress&, quic::PerPacketOptions*) {
          return quic::WriteResult{quic::WRITE_STATUS_OK, static_cast<int>(buf_len)};
        }));
  }

  void SetUp() override {
    quic_session_.Initialize();
    quic_connection_->setEnvoyConnection(quic_session_, quic_session_);
    quic_connection_->SetEncrypter(
        quic::ENCRYPTION_FORWARD_SECURE,
        std::make_unique<quic::NullEncrypter>(quic::Perspective::IS_CLIENT));
    quic_connection_->SetDefaultEncryptionLevel(quic::ENCRYPTION_FORWARD_SECURE);

    setQuicConfigWithDefaultValues(quic_session_.config());
    quic_session_.OnConfigNegotiated();
    quic_connection_->setUpConnectionSocket(*quic_connection_->connectionSocket(), delegate_);
    spdy_response_headers_[":status"] = "200";

    spdy_trailers_["key1"] = "value1";
  }

  void TearDown() override {
    if (quic_connection_->connected()) {
      quic_connection_->CloseConnection(
          quic::QUIC_NO_ERROR, "Closed by application",
          quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    }
  }

  size_t receiveResponse(const std::string& payload, bool fin, size_t offset = 0) {
    EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
        .WillOnce(Invoke([](const Http::ResponseHeaderMapPtr& headers, bool) {
          EXPECT_EQ("200", headers->getStatusValue());
        }));

    EXPECT_CALL(stream_decoder_, decodeData(_, _))
        .WillOnce(Invoke([&](Buffer::Instance& buffer, bool finished_reading) {
          EXPECT_EQ(payload, buffer.toString());
          EXPECT_EQ(fin, finished_reading);
        }));
    std::string data = absl::StrCat(spdyHeaderToHttp3StreamPayload(spdy_response_headers_),
                                    bodyToHttp3StreamPayload(payload));
    quic::QuicStreamFrame frame(stream_id_, fin, offset, data);
    quic_stream_->OnStreamFrame(frame);
    EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());
    return offset + data.length();
  }

  size_t receiveResponseHeaders(bool end_stream) {
    EXPECT_CALL(stream_decoder_, decodeHeaders_(_, end_stream))
        .WillOnce(Invoke([](const Http::ResponseHeaderMapPtr& headers, bool) {
          EXPECT_EQ("200", headers->getStatusValue());
        }));

    std::string data = spdyHeaderToHttp3StreamPayload(spdy_response_headers_);
    quic::QuicStreamFrame frame(stream_id_, end_stream, 0, data);
    quic_stream_->OnStreamFrame(frame);
    EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());
    return data.length();
  }

protected:
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  EnvoyQuicConnectionHelper connection_helper_;
  EnvoyQuicAlarmFactory alarm_factory_;
  testing::NiceMock<quic::test::MockPacketWriter> writer_;
  quic::ParsedQuicVersion quic_version_;
  quic::QuicConfig quic_config_;
  Network::Address::InstanceConstSharedPtr peer_addr_;
  Network::Address::InstanceConstSharedPtr self_addr_;
  MockDelegate delegate_;
  quic::DeterministicConnectionIdGenerator connection_id_generator_{
      quic::kQuicDefaultConnectionIdLength};
  EnvoyQuicClientConnection* quic_connection_;
  TestQuicCryptoClientStreamFactory crypto_stream_factory_;
  MockEnvoyQuicClientSession quic_session_;
  quic::QuicStreamId stream_id_{4u};
  Stats::IsolatedStoreImpl scope_;
  Http::Http3::CodecStats stats_;
  envoy::config::core::v3::Http3ProtocolOptions http3_options_;
  EnvoyQuicClientStream* quic_stream_;
  Http::MockResponseDecoder stream_decoder_;
  Http::MockStreamCallbacks stream_callbacks_;
  std::string host_{"www.abc.com"};
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  spdy::Http2HeaderBlock spdy_response_headers_;
  spdy::Http2HeaderBlock spdy_trailers_;
  Buffer::OwnedImpl request_body_{"Hello world"};
  std::string response_body_{"OK\n"};
};

TEST_F(EnvoyQuicClientStreamTest, GetRequestAndHeaderOnlyResponse) {
  const auto result = quic_stream_->encodeHeaders(request_headers_, /*end_stream=*/true);
  EXPECT_TRUE(result.ok());

  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([](const Http::ResponseHeaderMapPtr& headers, bool) {
        EXPECT_EQ("200", headers->getStatusValue());
      }));
  EXPECT_CALL(stream_decoder_, decodeData(BufferStringEqual(""), /*end_stream=*/true));
  std::string payload = spdyHeaderToHttp3StreamPayload(spdy_response_headers_);
  quic::QuicStreamFrame frame(stream_id_, true, 0, payload);
  quic_stream_->OnStreamFrame(frame);
  EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());
}

TEST_F(EnvoyQuicClientStreamTest, PostRequestAndResponse) {
  EXPECT_EQ(absl::nullopt, quic_stream_->http1StreamEncoderOptions());
  const auto result = quic_stream_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(result.ok());
  quic_stream_->encodeData(request_body_, false);
  quic_stream_->encodeTrailers(request_trailers_);

  size_t offset = receiveResponse(response_body_, false);
  EXPECT_CALL(stream_decoder_, decodeTrailers_(_))
      .WillOnce(Invoke([](const Http::ResponseTrailerMapPtr& headers) {
        Http::LowerCaseString key1("key1");
        Http::LowerCaseString key2(":final-offset");
        EXPECT_EQ("value1", headers->get(key1)[0]->value().getStringView());
        EXPECT_TRUE(headers->get(key2).empty());
      }));
  std::string more_response_body{"bbb"};
  EXPECT_CALL(stream_decoder_, decodeData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool finished_reading) {
        EXPECT_EQ(more_response_body, buffer.toString());
        EXPECT_EQ(false, finished_reading);
      }));
  std::string payload = absl::StrCat(bodyToHttp3StreamPayload(more_response_body),
                                     spdyHeaderToHttp3StreamPayload(spdy_trailers_));
  quic::QuicStreamFrame frame(stream_id_, true, offset, payload);
  quic_stream_->OnStreamFrame(frame);
}

TEST_F(EnvoyQuicClientStreamTest, PostRequestAndResponseWithAccounting) {
  EXPECT_EQ(absl::nullopt, quic_stream_->http1StreamEncoderOptions());
  EXPECT_EQ(0, quic_stream_->bytesMeter()->wireBytesSent());
  EXPECT_EQ(0, quic_stream_->bytesMeter()->headerBytesSent());
  const auto result = quic_stream_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(quic_stream_->stream_bytes_written(), quic_stream_->bytesMeter()->wireBytesSent());
  EXPECT_EQ(quic_stream_->stream_bytes_written(), quic_stream_->bytesMeter()->headerBytesSent());

  uint64_t body_bytes = quic_stream_->stream_bytes_written();
  quic_stream_->encodeData(request_body_, false);
  body_bytes = quic_stream_->stream_bytes_written() - body_bytes;
  EXPECT_EQ(quic_stream_->stream_bytes_written(), quic_stream_->bytesMeter()->wireBytesSent());
  EXPECT_EQ(quic_stream_->stream_bytes_written() - body_bytes,
            quic_stream_->bytesMeter()->headerBytesSent());
  quic_stream_->encodeTrailers(request_trailers_);
  EXPECT_EQ(quic_stream_->stream_bytes_written(), quic_stream_->bytesMeter()->wireBytesSent());
  EXPECT_EQ(quic_stream_->stream_bytes_written() - body_bytes,
            quic_stream_->bytesMeter()->headerBytesSent());

  EXPECT_EQ(0, quic_stream_->bytesMeter()->wireBytesReceived());
  EXPECT_EQ(0, quic_stream_->bytesMeter()->headerBytesReceived());

  size_t offset = receiveResponseHeaders(false);
  // Received header bytes do not include the HTTP/3 frame overhead.
  EXPECT_EQ(quic_stream_->stream_bytes_read() - 2,
            quic_stream_->bytesMeter()->headerBytesReceived());
  EXPECT_EQ(quic_stream_->stream_bytes_read(), quic_stream_->bytesMeter()->wireBytesReceived());
  EXPECT_CALL(stream_decoder_, decodeTrailers_(_))
      .WillOnce(Invoke([](const Http::ResponseTrailerMapPtr& headers) {
        Http::LowerCaseString key1("key1");
        Http::LowerCaseString key2(":final-offset");
        EXPECT_EQ("value1", headers->get(key1)[0]->value().getStringView());
        EXPECT_TRUE(headers->get(key2).empty());
      }));
  std::string more_response_body{"bbb"};
  EXPECT_CALL(stream_decoder_, decodeData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool finished_reading) {
        EXPECT_EQ(more_response_body, buffer.toString());
        EXPECT_EQ(false, finished_reading);
      }));
  std::string payload = absl::StrCat(bodyToHttp3StreamPayload(more_response_body),
                                     spdyHeaderToHttp3StreamPayload(spdy_trailers_));
  quic::QuicStreamFrame frame(stream_id_, true, offset, payload);
  quic_stream_->OnStreamFrame(frame);
  EXPECT_EQ(quic_stream_->stream_bytes_read() - 4 -
                bodyToHttp3StreamPayload(more_response_body).length(),
            quic_stream_->bytesMeter()->headerBytesReceived());
  EXPECT_EQ(quic_stream_->stream_bytes_read(), quic_stream_->bytesMeter()->wireBytesReceived());
}

TEST_F(EnvoyQuicClientStreamTest, PostRequestAnd1xx) {
  const auto result = quic_stream_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(result.ok());

  EXPECT_CALL(stream_decoder_, decode1xxHeaders_(_))
      .WillOnce(Invoke([this](const Http::ResponseHeaderMapPtr& headers) {
        EXPECT_EQ("100", headers->getStatusValue());
        EXPECT_EQ("0", headers->get(Http::LowerCaseString("i"))[0]->value().getStringView());
        quic_stream_->encodeData(request_body_, true);
      }));
  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([](const Http::ResponseHeaderMapPtr& headers, bool) {
        EXPECT_EQ("199", headers->getStatusValue());
        EXPECT_EQ("1", headers->get(Http::LowerCaseString("i"))[0]->value().getStringView());
      }));
  size_t offset = 0;
  size_t i = 0;
  // Receive several 10x headers, only the first 100 Continue header should be
  // delivered.
  for (const std::string& status : {"100", "199", "100"}) {
    spdy::Http2HeaderBlock continue_header;
    continue_header[":status"] = status;
    continue_header["i"] = absl::StrCat("", i++);
    std::string data = spdyHeaderToHttp3StreamPayload(continue_header);
    quic::QuicStreamFrame frame(stream_id_, false, offset, data);
    quic_stream_->OnStreamFrame(frame);
    offset += data.length();
  }

  receiveResponse(response_body_, true, offset);
}

TEST_F(EnvoyQuicClientStreamTest, ResetUpon101SwitchProtocol) {
  const auto result = quic_stream_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(result.ok());

  EXPECT_CALL(stream_callbacks_, onResetStream(Http::StreamResetReason::ProtocolError, _));
  // Receive several 10x headers, only the first 100 Continue header should be
  // delivered.
  spdy::Http2HeaderBlock continue_header;
  continue_header[":status"] = "101";
  std::string data = spdyHeaderToHttp3StreamPayload(continue_header);
  quic::QuicStreamFrame frame(stream_id_, false, 0u, data);
  quic_stream_->OnStreamFrame(frame);
}

TEST_F(EnvoyQuicClientStreamTest, WatermarkSendBuffer) {
  // Bump connection flow control window large enough not to cause connection
  // level flow control blocked.
  quic::QuicWindowUpdateFrame window_update(
      quic::kInvalidControlFrameId,
      quic::QuicUtils::GetInvalidStreamId(quic_version_.transport_version), 1024 * 1024);
  quic_session_.OnWindowUpdateFrame(window_update);

  request_headers_.addCopy(":content-length", "32770"); // 32KB + 2 byte
  const auto result = quic_stream_->encodeHeaders(request_headers_, /*end_stream=*/false);
  EXPECT_TRUE(result.ok());
  // Encode 32kB request body. first 16KB should be written out right away. The
  // rest should be buffered. The high watermark is 16KB, so this call should
  // make the send buffer reach its high watermark.
  std::string request(32 * 1024 + 1, 'a');
  Buffer::OwnedImpl buffer(request);
  EXPECT_CALL(stream_callbacks_, onAboveWriteBufferHighWatermark());
  quic_stream_->encodeData(buffer, false);

  EXPECT_EQ(0u, buffer.length());
  EXPECT_TRUE(quic_stream_->IsFlowControlBlocked());

  // Receive a WINDOW_UPDATE frame not large enough to drain half of the send
  // buffer.
  quic::QuicWindowUpdateFrame window_update1(quic::kInvalidControlFrameId, quic_stream_->id(),
                                             16 * 1024 + 8 * 1024);
  quic_stream_->OnWindowUpdateFrame(window_update1);
  EXPECT_FALSE(quic_stream_->IsFlowControlBlocked());
  quic_session_.OnCanWrite();
  EXPECT_TRUE(quic_stream_->IsFlowControlBlocked());

  // Receive another WINDOW_UPDATE frame to drain the send buffer till below low
  // watermark.
  quic::QuicWindowUpdateFrame window_update2(quic::kInvalidControlFrameId, quic_stream_->id(),
                                             16 * 1024 + 8 * 1024 + 1024);
  quic_stream_->OnWindowUpdateFrame(window_update2);
  EXPECT_FALSE(quic_stream_->IsFlowControlBlocked());
  EXPECT_CALL(stream_callbacks_, onBelowWriteBufferLowWatermark()).WillOnce(Invoke([this]() {
    std::string rest_request(1, 'a');
    Buffer::OwnedImpl buffer(rest_request);
    quic_stream_->encodeData(buffer, true);
  }));
  quic_session_.OnCanWrite();
  EXPECT_TRUE(quic_stream_->IsFlowControlBlocked());

  quic::QuicWindowUpdateFrame window_update3(quic::kInvalidControlFrameId, quic_stream_->id(),
                                             32 * 1024 + 1024);
  quic_stream_->OnWindowUpdateFrame(window_update3);
  quic_session_.OnCanWrite();

  EXPECT_TRUE(quic_stream_->local_end_stream_);
  EXPECT_TRUE(quic_stream_->write_side_closed());
  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
}

// Tests that headers and trailers buffered in send buffer contribute towards buffer watermark
// limits.
TEST_F(EnvoyQuicClientStreamTest, HeadersContributeToWatermark) {
  // Bump connection flow control window large enough not to cause connection level flow control
  // blocked
  quic::QuicWindowUpdateFrame window_update(
      quic::kInvalidControlFrameId,
      quic::QuicUtils::GetInvalidStreamId(quic_version_.transport_version), 1024 * 1024);
  quic_session_.OnWindowUpdateFrame(window_update);

  // Make the stream blocked by congestion control.
  EXPECT_CALL(quic_session_, WritevData(_, _, _, _, _, _))
      .WillOnce(
          Invoke([](quic::QuicStreamId, size_t /*write_length*/, quic::QuicStreamOffset,
                    quic::StreamSendingState state, bool, absl::optional<quic::EncryptionLevel>) {
            return quic::QuicConsumedData{0u, state != quic::NO_FIN};
          }));
  const auto result = quic_stream_->encodeHeaders(request_headers_, /*end_stream=*/false);
  EXPECT_TRUE(result.ok());

  // Encode 16kB -10 bytes request body. Because the high watermark is 16KB, with previously
  // buffered headers, this call should make the send buffers reach their high watermark.
  std::string request(16 * 1024 - 10, 'a');
  Buffer::OwnedImpl buffer(request);
  EXPECT_CALL(stream_callbacks_, onAboveWriteBufferHighWatermark());
  quic_stream_->encodeData(buffer, false);
  EXPECT_EQ(0u, buffer.length());

  // Unblock writing now, and this will write out 16kB data and cause stream to
  // be blocked by the flow control limit.
  EXPECT_CALL(quic_session_, WritevData(_, _, _, _, _, _))
      .WillOnce(
          Invoke([](quic::QuicStreamId, size_t write_length, quic::QuicStreamOffset,
                    quic::StreamSendingState state, bool, absl::optional<quic::EncryptionLevel>) {
            return quic::QuicConsumedData{write_length, state != quic::NO_FIN};
          }));
  EXPECT_CALL(stream_callbacks_, onBelowWriteBufferLowWatermark());
  quic_session_.OnCanWrite();
  EXPECT_TRUE(quic_stream_->IsFlowControlBlocked());

  // Update flow control window to write all the buffered data.
  quic::QuicWindowUpdateFrame window_update1(quic::kInvalidControlFrameId, quic_stream_->id(),
                                             32 * 1024);
  quic_stream_->OnWindowUpdateFrame(window_update1);
  EXPECT_CALL(quic_session_, WritevData(_, _, _, _, _, _))
      .WillOnce(
          Invoke([](quic::QuicStreamId, size_t write_length, quic::QuicStreamOffset,
                    quic::StreamSendingState state, bool, absl::optional<quic::EncryptionLevel>) {
            return quic::QuicConsumedData{write_length, state != quic::NO_FIN};
          }));
  quic_session_.OnCanWrite();
  // No data should be buffered at this point.

  EXPECT_CALL(quic_session_, WritevData(_, _, _, _, _, _))
      .WillOnce(
          Invoke([](quic::QuicStreamId, size_t, quic::QuicStreamOffset,
                    quic::StreamSendingState state, bool, absl::optional<quic::EncryptionLevel>) {
            return quic::QuicConsumedData{0u, state != quic::NO_FIN};
          }));
  // Send more data. If watermark bytes counting were not cleared in previous
  // OnCanWrite, this write would have caused the stream to exceed its high watermark.
  std::string request1(16 * 1024 - 3, 'a');
  Buffer::OwnedImpl buffer1(request1);
  quic_stream_->encodeData(buffer1, false);
  // Buffering more trailers will cause stream to reach high watermark, but
  // because trailers closes the stream, no callback should be triggered.
  quic_stream_->encodeTrailers(request_trailers_);

  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
}

TEST_F(EnvoyQuicClientStreamTest, ResetStream) {
  EXPECT_CALL(stream_callbacks_, onResetStream(Http::StreamResetReason::ConnectionFailure, _));
  quic_stream_->resetStream(Http::StreamResetReason::ConnectionFailure);
  EXPECT_TRUE(quic_stream_->rst_sent());
}

TEST_F(EnvoyQuicClientStreamTest, ReceiveResetStreamWriteClosed) {
  auto result = quic_stream_->encodeHeaders(request_headers_, true);
  EXPECT_TRUE(result.ok());
  EXPECT_CALL(stream_callbacks_, onResetStream(Http::StreamResetReason::RemoteReset, _));
  quic_stream_->OnStreamReset(quic::QuicRstStreamFrame(
      quic::kInvalidControlFrameId, quic_stream_->id(), quic::QUIC_STREAM_NO_ERROR, 0));
  EXPECT_TRUE(quic_stream_->rst_received());
}

TEST_F(EnvoyQuicClientStreamTest, ReceiveResetStreamWriteOpen) {
  quic_stream_->OnStreamReset(quic::QuicRstStreamFrame(
      quic::kInvalidControlFrameId, quic_stream_->id(), quic::QUIC_STREAM_NO_ERROR, 0));
  EXPECT_TRUE(quic_stream_->rst_received());
  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
}

TEST_F(EnvoyQuicClientStreamTest, CloseConnectionDuringDecodingHeader) {
  const auto result = quic_stream_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(result.ok());
  quic_stream_->encodeData(request_body_, true);

  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([this](const Http::ResponseHeaderMapPtr&, bool) {
        quic_connection_->CloseConnection(
            quic::QUIC_NO_ERROR, "Closed in decodeHeaders",
            quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      }));
  // onResetStream() callback should be triggered because end_stream is
  // not decoded with header.
  EXPECT_CALL(stream_callbacks_, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  std::string data = spdyHeaderToHttp3StreamPayload(spdy_response_headers_);
  quic::QuicStreamFrame frame(stream_id_, true, 0, data);
  quic_stream_->OnStreamFrame(frame);
}

TEST_F(EnvoyQuicClientStreamTest, CloseConnectionDuringDecodingDataWithEndStream) {
  const auto result = quic_stream_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(result.ok());
  quic_stream_->encodeData(request_body_, true);

  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false));
  EXPECT_CALL(stream_decoder_, decodeData(_, true))
      .WillOnce(Invoke([this](Buffer::Instance&, bool) {
        // onResetStream() callback shouldn't be triggered.
        quic_connection_->CloseConnection(
            quic::QUIC_NO_ERROR, "Closed in decodeDdata",
            quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      }));
  std::string data = absl::StrCat(spdyHeaderToHttp3StreamPayload(spdy_response_headers_),
                                  bodyToHttp3StreamPayload(response_body_));
  quic::QuicStreamFrame frame(stream_id_, true, 0, data);
  quic_stream_->OnStreamFrame(frame);
}

TEST_F(EnvoyQuicClientStreamTest, CloseConnectionDuringDecodingDataWithTrailer) {
  const auto result = quic_stream_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(result.ok());
  quic_stream_->encodeData(request_body_, true);

  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false));
  EXPECT_CALL(stream_decoder_, decodeData(_, false))
      .WillOnce(Invoke([this](Buffer::Instance&, bool) {
        // onResetStream() and decodeTrailers() shouldn't be triggered.
        quic_connection_->CloseConnection(
            quic::QUIC_NO_ERROR, "Closed in decodeDdata",
            quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
        EXPECT_TRUE(quic_stream_->read_side_closed());
      }));
  EXPECT_CALL(stream_callbacks_, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  std::string data = absl::StrCat(spdyHeaderToHttp3StreamPayload(spdy_response_headers_),
                                  bodyToHttp3StreamPayload(response_body_),
                                  spdyHeaderToHttp3StreamPayload(spdy_trailers_));
  quic::QuicStreamFrame frame(stream_id_, true, 0, data);
  quic_stream_->OnStreamFrame(frame);
}

TEST_F(EnvoyQuicClientStreamTest, CloseConnectionDuringDecodingTrailer) {
  const auto result = quic_stream_->encodeHeaders(request_headers_, true);
  EXPECT_TRUE(result.ok());

  size_t offset = receiveResponse(response_body_, false);
  EXPECT_CALL(stream_decoder_, decodeTrailers_(_))
      .WillOnce(Invoke([this](const Http::ResponseTrailerMapPtr&) {
        // onResetStream() callback shouldn't be triggered.
        quic_connection_->CloseConnection(
            quic::QUIC_NO_ERROR, "Closed in decodeTrailers",
            quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      }));
  std::string payload = spdyHeaderToHttp3StreamPayload(spdy_trailers_);
  quic::QuicStreamFrame frame(stream_id_, true, offset, payload);
  quic_stream_->OnStreamFrame(frame);
}

TEST_F(EnvoyQuicClientStreamTest, MetadataNotSupported) {
  Http::MetadataMap metadata_map = {{"key", "value"}};
  Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  Http::MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
  quic_stream_->encodeMetadata(metadata_map_vector);
  EXPECT_EQ(1, TestUtility::findCounter(scope_, "http3.metadata_not_supported_error")->value());
  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
}

// Tests that posted stream block callback won't cause use-after-free crash.
TEST_F(EnvoyQuicClientStreamTest, ReadDisabledBeforeClose) {
  const auto result = quic_stream_->encodeHeaders(request_headers_, /*end_stream=*/true);
  EXPECT_TRUE(result.ok());

  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([this](const Http::ResponseHeaderMapPtr& headers, bool) {
        EXPECT_EQ("200", headers->getStatusValue());
        quic_stream_->readDisable(true);
      }));
  EXPECT_CALL(stream_decoder_, decodeData(BufferStringEqual(""), /*end_stream=*/true));
  std::string payload = spdyHeaderToHttp3StreamPayload(spdy_response_headers_);
  quic::QuicStreamFrame frame(stream_id_, true, 0, payload);
  quic_stream_->OnStreamFrame(frame);

  // Reset to close the stream.
  EXPECT_CALL(stream_callbacks_, onResetStream(Http::StreamResetReason::LocalReset, _));
  quic_stream_->resetStream(Http::StreamResetReason::LocalReset);
  EXPECT_EQ(1u, quic_session_.closed_streams()->size());
  quic_session_.closed_streams()->clear();
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(EnvoyQuicClientStreamTest, MaxIncomingHeadersCount) {
  quic_session_.setMaxIncomingHeadersCount(100);
  const auto result = quic_stream_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(result.ok());
  quic_stream_->encodeData(request_body_, true);

  // Receive more response headers than allowed. Such response headers shouldn't be delivered to
  // stream decoder.
  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, _)).Times(0u);
  EXPECT_CALL(stream_callbacks_, onResetStream(Http::StreamResetReason::LocalReset, _));
  for (size_t i = 0; i < 101; ++i) {
    spdy_response_headers_[absl::StrCat("key", i)] = absl::StrCat("value", i);
  }
  std::string data = absl::StrCat(spdyHeaderToHttp3StreamPayload(spdy_response_headers_),
                                  bodyToHttp3StreamPayload(response_body_),
                                  spdyHeaderToHttp3StreamPayload(spdy_trailers_));
  quic::QuicStreamFrame frame(stream_id_, true, 0, data);
  quic_stream_->OnStreamFrame(frame);
}

TEST_F(EnvoyQuicClientStreamTest, EncodeHeadersOnClosedStream) {
  // Reset stream should clear the connection level buffered bytes accounting.
  EXPECT_CALL(stream_callbacks_,
              onResetStream(Http::StreamResetReason::LocalRefusedStreamReset, _));
  quic_stream_->resetStream(Http::StreamResetReason::LocalRefusedStreamReset);

  const auto result = quic_stream_->encodeHeaders(request_headers_, false);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(0u, quic_session_.bytesToSend());
}

TEST_F(EnvoyQuicClientStreamTest, EncodeDataOnClosedStream) {
  const auto result = quic_stream_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(result.ok());

  // Encode 18kB response body. first 16KB should be written out right away. The
  // rest should be buffered.
  std::string body(18 * 1024, 'a');
  Buffer::OwnedImpl buffer(body);
  quic_stream_->encodeData(buffer, false);
  EXPECT_LT(0u, quic_session_.bytesToSend());

  // Reset stream should clear the connection level buffered bytes accounting.
  EXPECT_CALL(stream_callbacks_,
              onResetStream(Http::StreamResetReason::LocalRefusedStreamReset, _));
  quic_stream_->resetStream(Http::StreamResetReason::LocalRefusedStreamReset);

  // Try to send more data on the closed stream. And the watermark shouldn't be
  // messed up.
  std::string body2(1024, 'a');
  Buffer::OwnedImpl buffer2(body2);
  EXPECT_ENVOY_BUG(quic_stream_->encodeData(buffer2, true),
                   "encodeData is called on write-closed stream");
  EXPECT_EQ(0u, quic_session_.bytesToSend());
}

TEST_F(EnvoyQuicClientStreamTest, EncodeTrailersOnClosedStream) {
  const auto result = quic_stream_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(result.ok());

  // Encode 18kB response body. first 16KB should be written out right away. The
  // rest should be buffered.
  std::string body(18 * 1024, 'a');
  Buffer::OwnedImpl buffer(body);
  quic_stream_->encodeData(buffer, false);
  EXPECT_LT(0u, quic_session_.bytesToSend());

  // Reset stream should clear the connection level buffered bytes accounting.
  EXPECT_CALL(stream_callbacks_,
              onResetStream(Http::StreamResetReason::LocalRefusedStreamReset, _));
  quic_stream_->resetStream(Http::StreamResetReason::LocalRefusedStreamReset);

  // Try to send trailers on the closed stream.
  EXPECT_ENVOY_BUG(quic_stream_->encodeTrailers(request_trailers_),
                   "encodeTrailers is called on write-closed stream");
  EXPECT_EQ(0u, quic_session_.bytesToSend());
}

} // namespace Quic
} // namespace Envoy
