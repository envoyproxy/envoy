#include <string>

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/test_tools/quic_connection_peer.h"
#pragma GCC diagnostic pop

#include "common/event/libevent_scheduler.h"
#include "common/http/headers.h"

#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"
#include "extensions/quic_listeners/quiche/envoy_quic_connection_helper.h"
#include "extensions/quic_listeners/quiche/envoy_quic_server_connection.h"
#include "extensions/quic_listeners/quiche/envoy_quic_server_session.h"
#include "extensions/quic_listeners/quiche/envoy_quic_server_stream.h"

#include "test/extensions/quic_listeners/quiche/test_utils.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Quic {

class EnvoyQuicServerStreamTest : public testing::TestWithParam<bool> {
public:
  EnvoyQuicServerStreamTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")),
        connection_helper_(*dispatcher_),
        alarm_factory_(*dispatcher_, *connection_helper_.GetClock()), quic_version_([]() {
          SetQuicReloadableFlag(quic_enable_version_draft_29, GetParam());
          SetQuicReloadableFlag(quic_disable_version_draft_27, !GetParam());
          SetQuicReloadableFlag(quic_disable_version_draft_25, !GetParam());
          return quic::CurrentSupportedVersions()[0];
        }()),
        listener_stats_({ALL_LISTENER_STATS(POOL_COUNTER(listener_config_.listenerScope()),
                                            POOL_GAUGE(listener_config_.listenerScope()),
                                            POOL_HISTOGRAM(listener_config_.listenerScope()))}),
        quic_connection_(quic::test::TestConnectionId(),
                         quic::QuicSocketAddress(quic::QuicIpAddress::Any6(), 12345),
                         connection_helper_, alarm_factory_, &writer_,
                         /*owns_writer=*/false, {quic_version_}, *listener_config_.socket_),
        quic_session_(quic_config_, {quic_version_}, &quic_connection_, *dispatcher_,
                      quic_config_.GetInitialStreamFlowControlWindowToSend() * 2),
        stream_id_(VersionUsesHttp3(quic_version_.transport_version) ? 4u : 5u),
        quic_stream_(new EnvoyQuicServerStream(stream_id_, &quic_session_, quic::BIDIRECTIONAL)),
        response_headers_{{":status", "200"}, {"response-key", "response-value"}},
        response_trailers_{{"trailer-key", "trailer-value"}} {
    quic_stream_->setRequestDecoder(stream_decoder_);
    quic_stream_->addCallbacks(stream_callbacks_);
    quic::test::QuicConnectionPeer::SetAddressValidated(&quic_connection_);
    quic_session_.ActivateStream(std::unique_ptr<EnvoyQuicServerStream>(quic_stream_));
    EXPECT_CALL(quic_session_, ShouldYield(_)).WillRepeatedly(testing::Return(false));
    EXPECT_CALL(quic_session_, WritevData(_, _, _, _, _, _))
        .WillRepeatedly(Invoke([](quic::QuicStreamId, size_t write_length, quic::QuicStreamOffset,
                                  quic::StreamSendingState state, bool,
                                  quiche::QuicheOptional<quic::EncryptionLevel>) {
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
    setQuicConfigWithDefaultValues(quic_session_.config());
    quic_session_.OnConfigNegotiated();
    request_headers_.OnHeaderBlockStart();
    request_headers_.OnHeader(":authority", host_);
    request_headers_.OnHeader(":method", "POST");
    request_headers_.OnHeader(":path", "/");
    request_headers_.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0,
                                      /*compressed_header_bytes=*/0);

    trailers_.OnHeaderBlockStart();
    trailers_.OnHeader("key1", "value1");
    if (!quic::VersionUsesHttp3(quic_version_.transport_version)) {
      // ":final-offset" is required and stripped off by quic.
      trailers_.OnHeader(":final-offset", absl::StrCat("", request_body_.length()));
    }
    trailers_.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0, /*compressed_header_bytes=*/0);
  }

  void TearDown() override {
    if (quic_connection_.connected()) {
      quic_session_.close(Network::ConnectionCloseType::NoFlush);
    }
  }

  std::string bodyToStreamPayload(const std::string& body) {
    std::string data = body;
    if (quic::VersionUsesHttp3(quic_version_.transport_version)) {
      std::unique_ptr<char[]> data_buffer;
      quic::QuicByteCount data_frame_header_length =
          quic::HttpEncoder::SerializeDataFrameHeader(body.length(), &data_buffer);
      quiche::QuicheStringPiece data_frame_header(data_buffer.get(), data_frame_header_length);
      data = absl::StrCat(data_frame_header, body);
    }
    return data;
  }

  size_t sendRequest(const std::string& payload, bool fin, size_t decoder_buffer_high_watermark) {
    EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
        .WillOnce(Invoke([this](const Http::RequestHeaderMapPtr& headers, bool) {
          EXPECT_EQ(host_, headers->getHostValue());
          EXPECT_EQ("/", headers->getPathValue());
          EXPECT_EQ(Http::Headers::get().MethodValues.Post, headers->getMethodValue());
        }));
    quic_stream_->OnStreamHeaderList(/*fin=*/false, request_headers_.uncompressed_header_bytes(),
                                     request_headers_);
    EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());

    EXPECT_CALL(stream_decoder_, decodeData(_, _))
        .WillOnce(Invoke([&](Buffer::Instance& buffer, bool finished_reading) {
          EXPECT_EQ(payload, buffer.toString());
          EXPECT_EQ(fin, finished_reading);
          if (!finished_reading && buffer.length() > decoder_buffer_high_watermark) {
            quic_stream_->readDisable(true);
          }
        }));
    std::string data = bodyToStreamPayload(payload);
    quic::QuicStreamFrame frame(stream_id_, fin, 0, data);
    quic_stream_->OnStreamFrame(frame);
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
  testing::NiceMock<Network::MockListenerConfig> listener_config_;
  Server::ListenerStats listener_stats_;
  EnvoyQuicServerConnection quic_connection_;
  MockEnvoyQuicSession quic_session_;
  quic::QuicStreamId stream_id_;
  EnvoyQuicServerStream* quic_stream_;
  Http::MockRequestDecoder stream_decoder_;
  Http::MockStreamCallbacks stream_callbacks_;
  quic::QuicHeaderList request_headers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  quic::QuicHeaderList trailers_;
  std::string host_{"www.abc.com"};
  std::string request_body_{"Hello world"};
};

INSTANTIATE_TEST_SUITE_P(EnvoyQuicServerStreamTests, EnvoyQuicServerStreamTest,
                         testing::ValuesIn({true, false}));

TEST_P(EnvoyQuicServerStreamTest, GetRequestAndResponse) {
  quic::QuicHeaderList request_headers;
  request_headers.OnHeaderBlockStart();
  request_headers.OnHeader(":authority", host_);
  request_headers.OnHeader(":method", "GET");
  request_headers.OnHeader(":path", "/");
  request_headers.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0,
                                   /*compressed_header_bytes=*/0);

  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/true))
      .WillOnce(Invoke([this](const Http::RequestHeaderMapPtr& headers, bool) {
        EXPECT_EQ(host_, headers->getHostValue());
        EXPECT_EQ("/", headers->getPathValue());
        EXPECT_EQ(Http::Headers::get().MethodValues.Get, headers->getMethodValue());
      }));
  quic_stream_->OnStreamHeaderList(/*fin=*/true, request_headers.uncompressed_header_bytes(),
                                   request_headers);
  EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());
  quic_stream_->encodeHeaders(response_headers_, /*end_stream=*/true);
}

