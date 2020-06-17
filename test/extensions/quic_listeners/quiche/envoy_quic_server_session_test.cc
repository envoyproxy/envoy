#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/crypto/null_encrypter.h"
#include "quiche/quic/core/quic_crypto_server_stream.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/test_tools/crypto_test_utils.h"
#include "quiche/quic/test_tools/quic_connection_peer.h"
#include "quiche/quic/test_tools/quic_server_session_base_peer.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

#pragma GCC diagnostic pop

#include <string>

#include "extensions/quic_listeners/quiche/envoy_quic_server_session.h"
#include "extensions/quic_listeners/quiche/envoy_quic_server_stream.h"
#include "extensions/quic_listeners/quiche/envoy_quic_server_connection.h"
#include "extensions/quic_listeners/quiche/codec_impl.h"
#include "extensions/quic_listeners/quiche/envoy_quic_connection_helper.h"
#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"
#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "test/extensions/quic_listeners/quiche/test_proof_source.h"
#include "test/extensions/quic_listeners/quiche/test_utils.h"
#include "extensions/transport_sockets/well_known_names.h"

#include "envoy/stats/stats_macros.h"
#include "common/event/libevent_scheduler.h"
#include "server/configuration_impl.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/global.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Quic {

class TestEnvoyQuicServerConnection : public EnvoyQuicServerConnection {
public:
  TestEnvoyQuicServerConnection(quic::QuicConnectionHelperInterface& helper,
                                quic::QuicAlarmFactory& alarm_factory,
                                quic::QuicPacketWriter& writer,
                                const quic::ParsedQuicVersionVector& supported_versions,
                                Network::ListenerConfig& listener_config,
                                Server::ListenerStats& stats, Network::Socket& listen_socket)
      : EnvoyQuicServerConnection(quic::test::TestConnectionId(),
                                  quic::QuicSocketAddress(quic::QuicIpAddress::Loopback4(), 12345),
                                  helper, alarm_factory, &writer, /*owns_writer=*/false,
                                  supported_versions, listener_config, stats, listen_socket) {}

  Network::Connection::ConnectionStats& connectionStats() const {
    return EnvoyQuicConnection::connectionStats();
  }

  MOCK_METHOD(void, SendConnectionClosePacket, (quic::QuicErrorCode, const std::string&));
  MOCK_METHOD(bool, SendControlFrame, (const quic::QuicFrame& frame));
};

// Derive to have simpler priority mechanism.
class TestEnvoyQuicServerSession : public EnvoyQuicServerSession {
public:
  using EnvoyQuicServerSession::EnvoyQuicServerSession;

  bool ShouldYield(quic::QuicStreamId /*stream_id*/) override {
    // Never yield to other stream so that it's easier to predict stream write
    // behavior.
    return false;
  }
};

class TestQuicCryptoServerStream : public quic::QuicCryptoServerStream {
public:
  explicit TestQuicCryptoServerStream(const quic::QuicCryptoServerConfig* crypto_config,
                                      quic::QuicCompressedCertsCache* compressed_certs_cache,
                                      quic::QuicSession* session,
                                      quic::QuicCryptoServerStreamBase::Helper* helper)
      : quic::QuicCryptoServerStream(crypto_config, compressed_certs_cache, session, helper) {}

  using quic::QuicCryptoServerStream::QuicCryptoServerStream;

  bool encryption_established() const override { return true; }
};

