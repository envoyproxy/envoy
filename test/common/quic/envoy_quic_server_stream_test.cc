#include <cstddef>
#include <string>

#include "source/common/event/libevent_scheduler.h"
#include "source/common/http/headers.h"
#include "source/common/quic/envoy_quic_alarm_factory.h"
#include "source/common/quic/envoy_quic_connection_helper.h"
#include "source/common/quic/envoy_quic_server_connection.h"
#include "source/common/quic/envoy_quic_server_session.h"
#include "source/common/quic/envoy_quic_server_stream.h"
#include "source/server/active_listener_base.h"

#include "test/common/quic/test_utils.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/quic/core/crypto/null_encrypter.h"
#include "quiche/quic/core/deterministic_connection_id_generator.h"
#include "quiche/quic/test_tools/quic_connection_peer.h"
#include "quiche/quic/test_tools/quic_session_peer.h"

namespace Envoy {
namespace Quic {

namespace {

using testing::_;
using testing::Invoke;

constexpr unsigned int kStreamId = 4u;

} // namespace

class EnvoyQuicServerStreamTest : public testing::Test {
public:
  EnvoyQuicServerStreamTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")),
        connection_helper_(*dispatcher_),
        alarm_factory_(*dispatcher_, *connection_helper_.GetClock()),
        quic_version_(quic::CurrentSupportedHttp3Versions()[0]),
        listener_stats_({ALL_LISTENER_STATS(POOL_COUNTER(listener_config_.listenerScope()),
                                            POOL_GAUGE(listener_config_.listenerScope()),
                                            POOL_HISTOGRAM(listener_config_.listenerScope()))}),
        quic_connection_(connection_helper_, alarm_factory_, writer_,
                         quic::ParsedQuicVersionVector{quic_version_}, *listener_config_.socket_,
                         connection_id_generator_),
        quic_stat_names_(listener_config_.listenerScope().symbolTable()),
        quic_session_(quic_config_, {quic_version_}, &quic_connection_, *dispatcher_,
                      quic_config_.GetInitialStreamFlowControlWindowToSend() * 2, quic_stat_names_,
                      listener_config_.listenerScope()),
        stats_(
            {ALL_HTTP3_CODEC_STATS(POOL_COUNTER_PREFIX(listener_config_.listenerScope(), "http3."),
                                   POOL_GAUGE_PREFIX(listener_config_.listenerScope(), "http3."))}),
        quic_stream_(new EnvoyQuicServerStream(
            stream_id_, &quic_session_, quic::BIDIRECTIONAL, stats_, http3_options_,
            envoy::config::core::v3::HttpProtocolOptions::ALLOW)),
        response_headers_{{":status", "200"}, {"response-key", "response-value"}},
        response_trailers_{{"trailer-key", "trailer-value"}} {
    EXPECT_CALL(stream_decoder_, accessLogHandlers());
    quic_stream_->setRequestDecoder(stream_decoder_);
    quic_stream_->addCallbacks(stream_callbacks_);
    quic_stream_->getStream().setFlushTimeout(std::chrono::milliseconds(30000));
    quic::test::QuicConnectionPeer::SetAddressValidated(&quic_connection_);
    quic_session_.ActivateStream(std::unique_ptr<EnvoyQuicServerStream>(quic_stream_));
    EXPECT_CALL(quic_session_, ShouldYield(_)).WillRepeatedly(testing::Return(false));
    EXPECT_CALL(quic_session_, WritevData(_, _, _, _, _, _))
        .WillRepeatedly(
            Invoke([](quic::QuicStreamId, size_t write_length, quic::QuicStreamOffset,
                      quic::StreamSendingState state, bool, absl::optional<quic::EncryptionLevel>) {
              return quic::QuicConsumedData{write_length, state != quic::NO_FIN};
            }));
    EXPECT_CALL(writer_, WritePacket(_, _, _, _, _, _))
        .WillRepeatedly(Invoke([](const char*, size_t buf_len, const quic::QuicIpAddress&,
                                  const quic::QuicSocketAddress&, quic::PerPacketOptions*,
                                  const quic::QuicPacketWriterParams&) {
          return quic::WriteResult{quic::WRITE_STATUS_OK, static_cast<int>(buf_len)};
        }));
  }

  void SetUp() override {
    quic_session_.Initialize();
    setQuicConfigWithDefaultValues(quic_session_.config());
    quic_connection_.SetEncrypter(
        quic::ENCRYPTION_FORWARD_SECURE,
        std::make_unique<quic::test::TaggingEncrypter>(quic::ENCRYPTION_FORWARD_SECURE));
    quic_connection_.SetDefaultEncryptionLevel(quic::ENCRYPTION_FORWARD_SECURE);

    quic_session_.OnConfigNegotiated();
    spdy_request_headers_[":authority"] = host_;
    spdy_request_headers_[":method"] = "POST";
    spdy_request_headers_[":path"] = "/";
    spdy_request_headers_[":scheme"] = "https";

    // OnSettingsFrame must be called since the MockEnvoyQuicSession object returns
    // HttpDatagramSupport::kRfc in the LocalHttpDatagramSupport method. Otherwise, the session
    // buffers data until the SETTINGS frame is received.
    quic::SettingsFrame settings;
    settings.values[quic::SETTINGS_H3_DATAGRAM] = 1;
    EXPECT_TRUE(quic_session_.OnSettingsFrame(settings));
  }

