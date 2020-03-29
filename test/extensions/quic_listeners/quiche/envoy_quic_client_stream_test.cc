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
using testing::Return;

class EnvoyQuicClientStreamTest : public testing::TestWithParam<bool> {
public:
  EnvoyQuicClientStreamTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher()),
        connection_helper_(*dispatcher_),
        alarm_factory_(*dispatcher_, *connection_helper_.GetClock()), quic_version_([]() {
          SetQuicReloadableFlag(quic_enable_version_draft_27, GetParam());
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
        stream_id_(quic_version_.transport_version == quic::QUIC_VERSION_IETF_DRAFT_27 ? 4u : 5u),
        quic_stream_(new EnvoyQuicClientStream(stream_id_, &quic_session_, quic::BIDIRECTIONAL)),
        request_headers_{{":authority", host_}, {":method", "POST"}, {":path", "/"}} {
    quic_stream_->setResponseDecoder(stream_decoder_);
    quic_stream_->addCallbacks(stream_callbacks_);
    quic_session_.ActivateStream(std::unique_ptr<EnvoyQuicClientStream>(quic_stream_));
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
    quic_connection_->setUpConnectionSocket();
    response_headers_.OnHeaderBlockStart();
    response_headers_.OnHeader(":status", "200");
    response_headers_.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0,
                                       /*compressed_header_bytes=*/0);

    trailers_.OnHeaderBlockStart();
    trailers_.OnHeader("key1", "value1");
    if (quic_version_.transport_version != quic::QUIC_VERSION_IETF_DRAFT_27) {
      // ":final-offset" is required and stripped off by quic.
      trailers_.OnHeader(":final-offset", absl::StrCat("", response_body_.length()));
    }
    trailers_.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0, /*compressed_header_bytes=*/0);
  }

  void TearDown() override {
    if (quic_connection_->connected()) {
      quic_connection_->CloseConnection(
          quic::QUIC_NO_ERROR, "Closed by application",
          quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
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
  quic::QuicHeaderList response_headers_;
  quic::QuicHeaderList trailers_;
  Buffer::OwnedImpl request_body_{"Hello world"};
  std::string response_body_{"OK\n"};
};

INSTANTIATE_TEST_SUITE_P(EnvoyQuicClientStreamTests, EnvoyQuicClientStreamTest,
                         testing::ValuesIn({true, false}));

TEST_P(EnvoyQuicClientStreamTest, PostRequestAndResponse) {
  EXPECT_EQ(absl::nullopt, quic_stream_->http1StreamEncoderOptions());
  quic_stream_->encodeHeaders(request_headers_, false);
  quic_stream_->encodeData(request_body_, true);

  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([](const Http::ResponseHeaderMapPtr& headers, bool) {
        EXPECT_EQ("200", headers->Status()->value().getStringView());
      }));
  quic_stream_->OnStreamHeaderList(/*fin=*/false, response_headers_.uncompressed_header_bytes(),
                                   response_headers_);
  EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());

  EXPECT_CALL(stream_decoder_, decodeData(_, _))
      .Times(testing::AtMost(2))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool finished_reading) {
        EXPECT_EQ(response_body_, buffer.toString());
        EXPECT_FALSE(finished_reading);
      }))
      // Depends on QUIC version, there may be an empty STREAM_FRAME with FIN. But
      // since there is trailers, finished_reading should always be false.
      .WillOnce(Invoke([](Buffer::Instance& buffer, bool finished_reading) {
        EXPECT_FALSE(finished_reading);
        EXPECT_EQ(0, buffer.length());
      }));
  std::string data = response_body_;
  if (quic_version_.transport_version == quic::QUIC_VERSION_IETF_DRAFT_27) {
    std::unique_ptr<char[]> data_buffer;
    quic::QuicByteCount data_frame_header_length =
        quic::HttpEncoder::SerializeDataFrameHeader(response_body_.length(), &data_buffer);
    quiche::QuicheStringPiece data_frame_header(data_buffer.get(), data_frame_header_length);
    data = absl::StrCat(data_frame_header, response_body_);
  }
  quic::QuicStreamFrame frame(stream_id_, false, 0, data);
  quic_stream_->OnStreamFrame(frame);

  EXPECT_CALL(stream_decoder_, decodeTrailers_(_))
      .WillOnce(Invoke([](const Http::ResponseTrailerMapPtr& headers) {
        Http::LowerCaseString key1("key1");
        Http::LowerCaseString key2(":final-offset");
        EXPECT_EQ("value1", headers->get(key1)->value().getStringView());
        EXPECT_EQ(nullptr, headers->get(key2));
      }));
  quic_stream_->OnStreamHeaderList(/*fin=*/true, trailers_.uncompressed_header_bytes(), trailers_);
}

TEST_P(EnvoyQuicClientStreamTest, OutOfOrderTrailers) {
  if (quic::VersionUsesHttp3(quic_version_.transport_version)) {
    EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
    return;
  }
  quic_stream_->encodeHeaders(request_headers_, true);
  EXPECT_CALL(stream_decoder_, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([](const Http::ResponseHeaderMapPtr& headers, bool) {
        EXPECT_EQ("200", headers->Status()->value().getStringView());
      }));
  quic_stream_->OnStreamHeaderList(/*fin=*/false, response_headers_.uncompressed_header_bytes(),
                                   response_headers_);
  EXPECT_TRUE(quic_stream_->FinishedReadingHeaders());

  // Trailer should be delivered to HCM later after body arrives.
  quic_stream_->OnStreamHeaderList(/*fin=*/true, trailers_.uncompressed_header_bytes(), trailers_);

  std::string data = response_body_;
  if (quic_version_.transport_version == quic::QUIC_VERSION_IETF_DRAFT_27) {
    std::unique_ptr<char[]> data_buffer;
    quic::QuicByteCount data_frame_header_length =
        quic::HttpEncoder::SerializeDataFrameHeader(response_body_.length(), &data_buffer);
    quiche::QuicheStringPiece data_frame_header(data_buffer.get(), data_frame_header_length);
    data = absl::StrCat(data_frame_header, response_body_);
  }
  quic::QuicStreamFrame frame(stream_id_, false, 0, data);
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
        EXPECT_EQ("value1", headers->get(key1)->value().getStringView());
        EXPECT_EQ(nullptr, headers->get(key2));
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
  quic_stream_->encodeHeaders(request_headers_, /*end_stream=*/false);
  // Encode 32kB request body. first 16KB should be written out right away. The
  // rest should be buffered. The high watermark is 16KB, so this call should
  // make the send buffer reach its high watermark.
  std::string request(32 * 1024 + 1, 'a');
  Buffer::OwnedImpl buffer(request);
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
    std::string rest_request(1, 'a');
    Buffer::OwnedImpl buffer(rest_request);
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
  EXPECT_CALL(stream_callbacks_, onResetStream(_, _));
}

} // namespace Quic
} // namespace Envoy
