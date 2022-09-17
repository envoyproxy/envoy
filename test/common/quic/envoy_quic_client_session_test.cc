#include "envoy/stats/stats_macros.h"

#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/quic/codec_impl.h"
#include "source/common/quic/envoy_quic_alarm_factory.h"
#include "source/common/quic/envoy_quic_client_connection.h"
#include "source/common/quic/envoy_quic_client_session.h"
#include "source/common/quic/envoy_quic_connection_helper.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_client_stream.h"

#include "test/common/quic/test_utils.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/quic/core/crypto/null_encrypter.h"
#include "quiche/quic/core/deterministic_connection_id_generator.h"
#include "quiche/quic/test_tools/crypto_test_utils.h"
#include "quiche/quic/test_tools/quic_session_peer.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Quic {

class TestEnvoyQuicClientConnection : public EnvoyQuicClientConnection {
public:
  TestEnvoyQuicClientConnection(const quic::QuicConnectionId& server_connection_id,
                                quic::QuicConnectionHelperInterface& helper,
                                quic::QuicAlarmFactory& alarm_factory,
                                quic::QuicPacketWriter& writer,
                                const quic::ParsedQuicVersionVector& supported_versions,
                                Event::Dispatcher& dispatcher,
                                Network::ConnectionSocketPtr&& connection_socket,
                                quic::ConnectionIdGeneratorInterface& generator)
      : EnvoyQuicClientConnection(server_connection_id, helper, alarm_factory, &writer, false,
                                  supported_versions, dispatcher, std::move(connection_socket),
                                  generator) {
    SetEncrypter(quic::ENCRYPTION_FORWARD_SECURE,
                 std::make_unique<quic::NullEncrypter>(quic::Perspective::IS_CLIENT));
    SetDefaultEncryptionLevel(quic::ENCRYPTION_FORWARD_SECURE);
  }

  MOCK_METHOD(void, SendConnectionClosePacket,
              (quic::QuicErrorCode, quic::QuicIetfTransportErrorCodes ietf_error,
               const std::string&));
  MOCK_METHOD(bool, SendControlFrame, (const quic::QuicFrame& frame));

  using EnvoyQuicClientConnection::connectionStats;
};