TEST_P(EnvoyQuicServerStreamTest, PostRequestAndResponse) {
  EXPECT_EQ(absl::nullopt, quic_stream_->http1StreamEncoderOptions());
  sendRequest(request_body_, true, request_body_.size() * 2);
  quic_stream_->encodeHeaders(response_headers_, /*end_stream=*/false);
  quic_stream_->encodeTrailers(response_trailers_);
}

TEST_P(EnvoyQuicServerStreamTest, DecodeHeadersBodyAndTrailers) {
  sendRequest(request_body_, false, request_body_.size() * 2);
  EXPECT_CALL(stream_decoder_, decodeTrailers_(_))
      .WillOnce(Invoke([](const Http::RequestTrailerMapPtr& headers) {
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
  if (quic::VersionUsesHttp3(quic_version_.transport_version)) {
    return;
  }
  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([this](const Http::RequestHeaderMapPtr& headers, bool) {
        EXPECT_EQ(host_, headers->getHostValue());
        EXPECT_EQ("/", headers->getPathValue());
        EXPECT_EQ(Http::Headers::get().MethodValues.Post, headers->getMethodValue());
      }));
  quic_stream_->OnStreamHeaderList(/*fin=*/false, request_headers_.uncompressed_header_bytes(),
                                   request_headers_);
  EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());

  // Trailer should be delivered to HCM later after body arrives.
  quic_stream_->OnStreamHeaderList(/*fin=*/true, trailers_.uncompressed_header_bytes(), trailers_);

  std::string data = bodyToStreamPayload(request_body_);
  quic::QuicStreamFrame frame(stream_id_, false, 0, data);
  EXPECT_CALL(stream_decoder_, decodeData(_, _))
      .WillOnce(Invoke([this](Buffer::Instance& buffer, bool finished_reading) {
        EXPECT_EQ(request_body_, buffer.toString());
        EXPECT_FALSE(finished_reading);
      }));

  EXPECT_CALL(stream_decoder_, decodeTrailers_(_))
      .WillOnce(Invoke([](const Http::RequestTrailerMapPtr& headers) {
        Http::LowerCaseString key1("key1");
        Http::LowerCaseString key2(":final-offset");
        EXPECT_EQ("value1", headers->get(key1)->value().getStringView());
        EXPECT_EQ(nullptr, headers->get(key2));
      }));
  quic_stream_->OnStreamFrame(frame);
}

TEST_P(EnvoyQuicServerStreamTest, ReadDisableUponLargePost) {
  std::string large_request(1024, 'a');
  // Sending such large request will cause read to be disabled.
  size_t payload_offset = sendRequest(large_request, false, 512);
  EXPECT_FALSE(quic_stream_->HasBytesToRead());
  // Disable reading one more time.
  quic_stream_->readDisable(true);
  std::string second_part_request = bodyToStreamPayload("bbb");
  // Receiving more data shouldn't push the receiving pipe line as the stream
  // should have been marked blocked.
  quic::QuicStreamFrame frame(stream_id_, false, payload_offset, second_part_request);
  EXPECT_CALL(stream_decoder_, decodeData(_, _)).Times(0);
  quic_stream_->OnStreamFrame(frame);

  // Re-enable reading just once shouldn't unblock stream.
  quic_stream_->readDisable(false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // This data frame should also be buffered.
  std::string last_part_request = bodyToStreamPayload("ccc");
  quic::QuicStreamFrame frame2(stream_id_, true, payload_offset + second_part_request.length(),
                               last_part_request);
  quic_stream_->OnStreamFrame(frame2);

  // Unblock stream now. The remaining data in the receiving buffer should be
  // pushed to upstream.
  EXPECT_CALL(stream_decoder_, decodeData(_, _))
      .WillOnce(Invoke([](Buffer::Instance& buffer, bool finished_reading) {
        std::string rest_request = "bbbccc";
        EXPECT_EQ(rest_request.size(), buffer.length());
        EXPECT_EQ(rest_request, buffer.toString());
        EXPECT_TRUE(finished_reading);
      }));
  quic_stream_->readDisable(false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
}

// Tests that ReadDisable() doesn't cause re-entry of OnBodyAvailable().
TEST_P(EnvoyQuicServerStreamTest, ReadDisableAndReEnableImmediately) {
  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([this](const Http::RequestHeaderMapPtr& headers, bool) {
        EXPECT_EQ(host_, headers->getHostValue());
        EXPECT_EQ("/", headers->getPathValue());
        EXPECT_EQ(Http::Headers::get().MethodValues.Post, headers->getMethodValue());
      }));
  quic_stream_->OnStreamHeaderList(/*fin=*/false, request_headers_.uncompressed_header_bytes(),
                                   request_headers_);
  EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());

  std::string payload(1024, 'a');
  EXPECT_CALL(stream_decoder_, decodeData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool finished_reading) {
        EXPECT_EQ(payload, buffer.toString());
        EXPECT_FALSE(finished_reading);
        quic_stream_->readDisable(true);
        // Re-enable reading should not trigger another decodeData.
        quic_stream_->readDisable(false);
      }));
  std::string data = bodyToStreamPayload(payload);
  quic::QuicStreamFrame frame(stream_id_, false, 0, data);
  quic_stream_->OnStreamFrame(frame);

  std::string last_part_request = bodyToStreamPayload("bbb");
  quic::QuicStreamFrame frame2(stream_id_, true, data.length(), last_part_request);
  EXPECT_CALL(stream_decoder_, decodeData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool finished_reading) {
        EXPECT_EQ("bbb", buffer.toString());
        EXPECT_TRUE(finished_reading);
      }));

  quic_stream_->OnStreamFrame(frame2);
  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
}

