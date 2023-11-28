#include <memory>
#include <string>

#include "envoy/stats/stats_macros.h"

#include "source/common/event/libevent_scheduler.h"
#include "source/common/quic/envoy_quic_alarm_factory.h"
#include "source/common/quic/envoy_quic_connection_helper.h"
#include "source/common/quic/envoy_quic_server_connection.h"
#include "source/common/quic/envoy_quic_server_session.h"
#include "source/common/quic/envoy_quic_server_stream.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/server_codec_impl.h"
#include "source/server/configuration_impl.h"

#include "test/common/quic/test_proof_source.h"
#include "test/common/quic/test_utils.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/global.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/quic/core/crypto/null_encrypter.h"
#include "quiche/quic/core/deterministic_connection_id_generator.h"
#include "quiche/quic/core/quic_crypto_server_stream.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/test_tools/crypto_test_utils.h"
#include "quiche/quic/test_tools/quic_connection_peer.h"
#include "quiche/quic/test_tools/quic_server_session_base_peer.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

using testing::_;
using testing::AnyNumber;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Quic {

// Derive to have simpler priority mechanism.
class TestEnvoyQuicServerSession : public EnvoyQuicServerSession {
public:
  using EnvoyQuicServerSession::EnvoyQuicServerSession;

  bool ShouldYield(quic::QuicStreamId /*stream_id*/) override {
    // Never yield to other stream so that it's easier to predict stream write
    // behavior.
    return false;
  }

  using EnvoyQuicServerSession::GetCryptoStream;
};

class ProofSourceDetailsSetter {
public:
  virtual ~ProofSourceDetailsSetter() = default;

  virtual void setProofSourceDetails(std::unique_ptr<EnvoyQuicProofSourceDetails> details) = 0;
};

class TestQuicCryptoServerStream : public quic::QuicCryptoServerStream,
                                   public ProofSourceDetailsSetter {
public:
  ~TestQuicCryptoServerStream() override = default;

  explicit TestQuicCryptoServerStream(const quic::QuicCryptoServerConfig* crypto_config,
                                      quic::QuicCompressedCertsCache* compressed_certs_cache,
                                      quic::QuicSession* session,
                                      quic::QuicCryptoServerStreamBase::Helper* helper)
      : quic::QuicCryptoServerStream(crypto_config, compressed_certs_cache, session, helper) {}

  bool encryption_established() const override { return true; }

  const EnvoyQuicProofSourceDetails* ProofSourceDetails() const override { return details_.get(); }

  void setProofSourceDetails(std::unique_ptr<EnvoyQuicProofSourceDetails> details) override {
    details_ = std::move(details);
  }

private:
  std::unique_ptr<EnvoyQuicProofSourceDetails> details_;
};

class TestEnvoyQuicTlsServerHandshaker : public quic::TlsServerHandshaker,
                                         public ProofSourceDetailsSetter {
public:
  ~TestEnvoyQuicTlsServerHandshaker() override = default;

  TestEnvoyQuicTlsServerHandshaker(quic::QuicSession* session,
                                   const quic::QuicCryptoServerConfig& crypto_config)
      : quic::TlsServerHandshaker(session, &crypto_config),
        params_(new quic::QuicCryptoNegotiatedParameters) {
    params_->cipher_suite = 1;
  }

  bool encryption_established() const override { return true; }
  const EnvoyQuicProofSourceDetails* ProofSourceDetails() const override { return details_.get(); }
  void setProofSourceDetails(std::unique_ptr<EnvoyQuicProofSourceDetails> details) override {
    details_ = std::move(details);
  }
  const quic::QuicCryptoNegotiatedParameters& crypto_negotiated_params() const override {
    return *params_;
  }

private:
  std::unique_ptr<EnvoyQuicProofSourceDetails> details_;
  quiche::QuicheReferenceCountedPointer<quic::QuicCryptoNegotiatedParameters> params_;
};

class EnvoyQuicTestCryptoServerStreamFactory : public EnvoyQuicCryptoServerStreamFactoryInterface {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override { return nullptr; }
  std::string name() const override { return "quic.test_crypto_server_stream"; }

  std::unique_ptr<quic::QuicCryptoServerStreamBase> createEnvoyQuicCryptoServerStream(
      const quic::QuicCryptoServerConfig* crypto_config,
      quic::QuicCompressedCertsCache* compressed_certs_cache, quic::QuicSession* session,
      quic::QuicCryptoServerStreamBase::Helper* helper,
      OptRef<const Network::DownstreamTransportSocketFactory> /*transport_socket_factory*/,
      Event::Dispatcher& /*dispatcher*/) override {
    switch (session->connection()->version().handshake_protocol) {
    case quic::PROTOCOL_QUIC_CRYPTO:
      return std::make_unique<TestQuicCryptoServerStream>(crypto_config, compressed_certs_cache,
                                                          session, helper);
    case quic::PROTOCOL_TLS1_3:
      return std::make_unique<TestEnvoyQuicTlsServerHandshaker>(session, *crypto_config);
    case quic::PROTOCOL_UNSUPPORTED:
      ASSERT(false, "Unknown handshake protocol");
    }
    return nullptr;
  }
};

class EnvoyQuicServerSessionTest : public testing::Test {
public:
  EnvoyQuicServerSessionTest()
      : api_(Api::createApiForTest(time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")), connection_helper_(*dispatcher_),
        alarm_factory_(*dispatcher_, *connection_helper_.GetClock()),
        quic_version_({[]() { return quic::CurrentSupportedHttp3Versions()[0]; }()}),
        quic_stat_names_(listener_config_.listenerScope().symbolTable()),
        quic_connection_(new MockEnvoyQuicServerConnection(
            connection_helper_, alarm_factory_, writer_, quic_version_, *listener_config_.socket_,
            connection_id_generator_)),
        crypto_config_(quic::QuicCryptoServerConfig::TESTING, quic::QuicRandom::GetInstance(),
                       std::make_unique<TestProofSource>(), quic::KeyExchangeSource::Default()),
        connection_stats_({QUIC_CONNECTION_STATS(
            POOL_COUNTER_PREFIX(listener_config_.listenerScope(), "quic.connection"))}),
        envoy_quic_session_(
            quic_config_, quic_version_,
            std::unique_ptr<MockEnvoyQuicServerConnection>(quic_connection_),
            /*visitor=*/nullptr, &crypto_stream_helper_, &crypto_config_, &compressed_certs_cache_,
            *dispatcher_,
            /*send_buffer_limit*/ quic::kDefaultFlowControlSendWindow * 1.5, quic_stat_names_,
            listener_config_.listenerScope(), crypto_stream_factory_,
            std::make_unique<StreamInfo::StreamInfoImpl>(
                dispatcher_->timeSource(),
                quic_connection_->connectionSocket()->connectionInfoProviderSharedPtr()),
            connection_stats_),
        stats_({ALL_HTTP3_CODEC_STATS(
            POOL_COUNTER_PREFIX(listener_config_.listenerScope(), "http3."),
            POOL_GAUGE_PREFIX(listener_config_.listenerScope(), "http3."))}) {

    EXPECT_EQ(time_system_.systemTime(), envoy_quic_session_.streamInfo().startTime());
    EXPECT_EQ(EMPTY_STRING, envoy_quic_session_.nextProtocol());

    // Advance time and trigger update of Dispatcher::approximateMonotonicTime()
    // because zero QuicTime is considered uninitialized.
    time_system_.advanceTimeAndRun(std::chrono::milliseconds(1), *dispatcher_,
                                   Event::Dispatcher::RunType::NonBlock);
    connection_helper_.GetClock()->Now();

    ON_CALL(writer_, WritePacket(_, _, _, _, _, _))
        .WillByDefault(Invoke([](const char*, size_t buf_len, const quic::QuicIpAddress&,
                                 const quic::QuicSocketAddress&, quic::PerPacketOptions*,
                                 const quic::QuicPacketWriterParams&) {
          return quic::WriteResult{quic::WRITE_STATUS_OK, static_cast<int>(buf_len)};
        }));
    ON_CALL(crypto_stream_helper_, CanAcceptClientHello(_, _, _, _, _)).WillByDefault(Return(true));
  }

  void SetUp() override {
    envoy_quic_session_.Initialize();
    setQuicConfigWithDefaultValues(envoy_quic_session_.config());
    envoy_quic_session_.OnConfigNegotiated();
    quic::test::QuicConfigPeer::SetNegotiated(envoy_quic_session_.config(), true);
    quic::test::QuicConnectionPeer::SetAddressValidated(quic_connection_);
    quic_connection_->SetEncrypter(
        quic::ENCRYPTION_FORWARD_SECURE,
        std::make_unique<quic::test::TaggingEncrypter>(quic::ENCRYPTION_FORWARD_SECURE));
    quic_connection_->SetDefaultEncryptionLevel(quic::ENCRYPTION_FORWARD_SECURE);
  }

  bool installReadFilter() {
    // Setup read filter.
    read_filter_ = std::make_shared<Network::MockReadFilter>(),
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
      http_connection_ = std::make_unique<QuicHttpServerConnectionImpl>(
          envoy_quic_session_, http_connection_callbacks_, stats_, http3_options_, 64 * 1024, 100,
          envoy::config::core::v3::HttpProtocolOptions::ALLOW);
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
    quic::QuicStreamId stream_id = 4u;
    return envoy_quic_session_.GetOrCreateStream(stream_id);
  }

  void TearDown() override {
    if (quic_connection_->connected()) {
      EXPECT_CALL(*quic_connection_, SendConnectionClosePacket(quic::QUIC_NO_ERROR, _, _));
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
  QuicStatNames quic_stat_names_;
  quic::DeterministicConnectionIdGenerator connection_id_generator_{
      quic::kQuicDefaultConnectionIdLength};
  MockEnvoyQuicServerConnection* quic_connection_;
  quic::QuicConfig quic_config_;
  quic::QuicCryptoServerConfig crypto_config_;
  testing::NiceMock<quic::test::MockQuicCryptoServerStreamHelper> crypto_stream_helper_;
  EnvoyQuicTestCryptoServerStreamFactory crypto_stream_factory_;
  QuicConnectionStats connection_stats_;
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
  Http::Http3::CodecStats stats_;
  envoy::config::core::v3::Http3ProtocolOptions http3_options_;
};

TEST_F(EnvoyQuicServerSessionTest, NewStreamBeforeInitializingFilter) {
  quic::QuicStreamId stream_id = 4u;
  EXPECT_ENVOY_BUG(envoy_quic_session_.GetOrCreateStream(stream_id),
                   fmt::format("attempts to create stream", envoy_quic_session_.id(), stream_id));
  EXPECT_CALL(*quic_connection_, SendConnectionClosePacket(quic::QUIC_NO_ERROR, _, _));
  EXPECT_CALL(*quic_connection_, SendControlFrame(_))
      .Times(testing::AtMost(1))
      .WillOnce(Invoke([](const quic::QuicFrame&) { return false; }));
  envoy_quic_session_.close(Network::ConnectionCloseType::NoFlush);
}

TEST_F(EnvoyQuicServerSessionTest, NewStream) {
  installReadFilter();

  EXPECT_EQ(envoy_quic_session_.GetCryptoStream()->GetSsl(),
            static_cast<const QuicSslConnectionInfo&>(*envoy_quic_session_.ssl()).ssl());
  Http::MockRequestDecoder request_decoder;
  EXPECT_CALL(http_connection_callbacks_, newStream(_, false))
      .WillOnce(testing::ReturnRef(request_decoder));
  EXPECT_CALL(request_decoder, accessLogHandlers());
  quic::QuicStreamId stream_id = 4u;
  auto stream =
      reinterpret_cast<quic::QuicSpdyStream*>(envoy_quic_session_.GetOrCreateStream(stream_id));

  // Basic checks.
  ASSERT_FALSE(envoy_quic_session_.startSecureTransport());

  // Receive a GET request on created stream.
  quic::QuicHeaderList headers;
  headers.OnHeaderBlockStart();
  std::string host("www.abc.com");
  headers.OnHeader(":authority", host);
  headers.OnHeader(":method", "GET");
  headers.OnHeader(":path", "/");
  headers.OnHeader(":scheme", "https");
  headers.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0, /*compressed_header_bytes=*/0);
  // Request headers should be propagated to decoder.
  EXPECT_CALL(request_decoder, decodeHeaders_(_, /*end_stream=*/true))
      .WillOnce(Invoke([&host](const Http::RequestHeaderMapSharedPtr& decoded_headers, bool) {
        EXPECT_EQ(host, decoded_headers->getHostValue());
        EXPECT_EQ("/", decoded_headers->getPathValue());
        EXPECT_EQ(Http::Headers::get().MethodValues.Get, decoded_headers->getMethodValue());
      }));
  stream->OnStreamHeaderList(/*fin=*/true, headers.uncompressed_header_bytes(), headers);
}

TEST_F(EnvoyQuicServerSessionTest, InvalidIncomingStreamId) {
  installReadFilter();
  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  // IETF stream 5 is server initiated.
  quic::QuicStreamId stream_id = 5u;
  std::string data("aaaa");
  quic::QuicStreamFrame stream_frame(stream_id, false, 0, data);
  EXPECT_CALL(http_connection_callbacks_, newStream(_, false)).Times(0);
  EXPECT_CALL(*quic_connection_, SendConnectionClosePacket(quic::QUIC_HTTP_STREAM_WRONG_DIRECTION,
                                                           _, "Data for nonexistent stream"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));

  envoy_quic_session_.OnStreamFrame(stream_frame);
}

TEST_F(EnvoyQuicServerSessionTest, NoNewStreamForInvalidIncomingStream) {
  installReadFilter();
  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  // IETF stream 5 is server initiated.
  quic::QuicStreamId stream_id = 5u;
  EXPECT_CALL(http_connection_callbacks_, newStream(_, false)).Times(0);
  EXPECT_CALL(*quic_connection_, SendConnectionClosePacket(quic::QUIC_HTTP_STREAM_WRONG_DIRECTION,
                                                           _, "Data for nonexistent stream"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));

  // Stream creation on closed connection should fail.
  EXPECT_EQ(nullptr, envoy_quic_session_.GetOrCreateStream(stream_id));
}

TEST_F(EnvoyQuicServerSessionTest, OnResetFrameIetfQuic) {
  installReadFilter();

  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EXPECT_CALL(request_decoder, accessLogHandlers()).Times(3);
  auto stream1 =
      dynamic_cast<EnvoyQuicServerStream*>(createNewStream(request_decoder, stream_callbacks));
  // Receiving RESET_STREAM alone should only close read side.
  quic::QuicRstStreamFrame rst1(/*control_frame_id=*/1u, stream1->id(),
                                quic::QUIC_ERROR_PROCESSING_STREAM, /*bytes_written=*/0u);
  envoy_quic_session_.OnRstStream(rst1);
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  stream1->encodeHeaders(response_headers, true);

  EXPECT_EQ(1U, TestUtility::findCounter(
                    listener_config_.listenerScope().store(),
                    "http3.downstream.rx.quic_reset_stream_error_code_QUIC_ERROR_PROCESSING_STREAM")
                    ->value());

  EXPECT_CALL(http_connection_callbacks_, newStream(_, false))
      .WillOnce(Invoke([&request_decoder, &stream_callbacks](Http::ResponseEncoder& encoder,
                                                             bool) -> Http::RequestDecoder& {
        encoder.getStream().addCallbacks(stream_callbacks);
        return request_decoder;
      }));
  quic::QuicStream* stream2 = envoy_quic_session_.GetOrCreateStream(stream1->id() + 4u);
  quic::QuicRstStreamFrame rst2(/*control_frame_id=*/1u, stream2->id(), quic::QUIC_REFUSED_STREAM,
                                /*bytes_written=*/0u);
  quic::QuicStopSendingFrame stop_sending2(/*control_frame_id=*/1u, stream2->id(),
                                           quic::QUIC_REFUSED_STREAM);
  // Receiving both STOP_SENDING and RESET_STREAM should close the stream.
  EXPECT_CALL(stream_callbacks,
              onResetStream(Http::StreamResetReason::RemoteRefusedStreamReset, _));
  EXPECT_CALL(*quic_connection_, SendControlFrame(_))
      .WillOnce(Invoke([stream_id = stream2->id()](const quic::QuicFrame& frame) {
        EXPECT_EQ(stream_id, frame.rst_stream_frame->stream_id);
        EXPECT_EQ(quic::QUIC_REFUSED_STREAM, frame.rst_stream_frame->error_code);
        return false;
      }));
  envoy_quic_session_.OnStopSendingFrame(stop_sending2);
  envoy_quic_session_.OnRstStream(rst2);
  EXPECT_EQ(1U, TestUtility::findCounter(
                    listener_config_.listenerScope().store(),
                    "http3.downstream.rx.quic_reset_stream_error_code_QUIC_REFUSED_STREAM")
                    ->value());

  EXPECT_CALL(http_connection_callbacks_, newStream(_, false))
      .WillOnce(Invoke([&request_decoder, &stream_callbacks](Http::ResponseEncoder& encoder,
                                                             bool) -> Http::RequestDecoder& {
        encoder.getStream().addCallbacks(stream_callbacks);
        return request_decoder;
      }));
  quic::QuicStream* stream3 = envoy_quic_session_.GetOrCreateStream(stream1->id() + 8u);
  quic::QuicRstStreamFrame rst3(/*control_frame_id=*/1u, stream3->id(), quic::QUIC_REFUSED_STREAM,
                                /*bytes_written=*/0u);
  quic::QuicStopSendingFrame stop_sending3(/*control_frame_id=*/1u, stream3->id(),
                                           quic::QUIC_REFUSED_STREAM);
  // Receiving both STOP_SENDING and RESET_STREAM should close the stream.
  EXPECT_CALL(stream_callbacks,
              onResetStream(Http::StreamResetReason::RemoteRefusedStreamReset, _));
  envoy_quic_session_.OnRstStream(rst3);
  envoy_quic_session_.OnStopSendingFrame(stop_sending3);
  EXPECT_EQ(2U, TestUtility::findCounter(
                    listener_config_.listenerScope().store(),
                    "http3.downstream.rx.quic_reset_stream_error_code_QUIC_REFUSED_STREAM")
                    ->value());
}

TEST_F(EnvoyQuicServerSessionTest, ConnectionClose) {
  installReadFilter();

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
}

TEST_F(EnvoyQuicServerSessionTest, ConnectionCloseWithActiveStream) {
  installReadFilter();

  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EXPECT_CALL(request_decoder, accessLogHandlers());
  quic::QuicStream* stream = createNewStream(request_decoder, stream_callbacks);
  EXPECT_CALL(*quic_connection_, SendConnectionClosePacket(quic::QUIC_NO_ERROR, _, _));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  envoy_quic_session_.close(Network::ConnectionCloseType::NoFlush);
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  EXPECT_TRUE(stream->write_side_closed() && stream->reading_stopped());
}

TEST_F(EnvoyQuicServerSessionTest, RemoteConnectionCloseWithActiveStream) {
  installReadFilter();

  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EXPECT_CALL(request_decoder, accessLogHandlers());
  quic::QuicStream* stream = createNewStream(request_decoder, stream_callbacks);
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::RemoteConnectionFailure, _));
  quic::QuicConnectionCloseFrame frame(quic_version_[0].transport_version,
                                       quic::QUIC_HANDSHAKE_TIMEOUT, quic::NO_IETF_QUIC_ERROR,
                                       "dummy details",
                                       /* transport_close_frame_type = */ 0);
  quic_connection_->OnConnectionCloseFrame(frame);
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  EXPECT_TRUE(stream->write_side_closed() && stream->reading_stopped());
}

TEST_F(EnvoyQuicServerSessionTest, NoFlushWithDataToWrite) {
  installReadFilter();

  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EXPECT_CALL(request_decoder, accessLogHandlers());
  quic::QuicStream* stream = createNewStream(request_decoder, stream_callbacks);
  envoy_quic_session_.MarkConnectionLevelWriteBlocked(stream->id());
  EXPECT_CALL(*quic_connection_, SendConnectionClosePacket(quic::QUIC_NO_ERROR, _, _));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  // Even though the stream is write blocked, connection should be closed
  // immediately.
  envoy_quic_session_.close(Network::ConnectionCloseType::NoFlush);
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  EXPECT_TRUE(stream->write_side_closed() && stream->reading_stopped());
}

TEST_F(EnvoyQuicServerSessionTest, FlushCloseWithDataToWrite) {
  installReadFilter();
  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EXPECT_CALL(request_decoder, accessLogHandlers());
  quic::QuicStream* stream = createNewStream(request_decoder, stream_callbacks);

  envoy_quic_session_.MarkConnectionLevelWriteBlocked(stream->id());
  EXPECT_TRUE(envoy_quic_session_.HasDataToWrite());
  // Connection shouldn't be closed right away as there is a stream write blocked.
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWrite);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());
  EXPECT_CALL(*quic_connection_, SendConnectionClosePacket(quic::QUIC_NO_ERROR, _, _));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  // Unblock that stream to trigger actual connection close.
  quic_connection_->OnCanWrite();
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  EXPECT_FALSE(quic_connection_->connected());
}

