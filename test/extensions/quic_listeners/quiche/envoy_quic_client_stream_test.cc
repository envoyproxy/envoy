#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"
#include "extensions/quic_listeners/quiche/envoy_quic_client_connection.h"
#include "extensions/quic_listeners/quiche/envoy_quic_client_stream.h"
#include "extensions/quic_listeners/quiche/envoy_quic_connection_helper.h"
#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"

#include "test/extensions/quic_listeners/quiche/test_utils.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {

using testing::_;
using testing::Invoke;

class EnvoyQuicClientStreamTest : public testing::TestWithParam<bool> {
public:
  EnvoyQuicClientStreamTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")),
        connection_helper_(*dispatcher_),
        alarm_factory_(*dispatcher_, *connection_helper_.GetClock()), quic_version_([]() {
          SetQuicReloadableFlag(quic_disable_version_draft_29, !GetParam());
          SetQuicReloadableFlag(quic_disable_version_draft_27, !GetParam());
          return quic::CurrentSupportedVersions()[0];
        }()),
        peer_addr_(Network::Utility::getAddressWithPort(*Network::Utility::getIpv6LoopbackAddress(),
                                                        12345)),
        self_addr_(Network::Utility::getAddressWithPort(*Network::Utility::getIpv6LoopbackAddress(),
                                                        54321)),
        quic_connection_(new EnvoyQuicClientConnection(
            quic::test::TestConnectionId(), connection_helper_, alarm_factory_, &writer_,
            /*owns_writer=*/false, {quic_version_}, *dispatcher_,
            createConnectionSocket(peer_addr_, self_addr_, nullptr))),
        quic_session_(quic_config_, {quic_version_}, quic_connection_, *dispatcher_,
                      quic_config_.GetInitialStreamFlowControlWindowToSend() * 2),
        stream_id_(quic::VersionUsesHttp3(quic_version_.transport_version) ? 4u : 5u),
        quic_stream_(new EnvoyQuicClientStream(stream_id_, &quic_session_, quic::BIDIRECTIONAL)),
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
    setQuicConfigWithDefaultValues(quic_session_.config());
    quic_session_.OnConfigNegotiated();
    quic_connection_->setUpConnectionSocket();
    response_headers_.OnHeaderBlockStart();
    response_headers_.OnHeader(":status", "200");
    response_headers_.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0,
                                       /*compressed_header_bytes=*/0);
    spdy_response_headers_[":status"] = "200";

    trailers_.OnHeaderBlockStart();
    trailers_.OnHeader("key1", "value1");
    if (!quic::VersionUsesHttp3(quic_version_.transport_version)) {
      // ":final-offset" is required and stripped off by quic.
      trailers_.OnHeader(":final-offset", absl::StrCat("", response_body_.length()));
    }
    trailers_.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0, /*compressed_header_bytes=*/0);
    spdy_trailers_["key1"] = "value1";
  }

  void TearDown() override {
    if (quic_connection_->connected()) {
      quic_connection_->CloseConnection(
          quic::QUIC_NO_ERROR, "Closed by application",
          quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    }
  }

  std::string bodyToStreamPayload(const std::string& body) {
    if (!quic::VersionUsesHttp3(quic_version_.transport_version)) {
      return body;
    }
    return bodyToHttp3StreamPayload(body);
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
    if (quic::VersionUsesHttp3(quic_version_.transport_version)) {
      std::string data = absl::StrCat(spdyHeaderToHttp3StreamPayload(spdy_response_headers_),
                                      bodyToStreamPayload(payload));
      quic::QuicStreamFrame frame(stream_id_, fin, offset, data);
      quic_stream_->OnStreamFrame(frame);
      EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());
      return offset + data.length();
    }
    quic_stream_->OnStreamHeaderList(/*fin=*/false, response_headers_.uncompressed_header_bytes(),
                                     response_headers_);

    quic::QuicStreamFrame frame(stream_id_, fin, offset, payload);
    quic_stream_->OnStreamFrame(frame);
    EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());
    return offset + payload.length();
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
  EnvoyQuicClientConnection* quic_connection_;
  MockEnvoyQuicClientSession quic_session_;
  quic::QuicStreamId stream_id_;
  EnvoyQuicClientStream* quic_stream_;
  Http::MockResponseDecoder stream_decoder_;
  Http::MockStreamCallbacks stream_callbacks_;
  std::string host_{"www.abc.com"};
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  quic::QuicHeaderList response_headers_;
  spdy::SpdyHeaderBlock spdy_response_headers_;
  quic::QuicHeaderList trailers_;
  spdy::SpdyHeaderBlock spdy_trailers_;
  Buffer::OwnedImpl request_body_{"Hello world"};
  std::string response_body_{"OK\n"};
};