class EnvoyQuicServerSessionTest : public testing::TestWithParam<bool> {
public:
  EnvoyQuicServerSessionTest()
      : api_(Api::createApiForTest(time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")), connection_helper_(*dispatcher_),
        alarm_factory_(*dispatcher_, *connection_helper_.GetClock()), quic_version_([]() {
          SetQuicReloadableFlag(quic_enable_version_draft_28, GetParam());
          SetQuicReloadableFlag(quic_enable_version_draft_27, GetParam());
          SetQuicReloadableFlag(quic_enable_version_draft_25_v3, GetParam());
          return quic::ParsedVersionOfIndex(quic::CurrentSupportedVersions(), 0);
        }()),
        listener_stats_({ALL_LISTENER_STATS(POOL_COUNTER(listener_config_.listenerScope()),
                                            POOL_GAUGE(listener_config_.listenerScope()),
                                            POOL_HISTOGRAM(listener_config_.listenerScope()))}),
        quic_connection_(new TestEnvoyQuicServerConnection(
            connection_helper_, alarm_factory_, writer_, quic_version_, listener_config_,
            listener_stats_, *listener_config_.socket_)),
        crypto_config_(quic::QuicCryptoServerConfig::TESTING, quic::QuicRandom::GetInstance(),
                       std::make_unique<TestProofSource>(), quic::KeyExchangeSource::Default()),
        envoy_quic_session_(quic_config_, quic_version_,
                            std::unique_ptr<TestEnvoyQuicServerConnection>(quic_connection_),
                            /*visitor=*/nullptr, &crypto_stream_helper_, &crypto_config_,
                            &compressed_certs_cache_, *dispatcher_,
                            /*send_buffer_limit*/ quic::kDefaultFlowControlSendWindow * 1.5),
        read_filter_(new Network::MockReadFilter()) {

    EXPECT_EQ(time_system_.systemTime(), envoy_quic_session_.streamInfo().startTime());
    EXPECT_EQ(EMPTY_STRING, envoy_quic_session_.nextProtocol());

    // Advance time and trigger update of Dispatcher::approximateMonotonicTime()
    // because zero QuicTime is considered uninitialized.
    time_system_.advanceTimeWait(std::chrono::milliseconds(1));
    connection_helper_.GetClock()->Now();

    ON_CALL(writer_, WritePacket(_, _, _, _, _))
        .WillByDefault(Invoke([](const char*, size_t buf_len, const quic::QuicIpAddress&,
                                 const quic::QuicSocketAddress&, quic::PerPacketOptions*) {
          return quic::WriteResult{quic::WRITE_STATUS_OK, static_cast<int>(buf_len)};
        }));
    ON_CALL(crypto_stream_helper_, CanAcceptClientHello(_, _, _, _, _)).WillByDefault(Return(true));
  }

  void SetUp() override {
    envoy_quic_session_.Initialize();
    setQuicConfigWithDefaultValues(envoy_quic_session_.config());
    envoy_quic_session_.OnConfigNegotiated();

    // Switch to a encryption forward secure crypto stream.
    quic::test::QuicServerSessionBasePeer::SetCryptoStream(&envoy_quic_session_, nullptr);
    quic::test::QuicServerSessionBasePeer::SetCryptoStream(
        &envoy_quic_session_,
        new TestQuicCryptoServerStream(&crypto_config_, &compressed_certs_cache_,
                                       &envoy_quic_session_, &crypto_stream_helper_));
    quic_connection_->SetDefaultEncryptionLevel(quic::ENCRYPTION_FORWARD_SECURE);
    quic_connection_->SetEncrypter(
        quic::ENCRYPTION_FORWARD_SECURE,
        std::make_unique<quic::NullEncrypter>(quic::Perspective::IS_SERVER));
  }

  bool installReadFilter() {
    // Setup read filter.
    envoy_quic_session_.addReadFilter(read_filter_);
    EXPECT_EQ(Http::Protocol::Http3,
              read_filter_->callbacks_->connection().streamInfo().protocol().value());
    EXPECT_EQ(envoy_quic_session_.id(), read_filter_->callbacks_->connection().id());
    EXPECT_EQ(&envoy_quic_session_, &read_filter_->callbacks_->connection());
    read_filter_->callbacks_->connection().addConnectionCallbacks(network_connection_callbacks_);
    read_filter_->callbacks_->connection().setConnectionStats(
        {read_total_, read_current_, write_total_, write_current_, nullptr, nullptr});
    EXPECT_EQ(&read_total_, &quic_connection_->connectionStats().read_total_);
    EXPECT_CALL(*read_filter_, onNewConnection()).WillOnce(Invoke([this]() {
      // Create ServerConnection instance and setup callbacks for it.
      http_connection_ = std::make_unique<QuicHttpServerConnectionImpl>(envoy_quic_session_,
                                                                        http_connection_callbacks_);
      EXPECT_EQ(Http::Protocol::Http3, http_connection_->protocol());
      // Stop iteration to avoid calling getRead/WriteBuffer().
      return Network::FilterStatus::StopIteration;
    }));
    return envoy_quic_session_.initializeReadFilters();
  }

  quic::QuicStream* createNewStream(Http::MockRequestDecoder& request_decoder,
                                    Http::MockStreamCallbacks& stream_callbacks) {
    EXPECT_CALL(http_connection_callbacks_, newStream(_, false))
        .WillOnce(Invoke([&request_decoder, &stream_callbacks](Http::ResponseEncoder& encoder,
                                                               bool) -> Http::RequestDecoder& {
          encoder.getStream().addCallbacks(stream_callbacks);
          return request_decoder;
        }));
    quic::QuicStreamId stream_id =
        quic::VersionUsesHttp3(quic_version_[0].transport_version) ? 4u : 5u;
    return envoy_quic_session_.GetOrCreateStream(stream_id);
  }

  void TearDown() override {
    if (quic_connection_->connected()) {
      EXPECT_CALL(*quic_connection_,
                  SendConnectionClosePacket(quic::QUIC_NO_ERROR, "Closed by application"));
      EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
      EXPECT_CALL(*quic_connection_, SendControlFrame(_))
          .Times(testing::AtMost(1))
          .WillOnce(Invoke([](const quic::QuicFrame&) { return false; }));
      envoy_quic_session_.close(Network::ConnectionCloseType::NoFlush);
    }
  }

protected:
  Event::SimulatedTimeSystemHelper time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  EnvoyQuicConnectionHelper connection_helper_;
  EnvoyQuicAlarmFactory alarm_factory_;
  quic::ParsedQuicVersionVector quic_version_;
  testing::NiceMock<quic::test::MockPacketWriter> writer_;
  testing::NiceMock<Network::MockListenerConfig> listener_config_;
  Server::ListenerStats listener_stats_;
  TestEnvoyQuicServerConnection* quic_connection_;
  quic::QuicConfig quic_config_;
  quic::QuicCryptoServerConfig crypto_config_;
  testing::NiceMock<quic::test::MockQuicCryptoServerStreamHelper> crypto_stream_helper_;
  TestEnvoyQuicServerSession envoy_quic_session_;
  quic::QuicCompressedCertsCache compressed_certs_cache_{100};
  std::shared_ptr<Network::MockReadFilter> read_filter_;
  Network::MockConnectionCallbacks network_connection_callbacks_;
  Http::MockServerConnectionCallbacks http_connection_callbacks_;
  testing::StrictMock<Stats::MockCounter> read_total_;
  testing::StrictMock<Stats::MockGauge> read_current_;
  testing::StrictMock<Stats::MockCounter> write_total_;
  testing::StrictMock<Stats::MockGauge> write_current_;
  Http::ServerConnectionPtr http_connection_;
};

INSTANTIATE_TEST_SUITE_P(EnvoyQuicServerSessionTests, EnvoyQuicServerSessionTest,
                         testing::ValuesIn({true, false}));

TEST_P(EnvoyQuicServerSessionTest, NewStream) {
  installReadFilter();

  Http::MockRequestDecoder request_decoder;
  EXPECT_CALL(http_connection_callbacks_, newStream(_, false))
      .WillOnce(testing::ReturnRef(request_decoder));
  quic::QuicStreamId stream_id =
      quic::VersionUsesHttp3(quic_version_[0].transport_version) ? 4u : 5u;
  auto stream =
      reinterpret_cast<quic::QuicSpdyStream*>(envoy_quic_session_.GetOrCreateStream(stream_id));
  // Receive a GET request on created stream.
  quic::QuicHeaderList headers;
  headers.OnHeaderBlockStart();
  std::string host("www.abc.com");
  headers.OnHeader(":authority", host);
  headers.OnHeader(":method", "GET");
  headers.OnHeader(":path", "/");
  headers.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0, /*compressed_header_bytes=*/0);
  // Request headers should be propagated to decoder.
  EXPECT_CALL(request_decoder, decodeHeaders_(_, /*end_stream=*/true))
      .WillOnce(Invoke([&host](const Http::RequestHeaderMapPtr& decoded_headers, bool) {
        EXPECT_EQ(host, decoded_headers->getHostValue());
        EXPECT_EQ("/", decoded_headers->getPathValue());
        EXPECT_EQ(Http::Headers::get().MethodValues.Get, decoded_headers->getMethodValue());
      }));
  stream->OnStreamHeaderList(/*fin=*/true, headers.uncompressed_header_bytes(), headers);
}

TEST_P(EnvoyQuicServerSessionTest, InvalidIncomingStreamId) {
  quic::SetVerbosityLogThreshold(1);
  installReadFilter();
  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  // IETF stream 5 and G-Quic stream 2 are server initiated.
  quic::QuicStreamId stream_id =
      quic::VersionUsesHttp3(quic_version_[0].transport_version) ? 5u : 2u;
  std::string data("aaaa");
  quic::QuicStreamFrame stream_frame(stream_id, false, 0, data);
  EXPECT_CALL(http_connection_callbacks_, newStream(_, false)).Times(0);
  EXPECT_CALL(*quic_connection_,
              SendConnectionClosePacket((quic::VersionUsesHttp3(quic_version_[0].transport_version)
                                             ? quic::QUIC_HTTP_STREAM_WRONG_DIRECTION
                                             : quic::QUIC_INVALID_STREAM_ID),
                                        "Data for nonexistent stream"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));

  envoy_quic_session_.OnStreamFrame(stream_frame);
}

TEST_P(EnvoyQuicServerSessionTest, NoNewStreamForInvalidIncomingStream) {
  installReadFilter();
  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  // IETF stream 5 and G-Quic stream 2 are server initiated.
  quic::QuicStreamId stream_id =
      quic::VersionUsesHttp3(quic_version_[0].transport_version) ? 5u : 2u;
  EXPECT_CALL(http_connection_callbacks_, newStream(_, false)).Times(0);
  EXPECT_CALL(*quic_connection_,
              SendConnectionClosePacket(quic::VersionUsesHttp3(quic_version_[0].transport_version)
                                            ? quic::QUIC_HTTP_STREAM_WRONG_DIRECTION
                                            : quic::QUIC_INVALID_STREAM_ID,
                                        "Data for nonexistent stream"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));

  // Stream creation on closed connection should fail.
  EXPECT_EQ(nullptr, envoy_quic_session_.GetOrCreateStream(stream_id));
}

TEST_P(EnvoyQuicServerSessionTest, OnResetFrame) {
  installReadFilter();
  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  quic::QuicStream* stream1 = createNewStream(request_decoder, stream_callbacks);
  quic::QuicRstStreamFrame rst1(/*control_frame_id=*/1u, stream1->id(),
                                quic::QUIC_ERROR_PROCESSING_STREAM, /*bytes_written=*/0u);
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::RemoteReset, _));
  if (!quic::VersionUsesHttp3(quic_version_[0].transport_version)) {
    EXPECT_CALL(*quic_connection_, SendControlFrame(_))
        .WillOnce(Invoke([stream_id = stream1->id()](const quic::QuicFrame& frame) {
          EXPECT_EQ(stream_id, frame.rst_stream_frame->stream_id);
          EXPECT_EQ(quic::QUIC_RST_ACKNOWLEDGEMENT, frame.rst_stream_frame->error_code);
          return false;
        }));
  } else {
  }
  stream1->OnStreamReset(rst1);

  EXPECT_CALL(http_connection_callbacks_, newStream(_, false))
      .WillOnce(Invoke([&request_decoder, &stream_callbacks](Http::ResponseEncoder& encoder,
                                                             bool) -> Http::RequestDecoder& {
        encoder.getStream().addCallbacks(stream_callbacks);
        return request_decoder;
      }));
  quic::QuicStream* stream2 = envoy_quic_session_.GetOrCreateStream(stream1->id() + 4u);
  quic::QuicRstStreamFrame rst2(/*control_frame_id=*/1u, stream2->id(), quic::QUIC_REFUSED_STREAM,
                                /*bytes_written=*/0u);
  EXPECT_CALL(stream_callbacks,
              onResetStream(Http::StreamResetReason::RemoteRefusedStreamReset, _));
  stream2->OnStreamReset(rst2);
}