  void TearDown() override {
    if (quic_connection_.connected()) {
      EXPECT_CALL(quic_session_, MaybeSendRstStreamFrame(_, _, _)).Times(testing::AtMost(1u));
      EXPECT_CALL(quic_session_,
                  MaybeSendStopSendingFrame(
                      _, quic::QuicResetStreamError::FromInternal(quic::QUIC_STREAM_NO_ERROR)))
          .Times(testing::AtMost(1u));
      EXPECT_CALL(quic_connection_,
                  SendConnectionClosePacket(_, quic::NO_IETF_QUIC_ERROR, "Closed by application"));
      quic_session_.close(Network::ConnectionCloseType::NoFlush);
    }
  }

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  void setUpCapsuleProtocol(bool close_send_stream, bool close_recv_stream) {
    // Decodes a CONNECT-UDP request.
    EXPECT_CALL(stream_decoder_, decodeHeaders_(_, _))
        .WillOnce(Invoke([](const Http::RequestHeaderMapSharedPtr& headers, bool) {
          Http::HeaderMap::GetResult capsule_protocol =
              headers->get(Http::LowerCaseString("Capsule-Protocol"));
          EXPECT_FALSE(capsule_protocol.empty());
          EXPECT_EQ(capsule_protocol[0]->value().getStringView(), "?1");
        }));
    quiche::HttpHeaderBlock request_headers;
    request_headers[":authority"] = host_;
    request_headers[":method"] = "CONNECT";
    request_headers[":protocol"] = "connect-udp";
    request_headers[":path"] = "/.well-known/masque/udp/192.0.2.6/443/";
    request_headers[":scheme"] = "https";
    request_headers["capsule-protocol"] = "?1";
    std::string payload = spdyHeaderToHttp3StreamPayload(request_headers);
    quic::QuicStreamFrame frame(stream_id_, close_recv_stream, 0, payload);
    quic_stream_->OnStreamFrame(frame);
    EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());

    // Encodes the response
    Http::TestResponseHeaderMapImpl response_headers = {{":status", "200"},
                                                        {"capsule-protocol", "?1"}};
    quic_stream_->encodeHeaders(response_headers, close_send_stream);
  }
#endif

  size_t receiveRequest(const std::string& payload, bool fin,
                        size_t decoder_buffer_high_watermark) {
    EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
        .WillOnce(Invoke([this](const Http::RequestHeaderMapSharedPtr& headers, bool) {
          EXPECT_EQ(host_, headers->getHostValue());
          EXPECT_EQ("/", headers->getPathValue());
          EXPECT_EQ(Http::Headers::get().MethodValues.Post, headers->getMethodValue());
        }));

    EXPECT_CALL(stream_decoder_, decodeData(_, _))
        .WillOnce(Invoke([&](Buffer::Instance& buffer, bool finished_reading) {
          EXPECT_EQ(payload, buffer.toString());
          EXPECT_EQ(fin, finished_reading);
          if (!finished_reading && buffer.length() > decoder_buffer_high_watermark) {
            quic_stream_->readDisable(true);
          }
        }));
    std::string data = absl::StrCat(spdyHeaderToHttp3StreamPayload(spdy_request_headers_),
                                    bodyToHttp3StreamPayload(payload));
    quic::QuicStreamFrame frame(stream_id_, fin, 0, data);
    quic_stream_->OnStreamFrame(frame);
    EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());
    return data.length();
  }

  size_t receiveRequestHeaders(bool end_stream) {
    EXPECT_CALL(stream_decoder_, decodeHeaders_(_, end_stream))
        .WillOnce(Invoke([this](const Http::RequestHeaderMapSharedPtr& headers, bool) {
          EXPECT_EQ(host_, headers->getHostValue());
          EXPECT_EQ("/", headers->getPathValue());
          EXPECT_EQ(Http::Headers::get().MethodValues.Post, headers->getMethodValue());
        }));

    std::string data = spdyHeaderToHttp3StreamPayload(spdy_request_headers_);
    quic::QuicStreamFrame frame(stream_id_, end_stream, 0, data);
    quic_stream_->OnStreamFrame(frame);
    EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());
    return data.length();
  }

  size_t receiveRequestBody(size_t offset, const std::string& payload, bool fin,
                            size_t decoder_buffer_high_watermark) {
    EXPECT_CALL(stream_decoder_, decodeData(_, _))
        .WillOnce(Invoke([&](Buffer::Instance& buffer, bool finished_reading) {
          EXPECT_EQ(payload, buffer.toString());
          EXPECT_EQ(fin, finished_reading);
          if (!finished_reading && buffer.length() > decoder_buffer_high_watermark) {
            quic_stream_->readDisable(true);
          }
        }));
    std::string data = absl::StrCat(bodyToHttp3StreamPayload(payload));
    quic::QuicStreamFrame frame(stream_id_, fin, offset, data);
    quic_stream_->OnStreamFrame(frame);
    EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());
    return data.length();
  }

  void receiveTrailers(size_t offset) {
    spdy_trailers_["key1"] = "value1";
    std::string payload = spdyHeaderToHttp3StreamPayload(spdy_trailers_);
    quic::QuicStreamFrame frame(stream_id_, true, offset, payload);
    quic_stream_->OnStreamFrame(frame);
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
  quic::DeterministicConnectionIdGenerator connection_id_generator_{
      quic::kQuicDefaultConnectionIdLength};
  testing::NiceMock<MockEnvoyQuicServerConnection> quic_connection_;
  Envoy::Quic::QuicStatNames quic_stat_names_;
  MockEnvoyQuicSession quic_session_;
  quic::QuicStreamId stream_id_{kStreamId};
  Http::Http3::CodecStats stats_;
  envoy::config::core::v3::Http3ProtocolOptions http3_options_;
  EnvoyQuicServerStream* quic_stream_;
  Http::MockRequestDecoder stream_decoder_;
  Http::MockStreamCallbacks stream_callbacks_;
  quiche::HttpHeaderBlock spdy_request_headers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  quiche::HttpHeaderBlock spdy_trailers_;
  std::string host_{"www.abc.com"};
  std::string request_body_{"Hello world"};
#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  std::string capsule_fragment_ = absl::HexStringToBytes("00"               // DATAGRAM capsule type
                                                         "08"               // capsule length
                                                         "a1a2a3a4a5a6a7a8" // HTTP Datagram payload
  );
  std::string datagram_fragment_ =
      absl::HexStringToBytes(absl::StrCat(absl::Hex(kStreamId / quic::kHttpDatagramStreamIdDivisor,
                                                    absl::kZeroPad2), // Quarter Stream ID
                                          "a1a2a3a4a5a6a7a8")         // HTTP Datagram Payload
      );