// Tests that the stream with a send buffer whose high limit is 16k and low
// limit is 8k sends over 32kB response.
TEST_P(EnvoyQuicServerStreamTest, WatermarkSendBuffer) {
  sendRequest(request_body_, true, request_body_.size() * 2);

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
  EXPECT_TRUE(quic_stream_->flow_controller()->IsBlocked());

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

TEST_P(EnvoyQuicServerStreamTest, HeadersContributeToWatermarkIquic) {
  if (!quic::VersionUsesHttp3(quic_version_.transport_version)) {
    EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
    return;
  }

  sendRequest(request_body_, true, request_body_.size() * 2);

  // Bump connection flow control window large enough not to cause connection level flow control
  // blocked
  quic::QuicWindowUpdateFrame window_update(
      quic::kInvalidControlFrameId,
      quic::QuicUtils::GetInvalidStreamId(quic_version_.transport_version), 1024 * 1024);
  quic_session_.OnWindowUpdateFrame(window_update);

  // Make the stream blocked by congestion control.
  EXPECT_CALL(quic_session_, WritevData(_, _, _, _, _, _))
      .WillOnce(Invoke([](quic::QuicStreamId, size_t /*write_length*/, quic::QuicStreamOffset,
                          quic::StreamSendingState state, bool,
                          quiche::QuicheOptional<quic::EncryptionLevel>) {
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
      .WillOnce(Invoke([](quic::QuicStreamId, size_t write_length, quic::QuicStreamOffset,
                          quic::StreamSendingState state, bool,
                          quiche::QuicheOptional<quic::EncryptionLevel>) {
        return quic::QuicConsumedData{write_length, state != quic::NO_FIN};
      }));
  EXPECT_CALL(stream_callbacks_, onBelowWriteBufferLowWatermark());
  quic_session_.OnCanWrite();
  EXPECT_TRUE(quic_stream_->flow_controller()->IsBlocked());

  // Update flow control window to write all the buffered data.
  quic::QuicWindowUpdateFrame window_update1(quic::kInvalidControlFrameId, quic_stream_->id(),
                                             32 * 1024);
  quic_stream_->OnWindowUpdateFrame(window_update1);
  EXPECT_CALL(quic_session_, WritevData(_, _, _, _, _, _))
      .WillOnce(Invoke([](quic::QuicStreamId, size_t write_length, quic::QuicStreamOffset,
                          quic::StreamSendingState state, bool,
                          quiche::QuicheOptional<quic::EncryptionLevel>) {
        return quic::QuicConsumedData{write_length, state != quic::NO_FIN};
      }));
  quic_session_.OnCanWrite();
  // No data should be buffered at this point.

  EXPECT_CALL(quic_session_, WritevData(_, _, _, _, _, _))
      .WillRepeatedly(Invoke([](quic::QuicStreamId, size_t, quic::QuicStreamOffset,
                                quic::StreamSendingState state, bool,
                                quiche::QuicheOptional<quic::EncryptionLevel>) {
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

  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
}

} // namespace Quic
} // namespace Envoy