// Tests that a write event after flush close should update the delay close
// timer.
TEST_F(EnvoyQuicServerSessionTest, WriteUpdatesDelayCloseTimer) {
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
  EXPECT_CALL(request_decoder, accessLogHandlers());
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
  request_headers.OnHeader(":scheme", "https");
  request_headers.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0, /*compressed_header_bytes=*/0);
  // Request headers should be propagated to decoder.
  EXPECT_CALL(request_decoder, decodeHeaders_(_, /*end_stream=*/true))
      .WillOnce(Invoke([&host](const Http::RequestHeaderMapSharedPtr& decoded_headers, bool) {
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
  EXPECT_TRUE(stream->IsFlowControlBlocked());
  EXPECT_FALSE(envoy_quic_session_.IsConnectionFlowControlBlocked());

  // Connection shouldn't be closed right away as there is a stream write blocked.
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWrite);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  time_system_.advanceTimeAndRun(std::chrono::milliseconds(10), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  // Another write event with updated flow control window should unblock the stream and flush some
  // stream data and update the timer. But it shouldn't close connection because there should still
  // be data buffered.
  quic::QuicWindowUpdateFrame window_update(quic::kInvalidControlFrameId, stream->id(),
                                            quic::kDefaultFlowControlSendWindow + 1);
  stream->OnWindowUpdateFrame(window_update);
  quic_connection_->OnCanWrite();
  EXPECT_TRUE(envoy_quic_session_.HasDataToWrite());
  EXPECT_TRUE(stream->IsFlowControlBlocked());

  // Timer shouldn't fire at original deadline.
  time_system_.advanceTimeAndRun(std::chrono::milliseconds(90), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  EXPECT_CALL(*quic_connection_,
              SendConnectionClosePacket(
                  quic::QUIC_NO_ERROR, _,
                  "Closed by application with reason: triggered_delayed_close_timeout"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  // Advance the time to fire connection close timer.
  time_system_.advanceTimeAndRun(std::chrono::milliseconds(10), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  EXPECT_FALSE(quic_connection_->connected());
}

// Tests that if delay close timeout is not configured, flush close will not act
// based on timeout.
TEST_F(EnvoyQuicServerSessionTest, FlushCloseNoTimeout) {
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
      std::make_unique<quic::test::TaggingEncrypter>(quic::ENCRYPTION_FORWARD_SECURE));
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
  EXPECT_CALL(request_decoder, accessLogHandlers());
  // Create a stream and write enough data to make it blocked.
  auto stream =
      dynamic_cast<EnvoyQuicServerStream*>(createNewStream(request_decoder, stream_callbacks));

  // Receive a GET request on created stream.
  quic::QuicHeaderList request_headers;
  request_headers.OnHeaderBlockStart();
  std::string host("www.abc.com");
  request_headers.OnHeader(":authority", host);
  request_headers.OnHeader(":method", "GET");
  request_headers.OnHeader(":scheme", "https");
  request_headers.OnHeader(":path", "/");
  request_headers.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0, /*compressed_header_bytes=*/0);
  // Request headers should be propagated to decoder.
  EXPECT_CALL(request_decoder, decodeHeaders_(_, /*end_stream=*/true))
      .WillOnce(Invoke([&host](const Http::RequestHeaderMapSharedPtr& decoded_headers, bool) {
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
  EXPECT_TRUE(stream->IsFlowControlBlocked());
  EXPECT_FALSE(envoy_quic_session_.IsConnectionFlowControlBlocked());

  // Connection shouldn't be closed right away as there is a stream write blocked.
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWrite);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());
  // Another write event without updating flow control window shouldn't trigger
  // connection close.
  quic_connection_->OnCanWrite();
  EXPECT_TRUE(envoy_quic_session_.HasDataToWrite());

  // No timeout set, so alarm shouldn't fire.
  time_system_.advanceTimeAndRun(std::chrono::milliseconds(100), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  // Force close connection.
  EXPECT_CALL(*quic_connection_, SendConnectionClosePacket(quic::QUIC_NO_ERROR, _, _));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(*quic_connection_, SendControlFrame(_))
      .Times(testing::AtMost(1))
      .WillOnce(Invoke([](const quic::QuicFrame&) { return false; }));
  envoy_quic_session_.close(Network::ConnectionCloseType::NoFlush);
}

TEST_F(EnvoyQuicServerSessionTest, FlushCloseWithTimeout) {
  installReadFilter();
  envoy_quic_session_.setDelayedCloseTimeout(std::chrono::milliseconds(100));
  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EXPECT_CALL(request_decoder, accessLogHandlers());
  quic::QuicStream* stream = createNewStream(request_decoder, stream_callbacks);

  envoy_quic_session_.MarkConnectionLevelWriteBlocked(stream->id());
  EXPECT_TRUE(envoy_quic_session_.HasDataToWrite());
  // Connection shouldn't be closed right away as there is a stream write blocked.
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWrite);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  // Advance the time a bit and try to close again. The delay close timer
  // shouldn't be rescheduled by this call.
  time_system_.advanceTimeAndRun(std::chrono::milliseconds(10), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWriteAndDelay);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  EXPECT_CALL(*quic_connection_, SendConnectionClosePacket(quic::QUIC_NO_ERROR, _, _));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  // Advance the time to fire connection close timer.
  time_system_.advanceTimeAndRun(std::chrono::milliseconds(90), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  EXPECT_FALSE(quic_connection_->connected());
}

TEST_F(EnvoyQuicServerSessionTest, FlushAndWaitForCloseWithTimeout) {
  installReadFilter();
  envoy_quic_session_.setDelayedCloseTimeout(std::chrono::milliseconds(100));
  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EXPECT_CALL(request_decoder, accessLogHandlers());
  quic::QuicStream* stream = createNewStream(request_decoder, stream_callbacks);

  envoy_quic_session_.MarkConnectionLevelWriteBlocked(stream->id());
  EXPECT_TRUE(envoy_quic_session_.HasDataToWrite());
  // Connection shouldn't be closed right away as there is a stream write blocked.
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWriteAndDelay);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());
  // Unblocking the stream shouldn't close the connection as it should be
  // delayed.
  time_system_.advanceTimeAndRun(std::chrono::milliseconds(10), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  quic_connection_->OnCanWrite();
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  // Advance the time to fire connection close timer.
  EXPECT_CALL(*quic_connection_, SendConnectionClosePacket(quic::QUIC_NO_ERROR, _, _));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  time_system_.advanceTimeAndRun(std::chrono::milliseconds(90), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  EXPECT_FALSE(quic_connection_->connected());
}

TEST_F(EnvoyQuicServerSessionTest, FlusWriteTransitToFlushWriteWithDelay) {
  installReadFilter();
  envoy_quic_session_.setDelayedCloseTimeout(std::chrono::milliseconds(100));
  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EXPECT_CALL(request_decoder, accessLogHandlers());
  quic::QuicStream* stream = createNewStream(request_decoder, stream_callbacks);

  envoy_quic_session_.MarkConnectionLevelWriteBlocked(stream->id());
  EXPECT_TRUE(envoy_quic_session_.HasDataToWrite());
  // Connection shouldn't be closed right away as there is a stream write blocked.
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWrite);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  time_system_.advanceTimeAndRun(std::chrono::milliseconds(10), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  // The closing behavior should be changed.
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWriteAndDelay);
  // Unblocking the stream shouldn't close the connection as it should be
  // delayed.
  quic_connection_->OnCanWrite();
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  EXPECT_CALL(*quic_connection_, SendConnectionClosePacket(quic::QUIC_NO_ERROR, _, _));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  // Advance the time to fire connection close timer.
  time_system_.advanceTimeAndRun(std::chrono::milliseconds(90), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  EXPECT_FALSE(quic_connection_->connected());
}

TEST_F(EnvoyQuicServerSessionTest, FlushAndWaitForCloseWithNoPendingData) {
  installReadFilter();
  envoy_quic_session_.setDelayedCloseTimeout(std::chrono::milliseconds(100));
  // This close should be delayed as configured.
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWriteAndDelay);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  // Advance the time a bit and try to close again. The delay close timer
  // shouldn't be rescheduled by this call.
  time_system_.advanceTimeAndRun(std::chrono::milliseconds(10), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  envoy_quic_session_.close(Network::ConnectionCloseType::FlushWriteAndDelay);
  EXPECT_EQ(Network::Connection::State::Open, envoy_quic_session_.state());

  EXPECT_CALL(*quic_connection_, SendConnectionClosePacket(quic::QUIC_NO_ERROR, _, _));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  // Advance the time to fire connection close timer.
  time_system_.advanceTimeAndRun(std::chrono::milliseconds(90), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
}

TEST_F(EnvoyQuicServerSessionTest, ShutdownNotice) {
  installReadFilter();
  testing::NiceMock<quic::test::MockHttp3DebugVisitor> debug_visitor;
  envoy_quic_session_.set_debug_visitor(&debug_visitor);
  EXPECT_CALL(debug_visitor, OnGoAwayFrameSent(_));
  http_connection_->shutdownNotice();
}

TEST_F(EnvoyQuicServerSessionTest, GoAway) {
  installReadFilter();
  testing::NiceMock<quic::test::MockHttp3DebugVisitor> debug_visitor;
  envoy_quic_session_.set_debug_visitor(&debug_visitor);
  EXPECT_CALL(debug_visitor, OnGoAwayFrameSent(_));
  http_connection_->goAway();
}

TEST_F(EnvoyQuicServerSessionTest, ConnectedAfterHandshake) {
  installReadFilter();
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::Connected));
  if (!quic_version_[0].UsesTls()) {
    envoy_quic_session_.SetDefaultEncryptionLevel(quic::ENCRYPTION_FORWARD_SECURE);
  } else {
    EXPECT_CALL(*quic_connection_, SendControlFrame(_));
    envoy_quic_session_.OnTlsHandshakeComplete();
  }
  EXPECT_EQ(nullptr, envoy_quic_session_.socketOptions());
  EXPECT_TRUE(quic_connection_->connectionSocket()->ioHandle().isOpen());
  EXPECT_TRUE(quic_connection_->connectionSocket()->ioHandle().close().ok());
  EXPECT_FALSE(quic_connection_->connectionSocket()->ioHandle().isOpen());
}

TEST_F(EnvoyQuicServerSessionTest, NetworkConnectionInterface) {
  installReadFilter();
  EXPECT_EQ(dispatcher_.get(), &envoy_quic_session_.dispatcher());
  EXPECT_TRUE(envoy_quic_session_.readEnabled());
}

TEST_F(EnvoyQuicServerSessionTest, SendBufferWatermark) {
  // Switch to a encryption forward secure crypto stream.
  quic::test::QuicServerSessionBasePeer::SetCryptoStream(&envoy_quic_session_, nullptr);
  quic::test::QuicServerSessionBasePeer::SetCryptoStream(
      &envoy_quic_session_,
      new TestQuicCryptoServerStream(&crypto_config_, &compressed_certs_cache_,
                                     &envoy_quic_session_, &crypto_stream_helper_));
  quic_connection_->SetDefaultEncryptionLevel(quic::ENCRYPTION_FORWARD_SECURE);
  quic_connection_->SetEncrypter(
      quic::ENCRYPTION_FORWARD_SECURE,
      std::make_unique<quic::test::TaggingEncrypter>(quic::ENCRYPTION_FORWARD_SECURE));
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
  EXPECT_CALL(request_decoder, accessLogHandlers());
  EXPECT_CALL(http_connection_callbacks_, newStream(_, false))
      .WillOnce(Invoke([&request_decoder, &stream_callbacks](Http::ResponseEncoder& encoder,
                                                             bool) -> Http::RequestDecoder& {
        encoder.getStream().addCallbacks(stream_callbacks);
        return request_decoder;
      }));
  quic::QuicStreamId stream_id = 4u;
  auto stream1 =
      dynamic_cast<EnvoyQuicServerStream*>(envoy_quic_session_.GetOrCreateStream(stream_id));

  // Receive a GET request on created stream.
  quic::QuicHeaderList request_headers;
  request_headers.OnHeaderBlockStart();
  std::string host("www.abc.com");
  request_headers.OnHeader(":authority", host);
  request_headers.OnHeader(":method", "GET");
  request_headers.OnHeader(":path", "/");
  request_headers.OnHeader(":scheme", "https");
  request_headers.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0, /*compressed_header_bytes=*/0);
  // Request headers should be propagated to decoder.
  EXPECT_CALL(request_decoder, decodeHeaders_(_, /*end_stream=*/true))
      .WillOnce(Invoke([&host](const Http::RequestHeaderMapSharedPtr& decoded_headers, bool) {
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
  EXPECT_TRUE(stream1->IsFlowControlBlocked());
  EXPECT_FALSE(envoy_quic_session_.IsConnectionFlowControlBlocked());

  // Receive another request and send back response to trigger connection level
  // send buffer watermark.
  Http::MockRequestDecoder request_decoder2;
  Http::MockStreamCallbacks stream_callbacks2;
  EXPECT_CALL(request_decoder2, accessLogHandlers());
  EXPECT_CALL(http_connection_callbacks_, newStream(_, false))
      .WillOnce(Invoke([&request_decoder2, &stream_callbacks2](Http::ResponseEncoder& encoder,
                                                               bool) -> Http::RequestDecoder& {
        encoder.getStream().addCallbacks(stream_callbacks2);
        return request_decoder2;
      }));
  auto stream2 =
      dynamic_cast<EnvoyQuicServerStream*>(envoy_quic_session_.GetOrCreateStream(stream_id + 4));
  EXPECT_CALL(request_decoder2, decodeHeaders_(_, /*end_stream=*/true))
      .WillOnce(Invoke([&host](const Http::RequestHeaderMapSharedPtr& decoded_headers, bool) {
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
  EXPECT_CALL(request_decoder3, accessLogHandlers());
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
      .WillOnce(Invoke([&host](const Http::RequestHeaderMapSharedPtr& decoded_headers, bool) {
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
  EXPECT_TRUE(stream1->IsFlowControlBlocked());

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
  EXPECT_TRUE(stream2->IsFlowControlBlocked());

  // Resetting stream3 should lower the buffered bytes, but callbacks will not
  // be triggered because end stream is already encoded.
  EXPECT_CALL(stream_callbacks3, onResetStream(Http::StreamResetReason::LocalReset, "")).Times(0);
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

TEST_F(EnvoyQuicServerSessionTest, IncomingUnidirectionalReadStream) {
  installReadFilter();
  Http::MockRequestDecoder request_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  // IETF stream 2 is client initiated uni-directional stream.
  quic::QuicStreamId stream_id = 2u;
  auto payload = std::make_unique<char[]>(8);
  quic::QuicDataWriter payload_writer(8, payload.get());
  EXPECT_TRUE(payload_writer.WriteVarInt62(1ul));
  EXPECT_CALL(http_connection_callbacks_, newStream(_, false)).Times(0u);
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(*quic_connection_, SendConnectionClosePacket(quic::QUIC_HTTP_RECEIVE_SERVER_PUSH, _,
                                                           "Received server push stream"));
  quic::QuicStreamFrame stream_frame(stream_id, false, 0, absl::string_view(payload.get(), 1));
  envoy_quic_session_.OnStreamFrame(stream_frame);
}

TEST_F(EnvoyQuicServerSessionTest, GetRttAndCwnd) {
  installReadFilter();
  EXPECT_GT(envoy_quic_session_.lastRoundTripTime().value(), std::chrono::microseconds(0));
  // Just make sure the CWND is non-zero. We don't want to make strong assertions on what the value
  // should be in this test, that is the job the congestion controllers' tests.
  EXPECT_GT(envoy_quic_session_.congestionWindowInBytes().value(), 500);

  envoy_quic_session_.configureInitialCongestionWindow(8000000, std::chrono::microseconds(1000000));
  EXPECT_GT(envoy_quic_session_.congestionWindowInBytes().value(),
            quic::kInitialCongestionWindow * quic::kDefaultTCPMSS);
}

TEST_F(EnvoyQuicServerSessionTest, SslConnectionInfoDumbImplmention) {
  installReadFilter();
  EXPECT_FALSE(envoy_quic_session_.ssl()->peerCertificatePresented());
  EXPECT_TRUE(envoy_quic_session_.ssl()->urlEncodedPemEncodedPeerCertificateChain().empty());
  EXPECT_TRUE(envoy_quic_session_.ssl()->dnsSansPeerCertificate().empty());
  EXPECT_TRUE(envoy_quic_session_.ssl()->dnsSansLocalCertificate().empty());
  EXPECT_FALSE(envoy_quic_session_.ssl()->validFromPeerCertificate().has_value());
  EXPECT_FALSE(envoy_quic_session_.ssl()->expirationPeerCertificate().has_value());
}

class EnvoyQuicServerSessionTestWillNotInitialize : public EnvoyQuicServerSessionTest {
  void SetUp() override {}
  void TearDown() override {
    EnvoyQuicServerSessionTest::SetUp();
    installReadFilter();
    EnvoyQuicServerSessionTest::TearDown();
  }
};

TEST_F(EnvoyQuicServerSessionTestWillNotInitialize, GetRttAndCwnd) {
  EXPECT_EQ(envoy_quic_session_.lastRoundTripTime(), absl::nullopt);
  EXPECT_EQ(envoy_quic_session_.congestionWindowInBytes(), absl::nullopt);
}

} // namespace Quic
} // namespace Envoy