#endif
};

TEST_F(EnvoyQuicServerStreamTest, GetRequestAndResponse) {
  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([this](const Http::RequestHeaderMapSharedPtr& headers, bool) {
        EXPECT_EQ(host_, headers->getHostValue());
        EXPECT_EQ("/", headers->getPathValue());
        EXPECT_EQ(Http::Headers::get().MethodValues.Get, headers->getMethodValue());
        // Verify that the duplicated headers are handled correctly before passing to stream
        // decoder.
        EXPECT_EQ("a=b; c=d",
                  headers->get(Http::Headers::get().Cookie)[0]->value().getStringView());
      }));
  EXPECT_CALL(stream_decoder_, decodeData(BufferStringEqual(""), /*end_stream=*/true));
  quiche::HttpHeaderBlock spdy_headers;
  spdy_headers[":authority"] = host_;
  spdy_headers[":method"] = "GET";
  spdy_headers[":path"] = "/";
  spdy_headers[":scheme"] = "https";
  spdy_headers.AppendValueOrAddHeader("cookie", "a=b");
  spdy_headers.AppendValueOrAddHeader("cookie", "c=d");
  std::string payload = spdyHeaderToHttp3StreamPayload(spdy_headers);
  quic::QuicStreamFrame frame(stream_id_, true, 0, payload);
  quic_stream_->OnStreamFrame(frame);
  EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());
  quic_stream_->encodeHeaders(response_headers_, /*end_stream=*/true);
}

TEST_F(EnvoyQuicServerStreamTest, PostRequestAndResponse) {
  EXPECT_EQ(absl::nullopt, quic_stream_->http1StreamEncoderOptions());
  receiveRequest(request_body_, true, request_body_.size() * 2);
  quic_stream_->encodeHeaders(response_headers_, /*end_stream=*/false);

  std::string response(18 * 1024, 'a');
  Buffer::OwnedImpl buffer(response);
  quic_stream_->encodeData(buffer, false);
  quic_stream_->encodeTrailers(response_trailers_);
}

TEST_F(EnvoyQuicServerStreamTest, PostRequestAndResponseWithMemSliceReleasor) {
  EXPECT_EQ(absl::nullopt, quic_stream_->http1StreamEncoderOptions());
  receiveRequest(request_body_, true, request_body_.size() * 2);
  quic_stream_->encodeHeaders(response_headers_, /*end_stream=*/false);
  quic_stream_->encodeTrailers(response_trailers_);
}

TEST_F(EnvoyQuicServerStreamTest, EncodeHeaderOnClosedStream) {
  receiveRequest(request_body_, true, request_body_.size() * 2);

  // Reset stream should clear the connection level buffered bytes accounting.
  EXPECT_CALL(quic_session_, MaybeSendStopSendingFrame(_, _));
  EXPECT_CALL(quic_session_, MaybeSendRstStreamFrame(_, _, _));
  EXPECT_CALL(stream_callbacks_,
              onResetStream(Http::StreamResetReason::LocalRefusedStreamReset, _));
  quic_stream_->resetStream(Http::StreamResetReason::LocalRefusedStreamReset);

  EXPECT_ENVOY_BUG(quic_stream_->encodeHeaders(response_headers_, /*end_stream=*/false),
                   "encodeHeaders is called on write-closed stream.");
}

TEST_F(EnvoyQuicServerStreamTest, EncodeDataOnClosedStream) {
  receiveRequest(request_body_, true, request_body_.size() * 2);
  quic_stream_->encodeHeaders(response_headers_, /*end_stream=*/false);

  // Encode 18kB response body. first 16KB should be written out right away. The
  // rest should be buffered.
  std::string response(18 * 1024, 'a');
  Buffer::OwnedImpl buffer(response);
  quic_stream_->encodeData(buffer, false);
  EXPECT_LT(0u, quic_session_.bytesToSend());

  // Reset stream should clear the connection level buffered bytes accounting.
  EXPECT_CALL(quic_session_, MaybeSendStopSendingFrame(_, _));
  EXPECT_CALL(quic_session_, MaybeSendRstStreamFrame(_, _, _));
  EXPECT_CALL(stream_callbacks_,
              onResetStream(Http::StreamResetReason::LocalRefusedStreamReset, _));
  quic_stream_->resetStream(Http::StreamResetReason::LocalRefusedStreamReset);

  // Try to send more data on the closed stream. And the watermark shouldn't be
  // messed up.
  std::string response2(1024, 'a');
  Buffer::OwnedImpl buffer2(response2);
  EXPECT_ENVOY_BUG(quic_stream_->encodeData(buffer2, true),
                   "encodeData is called on write-closed stream");
  EXPECT_EQ(0u, quic_session_.bytesToSend());
}