class EnvoyQuicClientSessionTest : public testing::TestWithParam<quic::ParsedQuicVersion> {
public:
  EnvoyQuicClientSessionTest()
      : api_(Api::createApiForTest(time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")), connection_helper_(*dispatcher_),
        alarm_factory_(*dispatcher_, *connection_helper_.GetClock()), quic_version_({GetParam()}),
        peer_addr_(Network::Utility::getAddressWithPort(*Network::Utility::getIpv6LoopbackAddress(),
                                                        12345)),
        self_addr_(Network::Utility::getAddressWithPort(*Network::Utility::getIpv6LoopbackAddress(),
                                                        54321)),
        quic_connection_(new TestEnvoyQuicClientConnection(
            quic::test::TestConnectionId(), connection_helper_, alarm_factory_, writer_,
            quic_version_, *dispatcher_, createConnectionSocket(peer_addr_, self_addr_, nullptr),
            connection_id_generator_)),
        crypto_config_(std::make_shared<quic::QuicCryptoClientConfig>(
            quic::test::crypto_test_utils::ProofVerifierForTesting())),
        quic_stat_names_(store_.symbolTable()),
        transport_socket_options_(std::make_shared<Network::TransportSocketOptionsImpl>()),
        envoy_quic_session_(quic_config_, quic_version_,
                            std::unique_ptr<TestEnvoyQuicClientConnection>(quic_connection_),
                            quic::QuicServerId("example.com", 443, false), crypto_config_, nullptr,
                            *dispatcher_,
                            /*send_buffer_limit*/ 1024 * 1024, crypto_stream_factory_,
                            quic_stat_names_, {}, store_, transport_socket_options_),
        stats_({ALL_HTTP3_CODEC_STATS(POOL_COUNTER_PREFIX(store_, "http3."),
                                      POOL_GAUGE_PREFIX(store_, "http3."))}),
        http_connection_(envoy_quic_session_, http_connection_callbacks_, stats_, http3_options_,
                         64 * 1024, 100) {
    EXPECT_EQ(time_system_.systemTime(), envoy_quic_session_.streamInfo().startTime());
    EXPECT_EQ(EMPTY_STRING, envoy_quic_session_.nextProtocol());
    EXPECT_EQ(Http::Protocol::Http3, http_connection_.protocol());

    time_system_.advanceTimeWait(std::chrono::milliseconds(1));
    ON_CALL(writer_, WritePacket(_, _, _, _, _))
        .WillByDefault(testing::Return(quic::WriteResult(quic::WRITE_STATUS_OK, 1)));
  }

  void SetUp() override {
    envoy_quic_session_.Initialize();
    setQuicConfigWithDefaultValues(envoy_quic_session_.config());
    envoy_quic_session_.OnConfigNegotiated();
    envoy_quic_session_.addConnectionCallbacks(network_connection_callbacks_);
    envoy_quic_session_.setConnectionStats(
        {read_total_, read_current_, write_total_, write_current_, nullptr, nullptr});
    EXPECT_EQ(&read_total_, &quic_connection_->connectionStats().read_total_);
  }

  void TearDown() override {
    if (quic_connection_->connected()) {
      EXPECT_CALL(*quic_connection_,
                  SendConnectionClosePacket(quic::QUIC_NO_ERROR, _, "Closed by application"));
      EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
      envoy_quic_session_.close(Network::ConnectionCloseType::NoFlush);
    }
  }

  EnvoyQuicClientStream& sendGetRequest(Http::ResponseDecoder& response_decoder,
                                        Http::StreamCallbacks& stream_callbacks) {
    auto& stream =
        dynamic_cast<EnvoyQuicClientStream&>(http_connection_.newStream(response_decoder));
    stream.getStream().addCallbacks(stream_callbacks);

    std::string host("www.abc.com");
    Http::TestRequestHeaderMapImpl request_headers{
        {":authority", host}, {":method", "GET"}, {":path", "/"}};
    const auto result = stream.encodeHeaders(request_headers, true);
    ASSERT(result.ok());
    return stream;
  }

protected:
  Event::SimulatedTimeSystemHelper time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  EnvoyQuicConnectionHelper connection_helper_;
  EnvoyQuicAlarmFactory alarm_factory_;
  quic::ParsedQuicVersionVector quic_version_;
  testing::NiceMock<quic::test::MockPacketWriter> writer_;
  Network::Address::InstanceConstSharedPtr peer_addr_;
  Network::Address::InstanceConstSharedPtr self_addr_;
  quic::DeterministicConnectionIdGenerator connection_id_generator_{
      quic::kQuicDefaultConnectionIdLength};
  TestEnvoyQuicClientConnection* quic_connection_;
  quic::QuicConfig quic_config_;
  std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config_;
  TestQuicCryptoClientStreamFactory crypto_stream_factory_;
  Stats::IsolatedStoreImpl store_;
  QuicStatNames quic_stat_names_;
  Network::TransportSocketOptionsConstSharedPtr transport_socket_options_;
  EnvoyQuicClientSession envoy_quic_session_;
  Network::MockConnectionCallbacks network_connection_callbacks_;
  Http::MockServerConnectionCallbacks http_connection_callbacks_;
  testing::StrictMock<Stats::MockCounter> read_total_;
  testing::StrictMock<Stats::MockGauge> read_current_;
  testing::StrictMock<Stats::MockCounter> write_total_;
  testing::StrictMock<Stats::MockGauge> write_current_;
  Http::Http3::CodecStats stats_;
  envoy::config::core::v3::Http3ProtocolOptions http3_options_;
  QuicHttpClientConnectionImpl http_connection_;
};

INSTANTIATE_TEST_SUITE_P(EnvoyQuicClientSessionTests, EnvoyQuicClientSessionTest,
                         testing::ValuesIn(quic::CurrentSupportedHttp3Versions()));

TEST_P(EnvoyQuicClientSessionTest, NewStream) {
  Http::MockResponseDecoder response_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EnvoyQuicClientStream& stream = sendGetRequest(response_decoder, stream_callbacks);

  quic::QuicHeaderList headers;
  headers.OnHeaderBlockStart();
  headers.OnHeader(":status", "200");
  headers.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0, /*compressed_header_bytes=*/0);
  // Response headers should be propagated to decoder.
  EXPECT_CALL(response_decoder, decodeHeaders_(_, /*end_stream=*/true))
      .WillOnce(Invoke([](const Http::ResponseHeaderMapPtr& decoded_headers, bool) {
        EXPECT_EQ("200", decoded_headers->getStatusValue());
      }));
  stream.OnStreamHeaderList(/*fin=*/true, headers.uncompressed_header_bytes(), headers);
}

TEST_P(EnvoyQuicClientSessionTest, PacketLimits) {
  // We always allow for reading packets, even if there's no stream.
  EXPECT_EQ(0, envoy_quic_session_.GetNumActiveStreams());
  EXPECT_EQ(16, envoy_quic_session_.numPacketsExpectedPerEventLoop());

  NiceMock<Http::MockResponseDecoder> response_decoder;
  NiceMock<Http::MockStreamCallbacks> stream_callbacks;
  EnvoyQuicClientStream& stream = sendGetRequest(response_decoder, stream_callbacks);

  quic::QuicHeaderList headers;
  headers.OnHeaderBlockStart();
  headers.OnHeader(":status", "200");
  headers.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0, /*compressed_header_bytes=*/0);
  // Response headers should be propagated to decoder.
  EXPECT_CALL(response_decoder, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([](const Http::ResponseHeaderMapPtr& decoded_headers, bool) {
        EXPECT_EQ("200", decoded_headers->getStatusValue());
      }));
  stream.OnStreamHeaderList(/*fin=*/false, headers.uncompressed_header_bytes(), headers);
  // With one stream, still read 16 packets.
  EXPECT_EQ(1, envoy_quic_session_.GetNumActiveStreams());
  EXPECT_EQ(16, envoy_quic_session_.numPacketsExpectedPerEventLoop());