INSTANTIATE_TEST_SUITE_P(EnvoyQuicClientStreamTests, EnvoyQuicClientStreamTest,
                         testing::ValuesIn({true, false}));

TEST_P(EnvoyQuicClientStreamTest, GetRequestAndHeaderOnlyResponse) {
  const auto result = quic_stream_->encodeHeaders(request_headers_, /*end_stream=*/true);
  EXPECT_TRUE(result.ok());

  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/!quic::VersionUsesHttp3(
                                                  quic_version_.transport_version)))
      .WillOnce(Invoke([](const Http::ResponseHeaderMapPtr& headers, bool) {
        EXPECT_EQ("200", headers->getStatusValue());
      }));
  if (quic::VersionUsesHttp3(quic_version_.transport_version)) {
    EXPECT_CALL(stream_decoder_, decodeData(BufferStringEqual(""), /*end_stream=*/true));
    std::string payload = spdyHeaderToHttp3StreamPayload(spdy_response_headers_);
    quic::QuicStreamFrame frame(stream_id_, true, 0, payload);
    quic_stream_->OnStreamFrame(frame);
  } else {
    quic_stream_->OnStreamHeaderList(/*fin=*/true, response_headers_.uncompressed_header_bytes(),
                                     response_headers_);
  }
  EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());
}

TEST_P(EnvoyQuicClientStreamTest, PostRequestAndResponse) {
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
  if (quic::VersionUsesHttp3(quic_version_.transport_version)) {
    std::string more_response_body{"bbb"};
    EXPECT_CALL(stream_decoder_, decodeData(_, _))
        .WillOnce(Invoke([&](Buffer::Instance& buffer, bool finished_reading) {
          EXPECT_EQ(more_response_body, buffer.toString());
          EXPECT_EQ(false, finished_reading);
        }));
    std::string payload = absl::StrCat(bodyToStreamPayload(more_response_body),
                                       spdyHeaderToHttp3StreamPayload(spdy_trailers_));
    quic::QuicStreamFrame frame(stream_id_, true, offset, payload);
    quic_stream_->OnStreamFrame(frame);
  } else {
    quic_stream_->OnStreamHeaderList(
        /*fin=*/!quic::VersionUsesHttp3(quic_version_.transport_version),
        trailers_.uncompressed_header_bytes(), trailers_);
  }
}

TEST_P(EnvoyQuicClientStreamTest, PostRequestAnd100Continue) {
  const auto result = quic_stream_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(result.ok());

  EXPECT_CALL(stream_decoder_, decode100ContinueHeaders_(_))
      .WillOnce(Invoke([this](const Http::ResponseHeaderMapPtr& headers) {
        EXPECT_EQ("100", headers->getStatusValue());
        EXPECT_EQ("0", headers->get(Http::LowerCaseString("i"))[0]->value().getStringView());
        quic_stream_->encodeData(request_body_, true);
      }));
  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([](const Http::ResponseHeaderMapPtr& headers, bool) {
        EXPECT_EQ("103", headers->getStatusValue());
        EXPECT_EQ("1", headers->get(Http::LowerCaseString("i"))[0]->value().getStringView());
      }));
  size_t offset = 0;
  size_t i = 0;
  // Receive several 10x headers, only the first 100 Continue header should be
  // delivered.
  for (const std::string& status : {"100", "103", "100"}) {
    if (quic::VersionUsesHttp3(quic_version_.transport_version)) {
      spdy::SpdyHeaderBlock continue_header;
      continue_header[":status"] = status;
      continue_header["i"] = absl::StrCat("", i++);
      std::string data = spdyHeaderToHttp3StreamPayload(continue_header);
      quic::QuicStreamFrame frame(stream_id_, false, offset, data);
      quic_stream_->OnStreamFrame(frame);
      offset += data.length();
    } else {
      quic::QuicHeaderList continue_header;
      continue_header.OnHeaderBlockStart();
      continue_header.OnHeader(":status", status);
      continue_header.OnHeader("i", absl::StrCat("", i++));
      continue_header.OnHeaderBlockEnd(0, 0);
      quic_stream_->OnStreamHeaderList(/*fin=*/false, continue_header.uncompressed_header_bytes(),
                                       continue_header);
    }
  }

  receiveResponse(response_body_, true, offset);
}