TEST_F(EnvoyQuicServerStreamTest, EncodeTrailersOnClosedStream) {
  receiveRequest(request_body_, true, request_body_.size() * 2);
  quic_stream_->encodeHeaders(response_headers_, /*end_stream=*/false);

  // Encode 18kB response body. first 16KB should be written out right away. The
  // rest should be buffered.
  std::string response(18 * 1024, 'a');
  Buffer::OwnedImpl buffer(response);
  quic_stream_->encodeData(buffer, false);
  EXPECT_LT(0u, quic_session_.bytesToSend());

  // Reset stream should clear the connection level buffered bytes accounting.
  EXPECT_CALL(quic_session_, MaybeSendStopSendingFrame(_, _));
  EXPECT_CALL(quic_session_, MaybeSendRstStreamFrame(_, _, _));
  EXPECT_CALL(stream_callbacks_,
              onResetStream(Http::StreamResetReason::LocalRefusedStreamReset, _));
  quic_stream_->resetStream(Http::StreamResetReason::LocalRefusedStreamReset);

  // Try to send trailers on the closed stream. And the watermark shouldn't be
  // messed up.
  EXPECT_ENVOY_BUG(quic_stream_->encodeTrailers(response_trailers_),
                   "encodeTrailers is called on write-closed stream");
  EXPECT_EQ(0u, quic_session_.bytesToSend());
}

TEST_F(EnvoyQuicServerStreamTest, PostRequestAndResponseWithAccounting) {
  EXPECT_EQ(absl::nullopt, quic_stream_->http1StreamEncoderOptions());
  EXPECT_EQ(0, quic_stream_->bytesMeter()->wireBytesReceived());
  EXPECT_EQ(0, quic_stream_->bytesMeter()->headerBytesReceived());
  size_t offset = receiveRequestHeaders(false);
  // Received header bytes do not include the HTTP/3 frame overhead.
  EXPECT_EQ(quic_stream_->stream_bytes_read() - 2,
            quic_stream_->bytesMeter()->headerBytesReceived());
  EXPECT_EQ(quic_stream_->stream_bytes_read(), quic_stream_->bytesMeter()->wireBytesReceived());
  size_t body_size = receiveRequestBody(offset, request_body_, true, request_body_.size() * 2);
  EXPECT_EQ(quic_stream_->stream_bytes_read(), quic_stream_->bytesMeter()->wireBytesReceived());
  EXPECT_EQ(quic_stream_->stream_bytes_read() - 2 - body_size,
            quic_stream_->bytesMeter()->headerBytesReceived());
  // Wire bytes received will be slightly larger than the body + headers because of body framing.
  EXPECT_EQ(4 + request_body_.size() + quic_stream_->bytesMeter()->headerBytesReceived(),
            quic_stream_->bytesMeter()->wireBytesReceived());
  EXPECT_EQ(0, quic_stream_->bytesMeter()->wireBytesSent());
  EXPECT_EQ(0, quic_stream_->bytesMeter()->headerBytesSent());
  quic_stream_->encodeHeaders(response_headers_, /*end_stream=*/false);
  EXPECT_GE(27, quic_stream_->bytesMeter()->headerBytesSent());
  EXPECT_GE(27, quic_stream_->bytesMeter()->wireBytesSent());

  quic_stream_->encodeTrailers(response_trailers_);
  EXPECT_GE(52, quic_stream_->bytesMeter()->headerBytesSent());
  EXPECT_GE(52, quic_stream_->bytesMeter()->wireBytesSent());
}

TEST_F(EnvoyQuicServerStreamTest, DecodeHeadersBodyAndTrailers) {
  size_t offset = receiveRequest(request_body_, false, request_body_.size() * 2);

  EXPECT_CALL(stream_decoder_, decodeTrailers_(_))
      .WillOnce(Invoke([](const Http::RequestTrailerMapPtr& headers) {
        Http::LowerCaseString key1("key1");
        Http::LowerCaseString key2(":final-offset");
        EXPECT_EQ("value1", headers->get(key1)[0]->value().getStringView());
        EXPECT_TRUE(headers->get(key2).empty());
      }));
  receiveTrailers(offset);
  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
}

TEST_F(EnvoyQuicServerStreamTest, ResetStreamByHCM) {
  receiveRequest(request_body_, false, request_body_.size() * 2);
  EXPECT_CALL(quic_session_, MaybeSendStopSendingFrame(_, _));
  EXPECT_CALL(quic_session_, MaybeSendRstStreamFrame(_, _, _));
  EXPECT_CALL(stream_callbacks_,
              onResetStream(Http::StreamResetReason::LocalRefusedStreamReset, _));
  quic_stream_->resetStream(Http::StreamResetReason::LocalRefusedStreamReset);
  EXPECT_TRUE(quic_stream_->rst_sent());
}

TEST_F(EnvoyQuicServerStreamTest, ReceiveStopSending) {
  size_t payload_offset = receiveRequest(request_body_, false, request_body_.size() * 2);
  // Receiving STOP_SENDING alone should trigger upstream reset.
  EXPECT_CALL(stream_callbacks_, onResetStream(Http::StreamResetReason::RemoteReset, _));
  EXPECT_CALL(quic_session_, MaybeSendRstStreamFrame(_, _, _));
  quic_stream_->OnStopSending(quic::QuicResetStreamError::FromInternal(quic::QUIC_STREAM_NO_ERROR));
  EXPECT_FALSE(quic_stream_->read_side_closed());

  // Following FIN should be discarded and the stream should be closed.
  std::string second_part_request = bodyToHttp3StreamPayload("aaaa");
  EXPECT_CALL(stream_decoder_, decodeData(_, _)).Times(0u);
  quic::QuicStreamFrame frame(stream_id_, true, payload_offset, second_part_request);
  quic_stream_->OnStreamFrame(frame);
  EXPECT_TRUE(quic_stream_->read_side_closed());
  EXPECT_TRUE(quic_stream_->write_side_closed());
}