TEST_P(EnvoyQuicServerSessionTest, ConnectionClose) {
  installReadFilter();

  std::string error_details("dummy details");
  quic::QuicErrorCode error(quic::QUIC_INVALID_FRAME_DATA);
  quic::QuicConnectionCloseFrame frame(quic_version_[0].transport_version, error, error_details,
                                       /* transport_close_frame_type = */ 0);
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose));
  quic_connection_->OnConnectionCloseFrame(frame);
  EXPECT_EQ(absl::StrCat(quic::QuicErrorCodeToString(error), " with details: ", error_details),
            envoy_quic_session_.transportFailureReason());
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
}

TEST_P(EnvoyQuicServerSessionTest, ConnectionCloseWithActiveStream) {
  installReadFilter();

  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  quic::QuicStream* stream = createNewStream(request_decoder, stream_callbacks);
  EXPECT_CALL(*quic_connection_,
              SendConnectionClosePacket(quic::QUIC_NO_ERROR, "Closed by application"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  envoy_quic_session_.close(Network::ConnectionCloseType::NoFlush);
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  EXPECT_TRUE(stream->write_side_closed() && stream->reading_stopped());
}

TEST_P(EnvoyQuicServerSessionTest, NoFlushWithDataToWrite) {
  installReadFilter();

  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  quic::QuicStream* stream = createNewStream(request_decoder, stream_callbacks);
  envoy_quic_session_.MarkConnectionLevelWriteBlocked(stream->id());
  EXPECT_CALL(*quic_connection_,
              SendConnectionClosePacket(quic::QUIC_NO_ERROR, "Closed by application"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  // Even though the stream is write blocked, connection should be closed
  // immediately.
  envoy_quic_session_.close(Network::ConnectionCloseType::NoFlush);
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  EXPECT_TRUE(stream->write_side_closed() && stream->reading_stopped());
}

TEST_P(EnvoyQuicServerSessionTest, FlushCloseWithDataToWrite) {
  installReadFilter();
  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  quic::QuicStream* stream = createNewStream(request_decoder, stream_callbacks);

  envoy_quic_session_.MarkConnectionLevelWriteBlocked(stream->id());
  EXPECT_TRUE(envoy_quic_session_.HasDataToWrite());
  // Connection shouldn't be closed right away as there is a stream write blocked.
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWrite);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());
  EXPECT_CALL(*quic_connection_,
              SendConnectionClosePacket(quic::QUIC_NO_ERROR, "Closed by application"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  // Unblock that stream to trigger actual connection close.
  envoy_quic_session_.OnCanWrite();
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  EXPECT_FALSE(quic_connection_->connected());
}

// Tests that a write event after flush close should update the delay close
// timer.
TEST_P(EnvoyQuicServerSessionTest, WriteUpdatesDelayCloseTimer) {
  installReadFilter();
  // Drive congestion control manually.
  auto send_algorithm = new testing::NiceMock<quic::test::MockSendAlgorithm>;
  quic::test::QuicConnectionPeer::SetSendAlgorithm(quic_connection_, send_algorithm);
  EXPECT_CALL(*send_algorithm, CanSend(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*send_algorithm, GetCongestionWindow()).WillRepeatedly(Return(quic::kDefaultTCPMSS));
  EXPECT_CALL(*send_algorithm, PacingRate(_)).WillRepeatedly(Return(quic::QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm, BandwidthEstimate())
      .WillRepeatedly(Return(quic::QuicBandwidth::Zero()));

  EXPECT_CALL(*quic_connection_, SendControlFrame(_)).Times(AnyNumber());

  // Bump connection flow control window large enough not to interfere
  // stream writing.
  envoy_quic_session_.flow_controller()->UpdateSendWindowOffset(
      10 * quic::kDefaultFlowControlSendWindow);

  envoy_quic_session_.setDelayedCloseTimeout(std::chrono::milliseconds(100));
  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  // Create a stream and write enough data to make it blocked.
  auto stream =
      dynamic_cast<EnvoyQuicServerStream*>(createNewStream(request_decoder, stream_callbacks));

  // Receive a GET request on created stream.
  quic::QuicHeaderList request_headers;
  request_headers.OnHeaderBlockStart();
  std::string host("www.abc.com");
  request_headers.OnHeader(":authority", host);
  request_headers.OnHeader(":method", "GET");
  request_headers.OnHeader(":path", "/");
  request_headers.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0, /*compressed_header_bytes=*/0);
  // Request headers should be propagated to decoder.
  EXPECT_CALL(request_decoder, decodeHeaders_(_, /*end_stream=*/true))
      .WillOnce(Invoke([&host](const Http::RequestHeaderMapPtr& decoded_headers, bool) {
        EXPECT_EQ(host, decoded_headers->getHostValue());
        EXPECT_EQ("/", decoded_headers->getPathValue());
        EXPECT_EQ(Http::Headers::get().MethodValues.Get, decoded_headers->getMethodValue());
      }));
  stream->OnStreamHeaderList(/*fin=*/true, request_headers.uncompressed_header_bytes(),
                             request_headers);

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {":content-length", "32770"}}; // 32KB + 2 bytes

  stream->encodeHeaders(response_headers, false);
  std::string response(32 * 1024 + 1, 'a');
  Buffer::OwnedImpl buffer(response);
  EXPECT_CALL(stream_callbacks, onAboveWriteBufferHighWatermark());
  stream->encodeData(buffer, false);
  // Stream become write blocked.
  EXPECT_TRUE(envoy_quic_session_.HasDataToWrite());
  EXPECT_TRUE(stream->flow_controller()->IsBlocked());
  EXPECT_FALSE(envoy_quic_session_.IsConnectionFlowControlBlocked());

  // Connection shouldn't be closed right away as there is a stream write blocked.
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWrite);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  time_system_.advanceTimeAsync(std::chrono::milliseconds(10));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  // Another write event without updating flow control window shouldn't trigger
  // connection close, but it should update the timer.
  envoy_quic_session_.OnCanWrite();
  EXPECT_TRUE(envoy_quic_session_.HasDataToWrite());

  // Timer shouldn't fire at original deadline.
  time_system_.advanceTimeAsync(std::chrono::milliseconds(90));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  EXPECT_CALL(*quic_connection_,
              SendConnectionClosePacket(quic::QUIC_NO_ERROR, "Closed by application"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  // Advance the time to fire connection close timer.
  time_system_.advanceTimeAsync(std::chrono::milliseconds(10));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  EXPECT_FALSE(quic_connection_->connected());
}

// Tests that if delay close timeout is not configured, flush close will not act
// based on timeout.
TEST_P(EnvoyQuicServerSessionTest, FlushCloseNoTimeout) {
  installReadFilter();
  // Switch to a encryption forward secure crypto stream.
  quic::test::QuicServerSessionBasePeer::SetCryptoStream(&envoy_quic_session_, nullptr);
  quic::test::QuicServerSessionBasePeer::SetCryptoStream(
      &envoy_quic_session_,
      new TestQuicCryptoServerStream(&crypto_config_, &compressed_certs_cache_,
                                     &envoy_quic_session_, &crypto_stream_helper_));
  quic_connection_->SetDefaultEncryptionLevel(quic::ENCRYPTION_FORWARD_SECURE);
  quic_connection_->SetEncrypter(
      quic::ENCRYPTION_FORWARD_SECURE,
      std::make_unique<quic::NullEncrypter>(quic::Perspective::IS_SERVER));
  // Drive congestion control manually.
  auto send_algorithm = new testing::NiceMock<quic::test::MockSendAlgorithm>;
  quic::test::QuicConnectionPeer::SetSendAlgorithm(quic_connection_, send_algorithm);
  EXPECT_CALL(*send_algorithm, CanSend(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*send_algorithm, GetCongestionWindow()).WillRepeatedly(Return(quic::kDefaultTCPMSS));
  EXPECT_CALL(*send_algorithm, PacingRate(_)).WillRepeatedly(Return(quic::QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm, BandwidthEstimate())
      .WillRepeatedly(Return(quic::QuicBandwidth::Zero()));

  EXPECT_CALL(*quic_connection_, SendControlFrame(_)).Times(AnyNumber());

  // Bump connection flow control window large enough not to interfere
  // stream writing.
  envoy_quic_session_.flow_controller()->UpdateSendWindowOffset(
      10 * quic::kDefaultFlowControlSendWindow);

  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  // Create a stream and write enough data to make it blocked.
  auto stream =
      dynamic_cast<EnvoyQuicServerStream*>(createNewStream(request_decoder, stream_callbacks));

  // Receive a GET request on created stream.
  quic::QuicHeaderList request_headers;
  request_headers.OnHeaderBlockStart();
  std::string host("www.abc.com");
  request_headers.OnHeader(":authority", host);
  request_headers.OnHeader(":method", "GET");
  request_headers.OnHeader(":path", "/");
  request_headers.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0, /*compressed_header_bytes=*/0);
  // Request headers should be propagated to decoder.
  EXPECT_CALL(request_decoder, decodeHeaders_(_, /*end_stream=*/true))
      .WillOnce(Invoke([&host](const Http::RequestHeaderMapPtr& decoded_headers, bool) {
        EXPECT_EQ(host, decoded_headers->getHostValue());
        EXPECT_EQ("/", decoded_headers->getPathValue());
        EXPECT_EQ(Http::Headers::get().MethodValues.Get, decoded_headers->getMethodValue());
      }));
  stream->OnStreamHeaderList(/*fin=*/true, request_headers.uncompressed_header_bytes(),
                             request_headers);

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {":content-length", "32770"}}; // 32KB + 2 bytes

  stream->encodeHeaders(response_headers, false);
  std::string response(32 * 1024 + 1, 'a');
  Buffer::OwnedImpl buffer(response);
  stream->encodeData(buffer, true);
  // Stream become write blocked.
  EXPECT_TRUE(envoy_quic_session_.HasDataToWrite());
  EXPECT_TRUE(stream->flow_controller()->IsBlocked());
  EXPECT_FALSE(envoy_quic_session_.IsConnectionFlowControlBlocked());

  // Connection shouldn't be closed right away as there is a stream write blocked.
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWrite);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());
  // Another write event without updating flow control window shouldn't trigger
  // connection close.
  envoy_quic_session_.OnCanWrite();
  EXPECT_TRUE(envoy_quic_session_.HasDataToWrite());

  // No timeout set, so alarm shouldn't fire.
  time_system_.advanceTimeAsync(std::chrono::milliseconds(100));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  // Force close connection.
  EXPECT_CALL(*quic_connection_,
              SendConnectionClosePacket(quic::QUIC_NO_ERROR, "Closed by application"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  EXPECT_CALL(*quic_connection_, SendControlFrame(_))
      .Times(testing::AtMost(1))
      .WillOnce(Invoke([](const quic::QuicFrame&) { return false; }));
  envoy_quic_session_.close(Network::ConnectionCloseType::NoFlush);
}

TEST_P(EnvoyQuicServerSessionTest, FlushCloseWithTimeout) {
  installReadFilter();
  envoy_quic_session_.setDelayedCloseTimeout(std::chrono::milliseconds(100));
  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  quic::QuicStream* stream = createNewStream(request_decoder, stream_callbacks);

  envoy_quic_session_.MarkConnectionLevelWriteBlocked(stream->id());
  EXPECT_TRUE(envoy_quic_session_.HasDataToWrite());
  // Connection shouldn't be closed right away as there is a stream write blocked.
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWrite);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  // Advance the time a bit and try to close again. The delay close timer
  // shouldn't be rescheduled by this call.
  time_system_.advanceTimeAsync(std::chrono::milliseconds(10));
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWriteAndDelay);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  EXPECT_CALL(*quic_connection_,
              SendConnectionClosePacket(quic::QUIC_NO_ERROR, "Closed by application"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  // Advance the time to fire connection close timer.
  time_system_.advanceTimeAsync(std::chrono::milliseconds(90));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  EXPECT_FALSE(quic_connection_->connected());
}

TEST_P(EnvoyQuicServerSessionTest, FlushAndWaitForCloseWithTimeout) {
  installReadFilter();
  envoy_quic_session_.setDelayedCloseTimeout(std::chrono::milliseconds(100));
  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  quic::QuicStream* stream = createNewStream(request_decoder, stream_callbacks);

  envoy_quic_session_.MarkConnectionLevelWriteBlocked(stream->id());
  EXPECT_TRUE(envoy_quic_session_.HasDataToWrite());
  // Connection shouldn't be closed right away as there is a stream write blocked.
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWriteAndDelay);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());
  // Unblocking the stream shouldn't close the connection as it should be
  // delayed.
  time_system_.advanceTimeWait(std::chrono::milliseconds(10));
  envoy_quic_session_.OnCanWrite();
  // delay close alarm should have been rescheduled.
  time_system_.advanceTimeAsync(std::chrono::milliseconds(90));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  EXPECT_CALL(*quic_connection_,
              SendConnectionClosePacket(quic::QUIC_NO_ERROR, "Closed by application"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  // Advance the time to fire connection close timer.
  time_system_.advanceTimeAsync(std::chrono::milliseconds(10));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  EXPECT_FALSE(quic_connection_->connected());
}

TEST_P(EnvoyQuicServerSessionTest, FlusWriteTransitToFlushWriteWithDelay) {
  installReadFilter();
  envoy_quic_session_.setDelayedCloseTimeout(std::chrono::milliseconds(100));
  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  quic::QuicStream* stream = createNewStream(request_decoder, stream_callbacks);

  envoy_quic_session_.MarkConnectionLevelWriteBlocked(stream->id());
  EXPECT_TRUE(envoy_quic_session_.HasDataToWrite());
  // Connection shouldn't be closed right away as there is a stream write blocked.
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWrite);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  time_system_.advanceTimeWait(std::chrono::milliseconds(10));
  // The closing behavior should be changed.
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWriteAndDelay);
  // Unblocking the stream shouldn't close the connection as it should be
  // delayed.
  envoy_quic_session_.OnCanWrite();

  // delay close alarm should have been rescheduled.
  time_system_.advanceTimeAsync(std::chrono::milliseconds(90));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  EXPECT_CALL(*quic_connection_,
              SendConnectionClosePacket(quic::QUIC_NO_ERROR, "Closed by application"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  // Advance the time to fire connection close timer.
  time_system_.advanceTimeAsync(std::chrono::milliseconds(10));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  EXPECT_FALSE(quic_connection_->connected());
}

TEST_P(EnvoyQuicServerSessionTest, FlushAndWaitForCloseWithNoPendingData) {
  installReadFilter();
  envoy_quic_session_.setDelayedCloseTimeout(std::chrono::milliseconds(100));
  // This close should be delayed as configured.
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWriteAndDelay);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  // Advance the time a bit and try to close again. The delay close timer
  // shouldn't be rescheduled by this call.
  time_system_.advanceTimeWait(std::chrono::milliseconds(10));
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWriteAndDelay);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  EXPECT_CALL(*quic_connection_,
              SendConnectionClosePacket(quic::QUIC_NO_ERROR, "Closed by application"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  // Advance the time to fire connection close timer.
  time_system_.advanceTimeAsync(std::chrono::milliseconds(90));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
}

TEST_P(EnvoyQuicServerSessionTest, ShutdownNotice) {
  installReadFilter();
  // Not verifying dummy implementation, just to have coverage.
  EXPECT_DEATH(envoy_quic_session_.enableHalfClose(true), "");
  EXPECT_EQ(nullptr, envoy_quic_session_.ssl());
  http_connection_->shutdownNotice();
}

TEST_P(EnvoyQuicServerSessionTest, GoAway) {
  installReadFilter();
  testing::NiceMock<quic::test::MockHttp3DebugVisitor> debug_visitor;
  envoy_quic_session_.set_debug_visitor(&debug_visitor);
  if (quic::VersionUsesHttp3(quic_version_[0].transport_version)) {
    EXPECT_CALL(debug_visitor, OnGoAwayFrameSent(_));
  } else {
    EXPECT_CALL(*quic_connection_, SendControlFrame(_));
  }
  http_connection_->goAway();
}

TEST_P(EnvoyQuicServerSessionTest, InitializeFilterChain) {
  std::string packet_content("random payload");
  auto encrypted_packet =
      std::unique_ptr<quic::QuicEncryptedPacket>(quic::test::ConstructEncryptedPacket(
          quic_connection_->connection_id(), quic::EmptyQuicConnectionId(), /*version_flag=*/true,
          /*reset_flag*/ false, /*packet_number=*/1, packet_content));

  quic::QuicSocketAddress self_address(
      envoyAddressInstanceToQuicSocketAddress(listener_config_.socket_->localAddress()));
  auto packet = std::unique_ptr<quic::QuicReceivedPacket>(
      quic::test::ConstructReceivedPacket(*encrypted_packet, connection_helper_.GetClock()->Now()));

  // Receiving above packet should trigger filter chain retrieval.
  Network::MockFilterChainManager filter_chain_manager;
  EXPECT_CALL(listener_config_, filterChainManager()).WillOnce(ReturnRef(filter_chain_manager));
  Network::MockFilterChain filter_chain;
  EXPECT_CALL(filter_chain_manager, findFilterChain(_))
      .WillOnce(Invoke([&](const Network::ConnectionSocket& socket) {
        EXPECT_EQ(*quicAddressToEnvoyAddressInstance(quic_connection_->peer_address()),
                  *socket.remoteAddress());
        EXPECT_EQ(*quicAddressToEnvoyAddressInstance(self_address), *socket.localAddress());
        EXPECT_EQ(listener_config_.socket_->ioHandle().fd(), socket.ioHandle().fd());
        EXPECT_EQ(Extensions::TransportSockets::TransportProtocolNames::get().Quic,
                  socket.detectedTransportProtocol());
        return &filter_chain;
      }));
  std::vector<Network::FilterFactoryCb> filter_factory{[this](
                                                           Network::FilterManager& filter_manager) {
    filter_manager.addReadFilter(read_filter_);
    read_filter_->callbacks_->connection().addConnectionCallbacks(network_connection_callbacks_);
    read_filter_->callbacks_->connection().setConnectionStats(
        {read_total_, read_current_, write_total_, write_current_, nullptr, nullptr});
  }};
  EXPECT_CALL(filter_chain, networkFilterFactories()).WillOnce(ReturnRef(filter_factory));
  EXPECT_CALL(*read_filter_, onNewConnection())
      // Stop iteration to avoid calling getRead/WriteBuffer().
      .WillOnce(Return(Network::FilterStatus::StopIteration));
  EXPECT_CALL(listener_config_.filter_chain_factory_, createNetworkFilterChain(_, _))
      .WillOnce(Invoke([](Network::Connection& connection,
                          const std::vector<Network::FilterFactoryCb>& filter_factories) {
        EXPECT_EQ(1u, filter_factories.size());
        Server::Configuration::FilterChainUtility::buildFilterChain(connection, filter_factories);
        return true;
      }));
  // Connection should be closed because this packet has invalid payload.
  EXPECT_CALL(*quic_connection_, SendConnectionClosePacket(_, _));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  quic_connection_->ProcessUdpPacket(self_address, quic_connection_->peer_address(), *packet);
  EXPECT_FALSE(quic_connection_->connected());
  EXPECT_EQ(nullptr, envoy_quic_session_.socketOptions());
  EXPECT_TRUE(quic_connection_->connectionSocket()->ioHandle().isOpen());
  EXPECT_TRUE(quic_connection_->connectionSocket()->ioHandle().close().ok());
  EXPECT_FALSE(quic_connection_->connectionSocket()->ioHandle().isOpen());
}

TEST_P(EnvoyQuicServerSessionTest, NetworkConnectionInterface) {
  installReadFilter();
  EXPECT_EQ(dispatcher_.get(), &envoy_quic_session_.dispatcher());
  EXPECT_TRUE(envoy_quic_session_.readEnabled());
}

TEST_P(EnvoyQuicServerSessionTest, SendBufferWatermark) {
  // Switch to a encryption forward secure crypto stream.
  quic::test::QuicServerSessionBasePeer::SetCryptoStream(&envoy_quic_session_, nullptr);
  quic::test::QuicServerSessionBasePeer::SetCryptoStream(
      &envoy_quic_session_,
      new TestQuicCryptoServerStream(&crypto_config_, &compressed_certs_cache_,
                                     &envoy_quic_session_, &crypto_stream_helper_));
  quic_connection_->SetDefaultEncryptionLevel(quic::ENCRYPTION_FORWARD_SECURE);
  quic_connection_->SetEncrypter(
      quic::ENCRYPTION_FORWARD_SECURE,
      std::make_unique<quic::NullEncrypter>(quic::Perspective::IS_SERVER));
  // Drive congestion control manually.
  auto send_algorithm = new testing::NiceMock<quic::test::MockSendAlgorithm>;
  quic::test::QuicConnectionPeer::SetSendAlgorithm(quic_connection_, send_algorithm);
  EXPECT_CALL(*send_algorithm, CanSend(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*send_algorithm, GetCongestionWindow()).WillRepeatedly(Return(quic::kDefaultTCPMSS));
  EXPECT_CALL(*send_algorithm, PacingRate(_)).WillRepeatedly(Return(quic::QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm, BandwidthEstimate())
      .WillRepeatedly(Return(quic::QuicBandwidth::Zero()));
  EXPECT_CALL(*quic_connection_, SendControlFrame(_)).Times(AnyNumber());

  // Bump connection flow control window large enough not to interfere
  // stream writing.
  envoy_quic_session_.flow_controller()->UpdateSendWindowOffset(
      10 * quic::kDefaultFlowControlSendWindow);
  installReadFilter();
  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EXPECT_CALL(http_connection_callbacks_, newStream(_, false))
      .WillOnce(Invoke([&request_decoder, &stream_callbacks](Http::ResponseEncoder& encoder,
                                                             bool) -> Http::RequestDecoder& {
        encoder.getStream().addCallbacks(stream_callbacks);
        return request_decoder;
      }));
  quic::QuicStreamId stream_id =
      quic::VersionUsesHttp3(quic_version_[0].transport_version) ? 4u : 5u;
  auto stream1 =
      dynamic_cast<EnvoyQuicServerStream*>(envoy_quic_session_.GetOrCreateStream(stream_id));

  // Receive a GET request on created stream.
  quic::QuicHeaderList request_headers;
  request_headers.OnHeaderBlockStart();
  std::string host("www.abc.com");
  request_headers.OnHeader(":authority", host);
  request_headers.OnHeader(":method", "GET");
  request_headers.OnHeader(":path", "/");
  request_headers.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0, /*compressed_header_bytes=*/0);
  // Request headers should be propagated to decoder.
  EXPECT_CALL(request_decoder, decodeHeaders_(_, /*end_stream=*/true))
      .WillOnce(Invoke([&host](const Http::RequestHeaderMapPtr& decoded_headers, bool) {
        EXPECT_EQ(host, decoded_headers->getHostValue());
        EXPECT_EQ("/", decoded_headers->getPathValue());
        EXPECT_EQ(Http::Headers::get().MethodValues.Get, decoded_headers->getMethodValue());
      }));
  stream1->OnStreamHeaderList(/*fin=*/true, request_headers.uncompressed_header_bytes(),
                              request_headers);

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {":content-length", "32770"}}; // 32KB + 2 bytes

  stream1->encodeHeaders(response_headers, false);
  std::string response(32 * 1024 + 1, 'a');
  Buffer::OwnedImpl buffer(response);
  EXPECT_CALL(stream_callbacks, onAboveWriteBufferHighWatermark());
  stream1->encodeData(buffer, false);
  EXPECT_TRUE(stream1->flow_controller()->IsBlocked());
  EXPECT_FALSE(envoy_quic_session_.IsConnectionFlowControlBlocked());

  // Receive another request and send back response to trigger connection level
  // send buffer watermark.
  Http::MockRequestDecoder request_decoder2;
  Http::MockStreamCallbacks stream_callbacks2;
  EXPECT_CALL(http_connection_callbacks_, newStream(_, false))
      .WillOnce(Invoke([&request_decoder2, &stream_callbacks2](Http::ResponseEncoder& encoder,
                                                               bool) -> Http::RequestDecoder& {
        encoder.getStream().addCallbacks(stream_callbacks2);
        return request_decoder2;
      }));
  auto stream2 =
      dynamic_cast<EnvoyQuicServerStream*>(envoy_quic_session_.GetOrCreateStream(stream_id + 4));
  EXPECT_CALL(request_decoder2, decodeHeaders_(_, /*end_stream=*/true))
      .WillOnce(Invoke([&host](const Http::RequestHeaderMapPtr& decoded_headers, bool) {
        EXPECT_EQ(host, decoded_headers->getHostValue());
        EXPECT_EQ("/", decoded_headers->getPathValue());
        EXPECT_EQ(Http::Headers::get().MethodValues.Get, decoded_headers->getMethodValue());
      }));
  stream2->OnStreamHeaderList(/*fin=*/true, request_headers.uncompressed_header_bytes(),
                              request_headers);
  stream2->encodeHeaders(response_headers, false);
  // This response will trigger both stream and connection's send buffer watermark upper limits.
  Buffer::OwnedImpl buffer2(response);
  EXPECT_CALL(network_connection_callbacks_, onAboveWriteBufferHighWatermark)
      .WillOnce(Invoke(
          [this]() { http_connection_->onUnderlyingConnectionAboveWriteBufferHighWatermark(); }));
  EXPECT_CALL(stream_callbacks2, onAboveWriteBufferHighWatermark()).Times(2);
  EXPECT_CALL(stream_callbacks, onAboveWriteBufferHighWatermark());
  stream2->encodeData(buffer2, false);

  // Receive another request, the new stream should be notified about connection
  // high watermark reached upon creation.
  Http::MockRequestDecoder request_decoder3;
  Http::MockStreamCallbacks stream_callbacks3;
  EXPECT_CALL(http_connection_callbacks_, newStream(_, false))
      .WillOnce(Invoke([&request_decoder3, &stream_callbacks3](Http::ResponseEncoder& encoder,
                                                               bool) -> Http::RequestDecoder& {
        encoder.getStream().addCallbacks(stream_callbacks3);
        return request_decoder3;
      }));
  EXPECT_CALL(stream_callbacks3, onAboveWriteBufferHighWatermark());
  auto stream3 =
      dynamic_cast<EnvoyQuicServerStream*>(envoy_quic_session_.GetOrCreateStream(stream_id + 8));
  EXPECT_CALL(request_decoder3, decodeHeaders_(_, /*end_stream=*/true))
      .WillOnce(Invoke([&host](const Http::RequestHeaderMapPtr& decoded_headers, bool) {
        EXPECT_EQ(host, decoded_headers->getHostValue());
        EXPECT_EQ("/", decoded_headers->getPathValue());
        EXPECT_EQ(Http::Headers::get().MethodValues.Get, decoded_headers->getMethodValue());
      }));
  stream3->OnStreamHeaderList(/*fin=*/true, request_headers.uncompressed_header_bytes(),
                              request_headers);

  // Update flow control window for stream1.
  quic::QuicWindowUpdateFrame window_update1(quic::kInvalidControlFrameId, stream1->id(),
                                             32 * 1024);
  stream1->OnWindowUpdateFrame(window_update1);
  EXPECT_CALL(stream_callbacks, onBelowWriteBufferLowWatermark()).WillOnce(Invoke([stream1]() {
    // Write rest response to stream1.
    std::string rest_response(1, 'a');
    Buffer::OwnedImpl buffer(rest_response);
    stream1->encodeData(buffer, true);
  }));
  envoy_quic_session_.OnCanWrite();
  EXPECT_TRUE(stream1->flow_controller()->IsBlocked());

  // Update flow control window for stream2.
  quic::QuicWindowUpdateFrame window_update2(quic::kInvalidControlFrameId, stream2->id(),
                                             32 * 1024);
  stream2->OnWindowUpdateFrame(window_update2);
  EXPECT_CALL(stream_callbacks2, onBelowWriteBufferLowWatermark()).WillOnce(Invoke([stream2]() {
    // Write rest response to stream2.
    std::string rest_response(1, 'a');
    Buffer::OwnedImpl buffer(rest_response);
    stream2->encodeData(buffer, true);
  }));
  // Writing out another 16k on stream2 will trigger connection's send buffer
  // come down below low watermark.
  EXPECT_CALL(network_connection_callbacks_, onBelowWriteBufferLowWatermark)
      .WillOnce(Invoke([this]() {
        // This call shouldn't be propagate to stream1 and stream2 because they both wrote to the
        // end of stream.
        http_connection_->onUnderlyingConnectionBelowWriteBufferLowWatermark();
      }));
  EXPECT_CALL(stream_callbacks3, onBelowWriteBufferLowWatermark()).WillOnce(Invoke([=]() {
    std::string super_large_response(40 * 1024, 'a');
    Buffer::OwnedImpl buffer(super_large_response);
    // This call will buffer 24k on stream3, raise the buffered bytes above
    // high watermarks of the stream and connection.
    // But callback will not propagate to stream_callback3 as the steam is
    // ended locally.
    stream3->encodeData(buffer, true);
  }));
  EXPECT_CALL(network_connection_callbacks_, onAboveWriteBufferHighWatermark());
  envoy_quic_session_.OnCanWrite();
  EXPECT_TRUE(stream2->flow_controller()->IsBlocked());

  // Resetting stream3 should lower the buffered bytes, but callbacks will not
  // be triggered because reset callback has been already triggered.
  EXPECT_CALL(stream_callbacks3, onResetStream(Http::StreamResetReason::LocalReset, ""));
  // Connection buffered data book keeping should also be updated.
  EXPECT_CALL(network_connection_callbacks_, onBelowWriteBufferLowWatermark());
  stream3->resetStream(Http::StreamResetReason::LocalReset);

  // Update flow control window for stream1.
  quic::QuicWindowUpdateFrame window_update3(quic::kInvalidControlFrameId, stream1->id(),
                                             48 * 1024);
  stream1->OnWindowUpdateFrame(window_update3);
  // Update flow control window for stream2.
  quic::QuicWindowUpdateFrame window_update4(quic::kInvalidControlFrameId, stream2->id(),
                                             48 * 1024);
  stream2->OnWindowUpdateFrame(window_update4);
  envoy_quic_session_.OnCanWrite();

  EXPECT_TRUE(stream1->write_side_closed());
  EXPECT_TRUE(stream2->write_side_closed());
}

TEST_P(EnvoyQuicServerSessionTest, HeadersContributeToWatermarkGquic) {
  if (quic::VersionUsesHttp3(quic_version_[0].transport_version)) {
    installReadFilter();
    return;
  }
  // Switch to a encryption forward secure crypto stream.
  quic::test::QuicServerSessionBasePeer::SetCryptoStream(&envoy_quic_session_, nullptr);
  quic::test::QuicServerSessionBasePeer::SetCryptoStream(
      &envoy_quic_session_,
      new TestQuicCryptoServerStream(&crypto_config_, &compressed_certs_cache_,
                                     &envoy_quic_session_, &crypto_stream_helper_));
  quic_connection_->SetDefaultEncryptionLevel(quic::ENCRYPTION_FORWARD_SECURE);
  quic_connection_->SetEncrypter(
      quic::ENCRYPTION_FORWARD_SECURE,
      std::make_unique<quic::NullEncrypter>(quic::Perspective::IS_SERVER));
  // Drive congestion control manually.
  auto send_algorithm = new testing::NiceMock<quic::test::MockSendAlgorithm>;
  quic::test::QuicConnectionPeer::SetSendAlgorithm(quic_connection_, send_algorithm);
  EXPECT_CALL(*send_algorithm, PacingRate(_)).WillRepeatedly(Return(quic::QuicBandwidth::Zero()));
  EXPECT_CALL(*send_algorithm, BandwidthEstimate())
      .WillRepeatedly(Return(quic::QuicBandwidth::Zero()));
  EXPECT_CALL(*quic_connection_, SendControlFrame(_)).Times(AnyNumber());

  // Bump connection flow control window large enough not to interfere
  // stream writing.
  envoy_quic_session_.flow_controller()->UpdateSendWindowOffset(
      10 * quic::kDefaultFlowControlSendWindow);
  installReadFilter();
  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EXPECT_CALL(http_connection_callbacks_, newStream(_, false))
      .WillOnce(Invoke([&request_decoder, &stream_callbacks](Http::ResponseEncoder& encoder,
                                                             bool) -> Http::RequestDecoder& {
        encoder.getStream().addCallbacks(stream_callbacks);
        return request_decoder;
      }));
  quic::QuicStreamId stream_id =
      quic::VersionUsesHttp3(quic_version_[0].transport_version) ? 4u : 5u;
  auto stream1 =
      dynamic_cast<EnvoyQuicServerStream*>(envoy_quic_session_.GetOrCreateStream(stream_id));

  // Receive a GET request on created stream.
  quic::QuicHeaderList request_headers;
  request_headers.OnHeaderBlockStart();
  std::string host("www.abc.com");
  request_headers.OnHeader(":authority", host);
  request_headers.OnHeader(":method", "GET");
  request_headers.OnHeader(":path", "/");
  request_headers.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0, /*compressed_header_bytes=*/0);
  // Request headers should be propagated to decoder.
  EXPECT_CALL(request_decoder, decodeHeaders_(_, /*end_stream=*/true))
      .WillOnce(Invoke([&host](const Http::RequestHeaderMapPtr& decoded_headers, bool) {
        EXPECT_EQ(host, decoded_headers->getHostValue());
        EXPECT_EQ("/", decoded_headers->getPathValue());
        EXPECT_EQ(Http::Headers::get().MethodValues.Get, decoded_headers->getMethodValue());
      }));
  stream1->OnStreamHeaderList(/*fin=*/true, request_headers.uncompressed_header_bytes(),
                              request_headers);

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  // Make connection congestion control blocked so headers are buffered.
  EXPECT_CALL(*send_algorithm, CanSend(_)).WillRepeatedly(Return(false));
  stream1->encodeHeaders(response_headers, false);
  // Buffer a response slightly smaller than connection level watermark, but
  // with the previously buffered headers, this write should reach high
  // watermark.
  std::string response(24 * 1024 - 1, 'a');
  Buffer::OwnedImpl buffer(response);
  // Triggered twice, once by stream, the other time by connection.
  EXPECT_CALL(stream_callbacks, onAboveWriteBufferHighWatermark()).Times(2);
  EXPECT_CALL(network_connection_callbacks_, onAboveWriteBufferHighWatermark)
      .WillOnce(Invoke(
          [this]() { http_connection_->onUnderlyingConnectionAboveWriteBufferHighWatermark(); }));
  stream1->encodeData(buffer, false);
  EXPECT_FALSE(envoy_quic_session_.IsConnectionFlowControlBlocked());

  // Write the buffered data out till stream is flow control blocked. Both
  // stream and connection level buffers should drop below watermark.
  EXPECT_CALL(*send_algorithm, CanSend(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*send_algorithm, GetCongestionWindow()).WillRepeatedly(Return(quic::kDefaultTCPMSS));
  EXPECT_CALL(network_connection_callbacks_, onBelowWriteBufferLowWatermark)
      .WillOnce(Invoke(
          [this]() { http_connection_->onUnderlyingConnectionBelowWriteBufferLowWatermark(); }));
  EXPECT_CALL(stream_callbacks, onBelowWriteBufferLowWatermark()).Times(2);
  envoy_quic_session_.OnCanWrite();
  EXPECT_TRUE(stream1->flow_controller()->IsBlocked());

  // Buffer more response because of flow control. The buffered bytes become just below connection
  // level high watermark.
  std::string response1(16 * 1024 - 20, 'a');
  Buffer::OwnedImpl buffer1(response1);
  EXPECT_CALL(stream_callbacks, onAboveWriteBufferHighWatermark());
  stream1->encodeData(buffer1, false);

  // Make connection congestion control blocked again.
  EXPECT_CALL(*send_algorithm, CanSend(_)).WillRepeatedly(Return(false));
  // Buffering the trailers will cause connection to reach high watermark.
  EXPECT_CALL(network_connection_callbacks_, onAboveWriteBufferHighWatermark)
      .WillOnce(Invoke(
          [this]() { http_connection_->onUnderlyingConnectionAboveWriteBufferHighWatermark(); }));
  Http::TestResponseTrailerMapImpl response_trailers{{"trailer-key", "trailer-value"}};
  stream1->encodeTrailers(response_trailers);

  EXPECT_CALL(network_connection_callbacks_, onBelowWriteBufferLowWatermark)
      .WillOnce(Invoke(
          [this]() { http_connection_->onUnderlyingConnectionBelowWriteBufferLowWatermark(); }));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::LocalReset, _));
  stream1->resetStream(Http::StreamResetReason::LocalReset);
}

} // namespace Quic
} // namespace Envoy