  EnvoyQuicClientStream& stream2 = sendGetRequest(response_decoder, stream_callbacks);
  EXPECT_CALL(response_decoder, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([](const Http::ResponseHeaderMapPtr& decoded_headers, bool) {
        EXPECT_EQ("200", decoded_headers->getStatusValue());
      }));
  stream2.OnStreamHeaderList(/*fin=*/false, headers.uncompressed_header_bytes(), headers);
  // With 2 streams, read 32 packets.
  EXPECT_EQ(2, envoy_quic_session_.GetNumActiveStreams());
  EXPECT_EQ(32, envoy_quic_session_.numPacketsExpectedPerEventLoop());

  EXPECT_CALL(*quic_connection_,
              SendConnectionClosePacket(quic::QUIC_NO_ERROR, _, "Closed by application"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  envoy_quic_session_.close(Network::ConnectionCloseType::NoFlush);
}

TEST_P(EnvoyQuicClientSessionTest, OnResetFrame) {
  Http::MockResponseDecoder response_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EnvoyQuicClientStream& stream = sendGetRequest(response_decoder, stream_callbacks);

  // G-QUIC or IETF bi-directional stream.
  quic::QuicStreamId stream_id = stream.id();
  quic::QuicRstStreamFrame rst1(/*control_frame_id=*/1u, stream_id,
                                quic::QUIC_ERROR_PROCESSING_STREAM, /*bytes_written=*/0u);
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::RemoteReset, _));
  envoy_quic_session_.OnRstStream(rst1);

  EXPECT_EQ(
      1U, TestUtility::findCounter(
              store_, "http3.upstream.rx.quic_reset_stream_error_code_QUIC_ERROR_PROCESSING_STREAM")
              ->value());
}

TEST_P(EnvoyQuicClientSessionTest, SendResetFrame) {
  Http::MockResponseDecoder response_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EnvoyQuicClientStream& stream = sendGetRequest(response_decoder, stream_callbacks);

  // IETF bi-directional stream.
  quic::QuicStreamId stream_id = stream.id();
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::LocalReset, _));
  EXPECT_CALL(*quic_connection_, SendControlFrame(_));
  envoy_quic_session_.ResetStream(stream_id, quic::QUIC_ERROR_PROCESSING_STREAM);

  EXPECT_EQ(
      1U, TestUtility::findCounter(
              store_, "http3.upstream.tx.quic_reset_stream_error_code_QUIC_ERROR_PROCESSING_STREAM")
              ->value());
}

TEST_P(EnvoyQuicClientSessionTest, OnGoAwayFrame) {
  Http::MockResponseDecoder response_decoder;
  Http::MockStreamCallbacks stream_callbacks;

  EXPECT_CALL(http_connection_callbacks_, onGoAway(Http::GoAwayErrorCode::NoError));
  envoy_quic_session_.OnHttp3GoAway(4u);
}

TEST_P(EnvoyQuicClientSessionTest, ConnectionClose) {
  std::string error_details("dummy details");
  quic::QuicErrorCode error(quic::QUIC_INVALID_FRAME_DATA);
  quic::QuicConnectionCloseFrame frame(quic_version_[0].transport_version, error,
                                       quic::NO_IETF_QUIC_ERROR, error_details,
                                       /* transport_close_frame_type = */ 0);
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose));
  quic_connection_->OnConnectionCloseFrame(frame);
  EXPECT_EQ(absl::StrCat(quic::QuicErrorCodeToString(error), " with details: ", error_details),
            envoy_quic_session_.transportFailureReason());
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());

  EXPECT_EQ(
      1U, TestUtility::findCounter(
              store_, "http3.upstream.rx.quic_connection_close_error_code_QUIC_INVALID_FRAME_DATA")
              ->value());
}