TEST_F(EnvoyQuicServerStreamTest, EarlyResponseWithStopSending) {
  receiveRequest(request_body_, false, request_body_.size() * 2);
  // Write response headers with FIN before finish receiving request.
  quic_stream_->encodeHeaders(response_headers_, true);
  // Resetting the stream now means stop reading and sending QUIC_STREAM_NO_ERROR or STOP_SENDING.
  EXPECT_CALL(quic_session_, MaybeSendStopSendingFrame(_, _));
  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
  quic_stream_->resetStream(Http::StreamResetReason::LocalReset);
  EXPECT_TRUE(quic_stream_->reading_stopped());
  EXPECT_EQ(quic::QUIC_STREAM_NO_ERROR, quic_stream_->stream_error());
}

TEST_F(EnvoyQuicServerStreamTest, ReadDisableUponLargePost) {
  std::string large_request(1024, 'a');
  // Sending such large request will cause read to be disabled.
  size_t payload_offset = receiveRequest(large_request, false, 512);
  EXPECT_FALSE(quic_stream_->HasBytesToRead());
  // Disable reading one more time.
  quic_stream_->readDisable(true);
  std::string second_part_request = bodyToHttp3StreamPayload("bbb");
  // Receiving more data in the same event loop will push the receiving pipe line.
  EXPECT_CALL(stream_decoder_, decodeData(_, _))
      .WillOnce(Invoke([](Buffer::Instance& buffer, bool finished_reading) {
        EXPECT_EQ(3u, buffer.length());
        EXPECT_EQ("bbb", buffer.toString());
        EXPECT_FALSE(finished_reading);
      }));
  quic::QuicStreamFrame frame(stream_id_, false, payload_offset, second_part_request);
  quic_stream_->OnStreamFrame(frame);
  payload_offset += second_part_request.length();

  // Re-enable reading just once shouldn't unblock stream.
  quic_stream_->readDisable(false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // This data frame should also be buffered.
  std::string last_part_request = bodyToHttp3StreamPayload("ccc");
  quic::QuicStreamFrame frame2(stream_id_, false, payload_offset, last_part_request);
  quic_stream_->OnStreamFrame(frame2);
  payload_offset += last_part_request.length();

  // Trailers should also be buffered.
  EXPECT_CALL(stream_decoder_, decodeTrailers_(_)).Times(0);
  receiveTrailers(payload_offset);

  // Unblock stream now. The remaining data in the receiving buffer should be
  // pushed to upstream.
  EXPECT_CALL(stream_decoder_, decodeData(_, _))
      .WillOnce(Invoke([](Buffer::Instance& buffer, bool finished_reading) {
        EXPECT_EQ(3u, buffer.length());
        EXPECT_EQ("ccc", buffer.toString());
        EXPECT_FALSE(finished_reading);
      }));
  EXPECT_CALL(stream_decoder_, decodeTrailers_(_));
  quic_stream_->readDisable(false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
}

// Tests that readDisable() doesn't cause re-entry of OnBodyAvailable().
TEST_F(EnvoyQuicServerStreamTest, ReadDisableAndReEnableImmediately) {
  std::string payload(1024, 'a');
  size_t offset = receiveRequest(payload, false, 2048);

  EXPECT_CALL(stream_decoder_, decodeData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool finished_reading) {
        EXPECT_EQ(payload, buffer.toString());
        EXPECT_FALSE(finished_reading);
        quic_stream_->readDisable(true);
        // Re-enable reading should not trigger another decodeData.
        quic_stream_->readDisable(false);
      }));
  std::string data = bodyToHttp3StreamPayload(payload);
  quic::QuicStreamFrame frame(stream_id_, false, offset, data);
  quic_stream_->OnStreamFrame(frame);
  offset += data.length();
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // The stream shouldn't be blocked in the next event loop.
  std::string last_part_request = bodyToHttp3StreamPayload("bbb");
  quic::QuicStreamFrame frame2(stream_id_, true, offset, last_part_request);
  EXPECT_CALL(stream_decoder_, decodeData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool finished_reading) {
        EXPECT_EQ("bbb", buffer.toString());
        EXPECT_TRUE(finished_reading);
      }));

  quic_stream_->OnStreamFrame(frame2);
  // The posted unblock callback shouldn't trigger another decodeData.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
}

TEST_F(EnvoyQuicServerStreamTest, ReadDisableUponHeaders) {
  std::string payload(1024, 'a');
  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([this](const Http::RequestHeaderMapSharedPtr&, bool) {
        quic_stream_->readDisable(true);
      }));
  EXPECT_CALL(stream_decoder_, decodeData(_, _));
  std::string data = absl::StrCat(spdyHeaderToHttp3StreamPayload(spdy_request_headers_),
                                  bodyToHttp3StreamPayload(payload));
  quic::QuicStreamFrame frame(stream_id_, false, 0, data);
  quic_stream_->OnStreamFrame(frame);
  EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());
  // Stream should be blocked in the next event loop.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  // Receiving more date shouldn't trigger decoding.
  EXPECT_CALL(stream_decoder_, decodeData(_, _)).Times(0);
  data = bodyToHttp3StreamPayload(payload);
  quic::QuicStreamFrame frame2(stream_id_, false, 0, data);
  quic_stream_->OnStreamFrame(frame2);

  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
}