TEST_P(EnvoyQuicClientStreamTest, ResetUpon101SwitchProtocol) {
  const auto result = quic_stream_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(result.ok());

  EXPECT_CALL(stream_callbacks_, onResetStream(Http::StreamResetReason::LocalReset, _));
  // Receive several 10x headers, only the first 100 Continue header should be
  // delivered.
  if (quic::VersionUsesHttp3(quic_version_.transport_version)) {
    spdy::SpdyHeaderBlock continue_header;
    continue_header[":status"] = "101";
    std::string data = spdyHeaderToHttp3StreamPayload(continue_header);
    quic::QuicStreamFrame frame(stream_id_, false, 0u, data);
    quic_stream_->OnStreamFrame(frame);
  } else {
    quic::QuicHeaderList continue_header;
    continue_header.OnHeaderBlockStart();
    continue_header.OnHeader(":status", "101");
    continue_header.OnHeaderBlockEnd(0, 0);
    quic_stream_->OnStreamHeaderList(/*fin=*/false, continue_header.uncompressed_header_bytes(),
                                     continue_header);
  }
}

TEST_P(EnvoyQuicClientStreamTest, OutOfOrderTrailers) {
  if (quic::VersionUsesHttp3(quic_version_.transport_version)) {
    EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
    return;
  }
  const auto result = quic_stream_->encodeHeaders(request_headers_, true);
  EXPECT_TRUE(result.ok());
  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([](const Http::ResponseHeaderMapPtr& headers, bool) {
        EXPECT_EQ("200", headers->getStatusValue());
      }));
  quic_stream_->OnStreamHeaderList(/*fin=*/false, response_headers_.uncompressed_header_bytes(),
                                   response_headers_);
  EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());

  // Trailer should be delivered to HCM later after body arrives.
  quic_stream_->OnStreamHeaderList(/*fin=*/true, trailers_.uncompressed_header_bytes(), trailers_);

  quic::QuicStreamFrame frame(stream_id_, false, 0, response_body_);
  EXPECT_CALL(stream_decoder_, decodeData(_, _))
      .Times(testing::AtMost(2))
      .WillOnce(Invoke([this](Buffer::Instance& buffer, bool finished_reading) {
        EXPECT_EQ(response_body_, buffer.toString());
        EXPECT_FALSE(finished_reading);
      }))
      // Depends on QUIC version, there may be an empty STREAM_FRAME with FIN. But
      // since there is trailers, finished_reading should always be false.
      .WillOnce(Invoke([](Buffer::Instance& buffer, bool finished_reading) {
        EXPECT_FALSE(finished_reading);
        EXPECT_EQ(0, buffer.length());
      }));

  EXPECT_CALL(stream_decoder_, decodeTrailers_(_))
      .WillOnce(Invoke([](const Http::ResponseTrailerMapPtr& headers) {
        Http::LowerCaseString key1("key1");
        Http::LowerCaseString key2(":final-offset");
        EXPECT_EQ("value1", headers->get(key1)[0]->value().getStringView());
        EXPECT_TRUE(headers->get(key2).empty());
      }));
  quic_stream_->OnStreamFrame(frame);
}