TEST_P(EnvoyQuicClientSessionTest, ConnectionCloseWithActiveStream) {
  Http::MockResponseDecoder response_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EnvoyQuicClientStream& stream = sendGetRequest(response_decoder, stream_callbacks);
  EXPECT_CALL(*quic_connection_,
              SendConnectionClosePacket(quic::QUIC_NO_ERROR, _, "Closed by application"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  envoy_quic_session_.close(Network::ConnectionCloseType::NoFlush);
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  EXPECT_TRUE(stream.write_side_closed() && stream.reading_stopped());
  EXPECT_EQ(1U, TestUtility::findCounter(
                    store_, "http3.upstream.tx.quic_connection_close_error_code_QUIC_NO_ERROR")
                    ->value());
}

TEST_P(EnvoyQuicClientSessionTest, HandshakeTimesOutWithActiveStream) {
  Http::MockResponseDecoder response_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EnvoyQuicClientStream& stream = sendGetRequest(response_decoder, stream_callbacks);
  EXPECT_CALL(*quic_connection_,
              SendConnectionClosePacket(quic::QUIC_HANDSHAKE_FAILED, _, "fake handshake time out"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ConnectionFailure, _));
  envoy_quic_session_.OnStreamError(quic::QUIC_HANDSHAKE_FAILED, "fake handshake time out");
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  EXPECT_TRUE(stream.write_side_closed() && stream.reading_stopped());
  EXPECT_EQ(1U,
            TestUtility::findCounter(
                store_, "http3.upstream.tx.quic_connection_close_error_code_QUIC_HANDSHAKE_FAILED")
                ->value());
}

TEST_P(EnvoyQuicClientSessionTest, ConnectionClosePopulatesQuicVersionStats) {
  std::string error_details("dummy details");
  quic::QuicErrorCode error(quic::QUIC_INVALID_FRAME_DATA);
  quic::QuicConnectionCloseFrame frame(quic_version_[0].transport_version, error,
                                       quic::NO_IETF_QUIC_ERROR, error_details,
                                       /* transport_close_frame_type = */ 0);
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose));
  quic_connection_->OnConnectionCloseFrame(frame);
  EXPECT_EQ(absl::StrCat(quic::QuicErrorCodeToString(error), " with details: ", error_details),
            envoy_quic_session_.transportFailureReason());
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  std::string quic_version_stat_name;
  switch (GetParam().transport_version) {
  case quic::QUIC_VERSION_IETF_DRAFT_29:
    quic_version_stat_name = "h3_29";
    break;
  case quic::QUIC_VERSION_IETF_RFC_V1:
    quic_version_stat_name = "rfc_v1";
    break;
  default:
    break;
  }
  EXPECT_EQ(1U, TestUtility::findCounter(
                    store_, absl::StrCat("http3.quic_version_", quic_version_stat_name))
                    ->value());
}

TEST_P(EnvoyQuicClientSessionTest, IncomingUnidirectionalReadStream) {
  quic::QuicStreamId stream_id = 1u;
  quic::QuicStreamFrame stream_frame(stream_id, false, 0, "aaa");
  envoy_quic_session_.OnStreamFrame(stream_frame);
  EXPECT_FALSE(quic::test::QuicSessionPeer::IsStreamCreated(&envoy_quic_session_, stream_id));
  // IETF stream 3 is server initiated uni-directional stream.
  stream_id = 3u;
  auto payload = std::make_unique<char[]>(8);
  quic::QuicDataWriter payload_writer(8, payload.get());
  EXPECT_TRUE(payload_writer.WriteVarInt62(1ul));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(*quic_connection_, SendConnectionClosePacket(quic::QUIC_HTTP_RECEIVE_SERVER_PUSH, _,
                                                           "Received server push stream"));
  quic::QuicStreamFrame stream_frame2(stream_id, false, 0, absl::string_view(payload.get(), 1));
  envoy_quic_session_.OnStreamFrame(stream_frame2);
}

TEST_P(EnvoyQuicClientSessionTest, GetRttAndCwnd) {
  EXPECT_GT(envoy_quic_session_.lastRoundTripTime().value(), std::chrono::microseconds(0));
  // Just make sure the CWND is non-zero. We don't want to make strong assertions on what the value
  // should be in this test, that is the job the congestion controllers' tests.
  EXPECT_GT(envoy_quic_session_.congestionWindowInBytes().value(), 500);

  envoy_quic_session_.configureInitialCongestionWindow(8000000, std::chrono::microseconds(1000000));
  EXPECT_GT(envoy_quic_session_.congestionWindowInBytes().value(),
            quic::kInitialCongestionWindow * quic::kDefaultTCPMSS);
}

TEST_P(EnvoyQuicClientSessionTest, VerifyContext) {
  auto& verify_context =
      dynamic_cast<EnvoyQuicProofVerifyContext&>(crypto_stream_factory_.lastVerifyContext().ref());
  EXPECT_FALSE(verify_context.isServer());
  EXPECT_EQ(transport_socket_options_.get(), verify_context.transportSocketOptions().get());
  EXPECT_EQ(dispatcher_.get(), &verify_context.dispatcher());
}

} // namespace Quic
} // namespace Envoy