TEST_F(EnvoyQuicServerStreamTest, ReadDisableUponTrailers) {
  size_t payload_offset = receiveRequest(request_body_, false, request_body_.length() * 2);
  EXPECT_FALSE(quic_stream_->HasBytesToRead());

  EXPECT_CALL(stream_decoder_, decodeTrailers_(_))
      .WillOnce(
          Invoke([this](const Http::RequestTrailerMapPtr&) { quic_stream_->readDisable(true); }));
  receiveTrailers(payload_offset);

  EXPECT_TRUE(quic_stream_->IsDoneReading());
  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
}

TEST_F(EnvoyQuicServerStreamTest, ReadDisableUponMalformedTrailers) {
  size_t payload_offset = receiveRequest(request_body_, false, request_body_.length() * 2);
  EXPECT_FALSE(quic_stream_->HasBytesToRead());
  EXPECT_CALL(stream_decoder_, decodeTrailers_(_)).Times(0);

  // Invalid uppercase key
  spdy_trailers_["KEY1"] = "value1";
  std::string payload = spdyHeaderToHttp3StreamPayload(spdy_trailers_);
  quic::QuicStreamFrame frame(stream_id_, true, payload_offset, payload);

  EXPECT_CALL(quic_session_, MaybeSendRstStreamFrame(_, _, _));
  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
  quic_stream_->OnStreamFrame(frame);
  EXPECT_TRUE(quic_stream_->IsDoneReading());
}

// Tests that the stream with a send buffer whose high limit is 16k and low
// limit is 8k sends over 32kB response.
TEST_F(EnvoyQuicServerStreamTest, WatermarkSendBuffer) {
  receiveRequest(request_body_, true, request_body_.size() * 2);

  // Bump connection flow control window large enough not to cause connection
  // level flow control blocked.
  quic::QuicWindowUpdateFrame window_update(
      quic::kInvalidControlFrameId,
      quic::QuicUtils::GetInvalidStreamId(quic_version_.transport_version), 1024 * 1024);
  quic_session_.OnWindowUpdateFrame(window_update);

  // 32KB + 2 byte. The initial stream flow control window is 16k.
  response_headers_.addCopy(":content-length", "32770");
  quic_stream_->encodeHeaders(response_headers_, /*end_stream=*/false);

  // Encode 32kB response body. first 16KB should be written out right away. The
  // rest should be buffered. The high watermark is 16KB, so this call should
  // make the send buffer reach its high watermark.
  std::string response(32 * 1024 + 1, 'a');
  Buffer::OwnedImpl buffer(response);
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
    std::string rest_response(1, 'a');
    Buffer::OwnedImpl buffer(rest_response);
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
}

TEST_F(EnvoyQuicServerStreamTest, HeadersContributeToWatermarkIquic) {
  receiveRequest(request_body_, true, request_body_.size() * 2);

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
  quic_stream_->encodeHeaders(response_headers_, /*end_stream=*/false);

  // Encode 16kB -10 bytes request body. Because the high watermark is 16KB, with previously
  // buffered headers, this call should make the send buffers reach their high watermark.
  std::string response(16 * 1024 - 10, 'a');
  Buffer::OwnedImpl buffer(response);
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
      .WillRepeatedly(
          Invoke([](quic::QuicStreamId, size_t, quic::QuicStreamOffset,
                    quic::StreamSendingState state, bool, absl::optional<quic::EncryptionLevel>) {
            return quic::QuicConsumedData{0u, state != quic::NO_FIN};
          }));
  // Send more data. If watermark bytes counting were not cleared in previous
  // OnCanWrite, this write would have caused the stream to exceed its high watermark.
  std::string response1(16 * 1024 - 3, 'a');
  Buffer::OwnedImpl buffer1(response1);
  quic_stream_->encodeData(buffer1, false);
  // Buffering more trailers will cause stream to reach high watermark, but
  // because trailers closes the stream, no callback should be triggered.
  quic_stream_->encodeTrailers(response_trailers_);
}

TEST_F(EnvoyQuicServerStreamTest, RequestHeaderTooLarge) {
  // Bump stream flow control window to allow request headers larger than 16K.
  quic::QuicWindowUpdateFrame window_update1(quic::kInvalidControlFrameId, quic_stream_->id(),
                                             32 * 1024);
  quic_stream_->OnWindowUpdateFrame(window_update1);
  EXPECT_CALL(quic_session_, MaybeSendStopSendingFrame(_, _));
  EXPECT_CALL(quic_session_, MaybeSendRstStreamFrame(_, _, _));
  EXPECT_CALL(stream_callbacks_, onResetStream(Http::StreamResetReason::LocalReset, _));
  quiche::HttpHeaderBlock spdy_headers;
  spdy_headers[":authority"] = host_;
  spdy_headers[":method"] = "POST";
  spdy_headers[":path"] = "/";
  // This header exceeds max header size limit and should cause stream reset.
  spdy_headers["long_header"] = std::string(16 * 1024 + 1, 'a');
  std::string payload = absl::StrCat(spdyHeaderToHttp3StreamPayload(spdy_headers),
                                     bodyToHttp3StreamPayload(request_body_));
  quic::QuicStreamFrame frame(stream_id_, false, 0, payload);
  quic_stream_->OnStreamFrame(frame);

  EXPECT_TRUE(quic_stream_->rst_sent());
}