TEST_P(EnvoyQuicClientStreamTest, WatermarkSendBuffer) {
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
// limits. Only IETF QUIC writes them on data stream, gQUIC writes them on dedicated headers stream
// and only contributes to connection watermark buffer.
TEST_P(EnvoyQuicClientStreamTest, HeadersContributeToWatermarkIquic) {
  if (!quic::VersionUsesHttp3(quic_version_.transport_version)) {
    EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
    return;
  }

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

TEST_P(EnvoyQuicClientStreamTest, ResetStream) {
  EXPECT_CALL(stream_callbacks_, onResetStream(Http::StreamResetReason::LocalReset, _));
  quic_stream_->resetStream(Http::StreamResetReason::LocalReset);
  EXPECT_TRUE(quic_stream_->rst_sent());
}

TEST_P(EnvoyQuicClientStreamTest, ReceiveResetStream) {
  EXPECT_CALL(stream_callbacks_, onResetStream(Http::StreamResetReason::RemoteReset, _));
  quic_stream_->OnStreamReset(quic::QuicRstStreamFrame(
      quic::kInvalidControlFrameId, quic_stream_->id(), quic::QUIC_STREAM_NO_ERROR, 0));
  EXPECT_TRUE(quic_stream_->rst_received());
}

TEST_P(EnvoyQuicClientStreamTest, CloseConnectionDuringDecodingHeader) {
  const auto result = quic_stream_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(result.ok());
  quic_stream_->encodeData(request_body_, true);

  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/!quic::VersionUsesHttp3(
                                                  quic_version_.transport_version)))
      .WillOnce(Invoke([this](const Http::ResponseHeaderMapPtr&, bool) {
        quic_connection_->CloseConnection(
            quic::QUIC_NO_ERROR, "Closed in decodeHeaders",
            quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      }));
  if (quic::VersionUsesHttp3(quic_version_.transport_version)) {
    // onResetStream() callback should be triggered because end_stream is
    // not decoded with header.
    EXPECT_CALL(stream_callbacks_,
                onResetStream(Http::StreamResetReason::ConnectionTermination, _));
    std::string data = spdyHeaderToHttp3StreamPayload(spdy_response_headers_);
    quic::QuicStreamFrame frame(stream_id_, true, 0, data);
    quic_stream_->OnStreamFrame(frame);
  } else {
    // onResetStream() callback shouldn't be triggered because end_stream is
    // already decoded.
    quic_stream_->OnStreamHeaderList(/*fin=*/true, response_headers_.uncompressed_header_bytes(),
                                     response_headers_);
  }
}

TEST_P(EnvoyQuicClientStreamTest, CloseConnectionDuringDecodingDataWithEndStream) {
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
  if (quic::VersionUsesHttp3(quic_version_.transport_version)) {
    std::string data = absl::StrCat(spdyHeaderToHttp3StreamPayload(spdy_response_headers_),
                                    bodyToStreamPayload(response_body_));
    quic::QuicStreamFrame frame(stream_id_, true, 0, data);
    quic_stream_->OnStreamFrame(frame);
  } else {
    quic_stream_->OnStreamHeaderList(/*fin=*/false, response_headers_.uncompressed_header_bytes(),
                                     response_headers_);

    quic::QuicStreamFrame frame(stream_id_, true, 0, response_body_);
    quic_stream_->OnStreamFrame(frame);
  }
}

TEST_P(EnvoyQuicClientStreamTest, CloseConnectionDuringDecodingDataWithTrailer) {
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
  if (quic::VersionUsesHttp3(quic_version_.transport_version)) {
    std::string data = absl::StrCat(spdyHeaderToHttp3StreamPayload(spdy_response_headers_),
                                    bodyToStreamPayload(response_body_),
                                    spdyHeaderToHttp3StreamPayload(spdy_trailers_));
    quic::QuicStreamFrame frame(stream_id_, true, 0, data);
    quic_stream_->OnStreamFrame(frame);
  } else {
    quic_stream_->OnStreamHeaderList(/*fin=*/false, response_headers_.uncompressed_header_bytes(),
                                     response_headers_);

    quic::QuicStreamFrame frame(stream_id_, /*fin=*/false, 0, response_body_);
    quic_stream_->OnStreamFrame(frame);
    quic_stream_->OnStreamHeaderList(
        /*fin=*/!quic::VersionUsesHttp3(quic_version_.transport_version),
        trailers_.uncompressed_header_bytes(), trailers_);
  }
}

TEST_P(EnvoyQuicClientStreamTest, CloseConnectionDuringDecodingTrailer) {
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
  if (quic::VersionUsesHttp3(quic_version_.transport_version)) {
    std::string payload = spdyHeaderToHttp3StreamPayload(spdy_trailers_);
    quic::QuicStreamFrame frame(stream_id_, true, offset, payload);
    quic_stream_->OnStreamFrame(frame);
  } else {
    quic_stream_->OnStreamHeaderList(
        /*fin=*/!quic::VersionUsesHttp3(quic_version_.transport_version),
        trailers_.uncompressed_header_bytes(), trailers_);
  }
}

} // namespace Quic
} // namespace Envoy