TEST_F(EnvoyQuicServerStreamTest, RequestTrailerTooLarge) {
  // Bump stream flow control window to allow request headers larger than 16K.
  quic::QuicWindowUpdateFrame window_update1(quic::kInvalidControlFrameId, quic_stream_->id(),
                                             20 * 1024);
  size_t offset = receiveRequest(request_body_, false, request_body_.size() * 2);

  quic_stream_->OnWindowUpdateFrame(window_update1);
  EXPECT_CALL(quic_session_, MaybeSendStopSendingFrame(_, _));
  EXPECT_CALL(quic_session_, MaybeSendRstStreamFrame(_, _, _));
  EXPECT_CALL(stream_callbacks_, onResetStream(Http::StreamResetReason::LocalReset, _));
  quiche::HttpHeaderBlock spdy_trailers;
  // This header exceeds max header size limit and should cause stream reset.
  spdy_trailers["long_header"] = std::string(16 * 1024 + 1, 'a');
  std::string payload = spdyHeaderToHttp3StreamPayload(spdy_trailers);
  quic::QuicStreamFrame frame(stream_id_, false, offset, payload);
  quic_stream_->OnStreamFrame(frame);

  EXPECT_TRUE(quic_stream_->rst_sent());
}

// Tests that closing connection is QUICHE write call stack doesn't mess up
// watermark buffer accounting.
TEST_F(EnvoyQuicServerStreamTest, ConnectionCloseDuringEncoding) {
  receiveRequest(request_body_, true, request_body_.size() * 2);
  quic_stream_->encodeHeaders(response_headers_, /*end_stream=*/false);
  std::string response(16 * 1024 + 1, 'a');
  EXPECT_CALL(quic_connection_,
              SendConnectionClosePacket(_, quic::NO_IETF_QUIC_ERROR, "Closed in WriteHeaders"));
  EXPECT_CALL(quic_session_, WritevData(_, _, _, _, _, _))
      .Times(testing::AtLeast(1u))
      .WillRepeatedly(
          Invoke([this](quic::QuicStreamId, size_t data_size, quic::QuicStreamOffset,
                        quic::StreamSendingState, bool, absl::optional<quic::EncryptionLevel>) {
            if (data_size < 10) {
              // Ietf QUIC sends a small data frame header before sending the data frame payload.
              return quic::QuicConsumedData{data_size, false};
            }
            // Mimic write failure while writing data frame payload.
            quic_connection_.CloseConnection(
                quic::QUIC_INTERNAL_ERROR, "Closed in WriteHeaders",
                quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
            // This will cause the payload to be buffered.
            return quic::QuicConsumedData{0, false};
          }));

  // Bump the stream flow control window.
  quic::QuicWindowUpdateFrame window_update(quic::kInvalidControlFrameId, quic_stream_->id(),
                                            20 * 1024);
  quic_stream_->OnWindowUpdateFrame(window_update);

  // Send a response which causes connection to close.
  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
  EXPECT_CALL(quic_session_, MaybeSendRstStreamFrame(_, _, _));

  Buffer::OwnedImpl buffer(response);
  // Though the stream send buffer is above high watermark, onAboveWriteBufferHighWatermark())
  // shouldn't be called because the connection is closed.
  quic_stream_->encodeData(buffer, false);
  EXPECT_EQ(quic_session_.bytesToSend(), 0u);
}

TEST_F(EnvoyQuicServerStreamTest, ConnectionCloseDuringEncodingEndStream) {
  receiveRequest(request_body_, true, request_body_.size() * 2);
  quic_stream_->encodeHeaders(response_headers_, /*end_stream=*/false);
  EXPECT_CALL(quic_connection_,
              SendConnectionClosePacket(_, quic::NO_IETF_QUIC_ERROR, "Closed in WriteHeaders"));
  EXPECT_CALL(quic_session_, WritevData(_, _, _, _, _, _))
      .Times(testing::AtLeast(1u))
      .WillRepeatedly(
          Invoke([this](quic::QuicStreamId, size_t data_size, quic::QuicStreamOffset,
                        quic::StreamSendingState, bool, absl::optional<quic::EncryptionLevel>) {
            if (data_size < 10) {
              // Ietf QUIC sends a small data frame header before sending the data frame payload.
              return quic::QuicConsumedData{data_size, false};
            }
            // Mimic write failure while writing data frame payload.
            quic_connection_.CloseConnection(
                quic::QUIC_INTERNAL_ERROR, "Closed in WriteHeaders",
                quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
            // This will cause the payload to be buffered.
            return quic::QuicConsumedData{0, false};
          }));

  // Send a response which causes connection to close.
  EXPECT_CALL(quic_session_, MaybeSendRstStreamFrame(_, _, _));

  std::string response(16 * 1024 + 1, 'a');
  Buffer::OwnedImpl buffer(response);
  // Though the stream send buffer is above high watermark, onAboveWriteBufferHighWatermark())
  // shouldn't be called because the connection is closed.
  quic_stream_->encodeData(buffer, true);
  EXPECT_EQ(quic_session_.bytesToSend(), 0u);
  EXPECT_TRUE(quic_stream_->write_side_closed() && quic_stream_->read_side_closed());
  quic_session_.CleanUpClosedStreams();
}

// Tests that after end_stream is encoded, closing connection shouldn't call
// onResetStream() callbacks.
TEST_F(EnvoyQuicServerStreamTest, ConnectionCloseAfterEndStreamEncoded) {
  receiveRequest(request_body_, true, request_body_.size() * 2);
  EXPECT_CALL(quic_connection_,
              SendConnectionClosePacket(_, quic::NO_IETF_QUIC_ERROR, "Closed in WriteHeaders"));
  EXPECT_CALL(quic_session_, WritevData(_, _, _, _, _, _))
      .WillOnce(
          Invoke([this](quic::QuicStreamId, size_t, quic::QuicStreamOffset,
                        quic::StreamSendingState, bool, absl::optional<quic::EncryptionLevel>) {
            quic_connection_.CloseConnection(
                quic::QUIC_INTERNAL_ERROR, "Closed in WriteHeaders",
                quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
            return quic::QuicConsumedData{0, false};
          }));
  EXPECT_CALL(quic_session_, MaybeSendRstStreamFrame(_, _, _));
  quic_stream_->encodeHeaders(response_headers_, /*end_stream=*/true);
}

// Tests that when stream is cleaned up, QuicStatsGatherer executes any pending logs.
TEST_F(EnvoyQuicServerStreamTest, StatsGathererLogsOnStreamDestruction) {

  // Set up QuicStatsGatherer with required access logger, stream info, headers and trailers.
  std::shared_ptr<AccessLog::MockInstance> mock_logger(new NiceMock<AccessLog::MockInstance>());
  std::list<AccessLog::InstanceSharedPtr> loggers = {mock_logger};
  Event::GlobalTimeSystem test_time_;
  Envoy::StreamInfo::StreamInfoImpl stream_info{Http::Protocol::Http2, test_time_.timeSystem(),
                                                nullptr,
                                                StreamInfo::FilterState::LifeSpan::FilterChain};
  quic_stream_->statsGatherer()->setAccessLogHandlers(loggers);
  quic_stream_->setDeferredLoggingHeadersAndTrailers(nullptr, nullptr, nullptr, stream_info);

  receiveRequest(request_body_, false, request_body_.size() * 2);
  quic_stream_->encodeHeaders(response_headers_, /*end_stream=*/true);
  EXPECT_CALL(quic_session_, MaybeSendStopSendingFrame(_, _));
  EXPECT_CALL(stream_callbacks_, onResetStream(Http::StreamResetReason::LocalReset, _));
  // The stats gatherer has outstanding bytes that have not been acked.
  EXPECT_GT(quic_stream_->statsGatherer()->bytesOutstanding(), 0);
  // Close the stream; incoming acks will no longer invoke the stats gatherer but
  // the stats gatherer should log on stream close despite not receiving final downstream ack.
  EXPECT_CALL(*mock_logger, log(_, _));
  quic_stream_->resetStream(Http::StreamResetReason::LocalRefusedStreamReset);
}

TEST_F(EnvoyQuicServerStreamTest, MetadataNotSupported) {
  Http::MetadataMap metadata_map = {{"key", "value"}};
  Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
  Http::MetadataMapVector metadata_map_vector;
  metadata_map_vector.push_back(std::move(metadata_map_ptr));
  quic_stream_->encodeMetadata(metadata_map_vector);
  EXPECT_EQ(1,
            TestUtility::findCounter(listener_config_.store_, "http3.metadata_not_supported_error")
                ->value());
  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
}

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
TEST_F(EnvoyQuicServerStreamTest, EncodeCapsule) {
  setUpCapsuleProtocol(false, true);
  Buffer::OwnedImpl buffer(capsule_fragment_);
  EXPECT_CALL(quic_connection_, SendMessage(_, _, _))
      .WillOnce([this](quic::QuicMessageId, absl::Span<quiche::QuicheMemSlice> message, bool) {
        EXPECT_EQ(message.data()->AsStringView(), datagram_fragment_);
        return quic::MESSAGE_STATUS_SUCCESS;
      });
  quic_stream_->encodeData(buffer, /*end_stream=*/true);
}

TEST_F(EnvoyQuicServerStreamTest, DecodeHttp3Datagram) {
  setUpCapsuleProtocol(true, false);
  EXPECT_CALL(stream_decoder_, decodeData(BufferStringEqual(capsule_fragment_), _));
  quic_session_.OnMessageReceived(datagram_fragment_);
}
#endif

TEST_F(EnvoyQuicServerStreamTest, RegularHeaderBeforePseudoHeader) {
  quiche::HttpHeaderBlock spdy_headers;
  spdy_headers["foo"] = "bar";
  spdy_headers[":authority"] = host_;
  spdy_headers[":method"] = "GET";
  spdy_headers[":path"] = "/";
  spdy_headers[":scheme"] = "https";
  std::string payload = spdyHeaderToHttp3StreamPayload(spdy_headers);
  quic::QuicStreamFrame frame(stream_id_, true, 0, payload);
  EXPECT_CALL(stream_callbacks_, onResetStream(Http::StreamResetReason::ProtocolError, _));
  EXPECT_CALL(quic_session_, MaybeSendStopSendingFrame(_, _));
  EXPECT_CALL(quic_session_, MaybeSendRstStreamFrame(_, _, _));
  quic_stream_->OnStreamFrame(frame);
}

TEST_F(EnvoyQuicServerStreamTest, DuplicatedPathHeader) {
  quic::QuicHeaderList header_list;
  header_list.OnHeader(":authority", "www.google.com:4433");
  header_list.OnHeader(":method", "GET");
  header_list.OnHeader(":scheme", "https");
  header_list.OnHeader(":path", "/");
  header_list.OnHeader(":path", "/");
  header_list.OnHeaderBlockEnd(0, 0);

  EXPECT_CALL(stream_callbacks_, onResetStream(Http::StreamResetReason::ProtocolError, _));
  EXPECT_CALL(quic_session_, MaybeSendStopSendingFrame(_, _));
  EXPECT_CALL(quic_session_, MaybeSendRstStreamFrame(_, _, _));
  quic_stream_->OnStreamHeaderList(true, 0, header_list);
}

} // namespace Quic
} // namespace Envoy
