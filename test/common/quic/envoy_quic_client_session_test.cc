#include "envoy/stats/stats_macros.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/quic/client_codec_impl.h"
#include "source/common/quic/envoy_quic_alarm_factory.h"
#include "source/common/quic/envoy_quic_client_connection.h"
#include "source/common/quic/envoy_quic_client_session.h"
#include "source/common/quic/envoy_quic_connection_helper.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_client_packet_writer_factory_impl.h"
#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_client_stream.h"

#include "test/common/quic/test_utils.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/threadsafe_singleton_injector.h"

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

class EnvoyQuicClientConnectionPeer {
public:
  static void onFileEvent(EnvoyQuicClientConnection& connection, uint32_t events,
                          Network::ConnectionSocket& connection_socket) {
    connection.onFileEvent(events, connection_socket);
  }
};

class TestEnvoyQuicClientConnection : public EnvoyQuicClientConnection {
public:
  TestEnvoyQuicClientConnection(const quic::QuicConnectionId& server_connection_id,
                                quic::QuicConnectionHelperInterface& helper,
                                quic::QuicAlarmFactory& alarm_factory,
                                quic::QuicPacketWriter* writer,
                                const quic::ParsedQuicVersionVector& supported_versions,
                                Event::Dispatcher& dispatcher,
                                Network::ConnectionSocketPtr&& connection_socket,
                                quic::ConnectionIdGeneratorInterface& generator)
      : EnvoyQuicClientConnection(server_connection_id, helper, alarm_factory, writer, true,
                                  supported_versions, dispatcher, std::move(connection_socket),
                                  generator) {
    SetEncrypter(quic::ENCRYPTION_FORWARD_SECURE,
                 std::make_unique<quic::test::TaggingEncrypter>(quic::ENCRYPTION_FORWARD_SECURE));
    InstallDecrypter(quic::ENCRYPTION_FORWARD_SECURE,
                     std::make_unique<quic::test::TaggingDecrypter>());
    SetDefaultEncryptionLevel(quic::ENCRYPTION_FORWARD_SECURE);
  }

  void processPacket(Network::Address::InstanceConstSharedPtr local_address,
                     Network::Address::InstanceConstSharedPtr peer_address,
                     Buffer::InstancePtr buffer, MonotonicTime receive_time, uint8_t tos,
                     Buffer::OwnedImpl saved_cmsg) override {
    last_local_address_ = local_address;
    last_peer_address_ = peer_address;
    EnvoyQuicClientConnection::processPacket(local_address, peer_address, std::move(buffer),
                                             receive_time, tos, std::move(saved_cmsg));
    ++num_packets_received_;
  }

  MOCK_METHOD(void, SendConnectionClosePacket,
              (quic::QuicErrorCode, quic::QuicIetfTransportErrorCodes ietf_error,
               const std::string&));
  MOCK_METHOD(bool, SendControlFrame, (const quic::QuicFrame& frame));

  Network::Address::InstanceConstSharedPtr getLastLocalAddress() const {
    return last_local_address_;
  }

  Network::Address::InstanceConstSharedPtr getLastPeerAddress() const { return last_peer_address_; }

  uint32_t packetsReceived() const { return num_packets_received_; }

  using EnvoyQuicClientConnection::connectionStats;

private:
  Network::Address::InstanceConstSharedPtr last_local_address_;
  Network::Address::InstanceConstSharedPtr last_peer_address_;
  uint32_t num_packets_received_{0};
};

class EnvoyQuicClientSessionTest
    : public testing::TestWithParam<std::tuple<quic::ParsedQuicVersion, bool>> {
public:
  EnvoyQuicClientSessionTest()
      : api_(Api::createApiForTest(time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")), connection_helper_(*dispatcher_),
        alarm_factory_(*dispatcher_, *connection_helper_.GetClock()),
        quic_version_({std::get<0>(GetParam())}),
        writer_(new testing::NiceMock<quic::test::MockPacketWriter>()),
        peer_addr_(
            Network::Test::getCanonicalLoopbackAddress(TestEnvironment::getIpVersionsForTest()[0])),
        self_addr_(Network::Utility::getAddressWithPort(
            *Network::Test::getCanonicalLoopbackAddress(TestEnvironment::getIpVersionsForTest()[0]),
            54321)),
        peer_socket_(std::make_unique<Network::UdpListenSocket>(peer_addr_, /*options=*/nullptr,
                                                                /*bind=*/true)),
        crypto_config_(std::make_shared<quic::QuicCryptoClientConfig>(
            quic::test::crypto_test_utils::ProofVerifierForTesting())),
        quic_stat_names_(store_.symbolTable()),
        transport_socket_options_(std::make_shared<Network::TransportSocketOptionsImpl>()),
        stats_({ALL_HTTP3_CODEC_STATS(POOL_COUNTER_PREFIX(store_, "http3."),
                                      POOL_GAUGE_PREFIX(store_, "http3."))}),
        quiche_handles_migration_(std::get<1>(GetParam())) {
    // After binding the listen peer socket, set the bound IP address of the peer.
    peer_addr_ = peer_socket_->connectionInfoProvider().localAddress();
    http3_options_.mutable_quic_protocol_options()
        ->mutable_num_timeouts_to_trigger_port_migration()
        ->set_value(1);
    if (quiche_handles_migration_) {
      migration_config_.allow_port_migration = true;
    }
  }

  void SetUp() override {
    quic::QuicForceBlockablePacketWriter* wrapper = nullptr;
    if (quiche_handles_migration_) {
      wrapper = new quic::QuicForceBlockablePacketWriter();
      // Owns the inner writer.
      wrapper->set_writer(writer_);
    }
    quic_connection_ = new TestEnvoyQuicClientConnection(
        quic::test::TestConnectionId(), connection_helper_, alarm_factory_,
        (quiche_handles_migration_ ? wrapper : static_cast<quic::QuicPacketWriter*>(writer_)),
        quic_version_, *dispatcher_, createConnectionSocket(peer_addr_, self_addr_, nullptr),
        connection_id_generator_);
    EnvoyQuicClientConnection::EnvoyQuicMigrationHelper* migration_helper = nullptr;
    if (!quiche_handles_migration_) {
      quic_connection_->setWriterFactory(writer_factory_);
    } else {
      migration_helper = &quic_connection_->getOrCreateMigrationHelper(writer_factory_, {});
    }

    OptRef<Http::HttpServerPropertiesCache> cache;
    OptRef<Network::UpstreamTransportSocketFactory> uts_factory;
    envoy_quic_session_ = std::make_unique<EnvoyQuicClientSession>(
        quic_config_, quic_version_,
        std::unique_ptr<TestEnvoyQuicClientConnection>(quic_connection_), wrapper, migration_helper,
        migration_config_, quic::QuicServerId("example.com", 443), crypto_config_, *dispatcher_,
        /*send_buffer_limit*/ 1024 * 1024, crypto_stream_factory_, quic_stat_names_, cache,
        *store_.rootScope(), transport_socket_options_, uts_factory);

    http_connection_ = std::make_unique<QuicHttpClientConnectionImpl>(
        *envoy_quic_session_, http_connection_callbacks_, stats_, http3_options_, 64 * 1024, 100);
    EXPECT_EQ(time_system_.systemTime(), envoy_quic_session_->streamInfo().startTime());
    EXPECT_EQ(EMPTY_STRING, envoy_quic_session_->nextProtocol());
    EXPECT_EQ(Http::Protocol::Http3, http_connection_->protocol());

    time_system_.advanceTimeWait(std::chrono::milliseconds(1));
    ON_CALL(*writer_, WritePacket(_, _, _, _, _, _))
        .WillByDefault(testing::Return(quic::WriteResult(quic::WRITE_STATUS_OK, 1)));

    envoy_quic_session_->Initialize();
    setQuicConfigWithDefaultValues(envoy_quic_session_->config());
    quic::test::QuicConfigPeer::SetReceivedStatelessResetToken(
        envoy_quic_session_->config(),
        quic::QuicUtils::GenerateStatelessResetToken(quic::test::TestConnectionId()));
    envoy_quic_session_->OnConfigNegotiated();
    envoy_quic_session_->addConnectionCallbacks(network_connection_callbacks_);
    envoy_quic_session_->setConnectionStats(
        {read_total_, read_current_, write_total_, write_current_, nullptr, nullptr});
    EXPECT_EQ(&read_total_, &quic_connection_->connectionStats().read_total_);
  }

  void TearDown() override {
    if (quic_connection_->connected()) {
      EXPECT_CALL(*quic_connection_,
                  SendConnectionClosePacket(quic::QUIC_NO_ERROR, _, "Closed by application"));
      EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
      envoy_quic_session_->close(Network::ConnectionCloseType::NoFlush);
    }
    peer_socket_->close();
  }

  EnvoyQuicClientStream& sendGetRequest(Http::ResponseDecoder& response_decoder,
                                        Http::StreamCallbacks& stream_callbacks) {
    auto& stream =
        dynamic_cast<EnvoyQuicClientStream&>(http_connection_->newStream(response_decoder));
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
  testing::NiceMock<quic::test::MockPacketWriter>* writer_;
  // Initialized with port 0 and modified during peer_socket_ creation.
  Network::Address::InstanceConstSharedPtr peer_addr_;
  Network::Address::InstanceConstSharedPtr self_addr_;
  // Used in some tests to trigger a read event on the connection to test its full interaction with
  // socket read utility functions.
  Network::UdpListenSocketPtr peer_socket_;
  quic::DeterministicConnectionIdGenerator connection_id_generator_{
      quic::kQuicDefaultConnectionIdLength};
  TestEnvoyQuicClientConnection* quic_connection_;
  quic::QuicConfig quic_config_;
  std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config_;
  TestQuicCryptoClientStreamFactory crypto_stream_factory_;
  Stats::IsolatedStoreImpl store_;
  QuicStatNames quic_stat_names_;
  Network::TransportSocketOptionsConstSharedPtr transport_socket_options_;
  std::unique_ptr<EnvoyQuicClientSession> envoy_quic_session_;
  Network::MockConnectionCallbacks network_connection_callbacks_;
  Http::MockServerConnectionCallbacks http_connection_callbacks_;
  testing::StrictMock<Stats::MockCounter> read_total_;
  testing::StrictMock<Stats::MockGauge> read_current_;
  testing::StrictMock<Stats::MockCounter> write_total_;
  testing::StrictMock<Stats::MockGauge> write_current_;
  Http::Http3::CodecStats stats_;
  envoy::config::core::v3::Http3ProtocolOptions http3_options_;
  std::unique_ptr<QuicHttpClientConnectionImpl> http_connection_;
  bool quiche_handles_migration_;
  quic::QuicConnectionMigrationConfig migration_config_{quicConnectionMigrationDisableAllConfig()};
  QuicClientPacketWriterFactoryImpl writer_factory_;
};

INSTANTIATE_TEST_SUITE_P(EnvoyQuicClientSessionTests, EnvoyQuicClientSessionTest,
                         testing::Combine(testing::ValuesIn(quic::CurrentSupportedHttp3Versions()),
                                          testing::Bool()));

TEST_P(EnvoyQuicClientSessionTest, ShutdownNoOp) { http_connection_->shutdownNotice(); }

INSTANTIATE_TEST_SUITE_P(EnvoyQuicClientSessionTest, EnvoyQuicClientSessionTest,
                         testing::Combine(testing::ValuesIn(quic::CurrentSupportedHttp3Versions()),
                                          testing::Bool()));

TEST_P(EnvoyQuicClientSessionTest, NewStream) {
  Http::MockResponseDecoder response_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EnvoyQuicClientStream& stream = sendGetRequest(response_decoder, stream_callbacks);

  quic::QuicHeaderList headers;
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
  EXPECT_EQ(0, envoy_quic_session_->GetNumActiveStreams());
  EXPECT_EQ(16, envoy_quic_session_->numPacketsExpectedPerEventLoop());

  NiceMock<Http::MockResponseDecoder> response_decoder;
  NiceMock<Http::MockStreamCallbacks> stream_callbacks;
  EnvoyQuicClientStream& stream = sendGetRequest(response_decoder, stream_callbacks);

  quic::QuicHeaderList headers;
  headers.OnHeader(":status", "200");
  headers.OnHeaderBlockEnd(/*uncompressed_header_bytes=*/0, /*compressed_header_bytes=*/0);
  // Response headers should be propagated to decoder.
  EXPECT_CALL(response_decoder, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([](const Http::ResponseHeaderMapPtr& decoded_headers, bool) {
        EXPECT_EQ("200", decoded_headers->getStatusValue());
      }));
  stream.OnStreamHeaderList(/*fin=*/false, headers.uncompressed_header_bytes(), headers);
  // With one stream, still read 16 packets.
  EXPECT_EQ(1, envoy_quic_session_->GetNumActiveStreams());
  EXPECT_EQ(16, envoy_quic_session_->numPacketsExpectedPerEventLoop());

  EnvoyQuicClientStream& stream2 = sendGetRequest(response_decoder, stream_callbacks);
  EXPECT_CALL(response_decoder, decodeHeaders_(_, /*end_stream=*/false))
      .WillOnce(Invoke([](const Http::ResponseHeaderMapPtr& decoded_headers, bool) {
        EXPECT_EQ("200", decoded_headers->getStatusValue());
      }));
  stream2.OnStreamHeaderList(/*fin=*/false, headers.uncompressed_header_bytes(), headers);
  // With 2 streams, read 32 packets.
  EXPECT_EQ(2, envoy_quic_session_->GetNumActiveStreams());
  EXPECT_EQ(32, envoy_quic_session_->numPacketsExpectedPerEventLoop());

  EXPECT_CALL(*quic_connection_,
              SendConnectionClosePacket(quic::QUIC_NO_ERROR, _, "Closed by application"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  envoy_quic_session_->close(Network::ConnectionCloseType::NoFlush);
}

TEST_P(EnvoyQuicClientSessionTest, OnResetFrame) {
  Http::MockResponseDecoder response_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EnvoyQuicClientStream& stream = sendGetRequest(response_decoder, stream_callbacks);

  // G-QUIC or IETF bi-directional stream.
  quic::QuicStreamId stream_id = stream.id();
  quic::QuicRstStreamFrame rst1(/*control_frame_id=*/1u, stream_id,
                                quic::QUIC_ERROR_PROCESSING_STREAM, /*bytes_written=*/0u);
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ProtocolError, _));
  envoy_quic_session_->OnRstStream(rst1);

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
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ProtocolError, _));
  EXPECT_CALL(*quic_connection_, SendControlFrame(_));
  envoy_quic_session_->ResetStream(stream_id, quic::QUIC_ERROR_PROCESSING_STREAM);

  EXPECT_EQ(
      1U, TestUtility::findCounter(
              store_, "http3.upstream.tx.quic_reset_stream_error_code_QUIC_ERROR_PROCESSING_STREAM")
              ->value());
}

TEST_P(EnvoyQuicClientSessionTest, OnGoAwayFrame) {
  Http::MockResponseDecoder response_decoder;
  Http::MockStreamCallbacks stream_callbacks;

  EXPECT_CALL(http_connection_callbacks_, onGoAway(Http::GoAwayErrorCode::NoError));
  envoy_quic_session_->OnHttp3GoAway(4u);
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
            envoy_quic_session_->transportFailureReason());
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_->state());

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
  envoy_quic_session_->close(Network::ConnectionCloseType::NoFlush);
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_->state());
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
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::LocalConnectionFailure, _));
  envoy_quic_session_->OnStreamError(quic::QUIC_HANDSHAKE_FAILED, "fake handshake time out");
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_->state());
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
            envoy_quic_session_->transportFailureReason());
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_->state());
  std::string quic_version_stat_name;
  switch (quic_version_[0].transport_version) {
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
  envoy_quic_session_->OnStreamFrame(stream_frame);
  EXPECT_FALSE(quic::test::QuicSessionPeer::IsStreamCreated(envoy_quic_session_.get(), stream_id));
  // IETF stream 3 is server initiated uni-directional stream.
  stream_id = 3u;
  auto payload = std::make_unique<char[]>(8);
  quic::QuicDataWriter payload_writer(8, payload.get());
  EXPECT_TRUE(payload_writer.WriteVarInt62(1ul));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(*quic_connection_, SendConnectionClosePacket(quic::QUIC_HTTP_RECEIVE_SERVER_PUSH, _,
                                                           "Received server push stream"));
  quic::QuicStreamFrame stream_frame2(stream_id, false, 0, absl::string_view(payload.get(), 1));
  envoy_quic_session_->OnStreamFrame(stream_frame2);
}

TEST_P(EnvoyQuicClientSessionTest, GetRttAndCwnd) {
  EXPECT_GT(envoy_quic_session_->lastRoundTripTime().value(), std::chrono::microseconds(0));
  // Just make sure the CWND is non-zero. We don't want to make strong assertions on what the value
  // should be in this test, that is the job the congestion controllers' tests.
  EXPECT_GT(envoy_quic_session_->congestionWindowInBytes().value(), 500);

  envoy_quic_session_->configureInitialCongestionWindow(8000000,
                                                        std::chrono::microseconds(1000000));
  EXPECT_GT(envoy_quic_session_->congestionWindowInBytes().value(),
            quic::kInitialCongestionWindow * quic::kDefaultTCPMSS);
}

TEST_P(EnvoyQuicClientSessionTest, VerifyContext) {
  auto& verify_context =
      dynamic_cast<EnvoyQuicProofVerifyContext&>(crypto_stream_factory_.lastVerifyContext().ref());
  EXPECT_FALSE(verify_context.isServer());
  EXPECT_EQ(transport_socket_options_.get(), verify_context.transportSocketOptions().get());
  EXPECT_EQ(dispatcher_.get(), &verify_context.dispatcher());
  EXPECT_EQ(peer_addr_->asString(), verify_context.extraValidationContext()
                                        .callbacks->connection()
                                        .connectionInfoSetter()
                                        .remoteAddress()
                                        ->asString());
  EXPECT_TRUE(verify_context.extraValidationContext().callbacks->ioHandle().isOpen());
}

TEST_P(EnvoyQuicClientSessionTest, VerifyContextAbortOnRaiseEvent) {
  auto& verify_context =
      dynamic_cast<EnvoyQuicProofVerifyContext&>(crypto_stream_factory_.lastVerifyContext().ref());
  EXPECT_DEATH(verify_context.extraValidationContext().callbacks->raiseEvent(
                   Network::ConnectionEvent::Connected),
               "unexpectedly reached");
}

TEST_P(EnvoyQuicClientSessionTest, VerifyContextAbortOnShouldDrainReadBuffer) {
  auto& verify_context =
      dynamic_cast<EnvoyQuicProofVerifyContext&>(crypto_stream_factory_.lastVerifyContext().ref());
  EXPECT_DEATH(verify_context.extraValidationContext().callbacks->shouldDrainReadBuffer(),
               "unexpectedly reached");
}

TEST_P(EnvoyQuicClientSessionTest, VerifyContextAbortOnSetTransportSocketIsReadable) {
  auto& verify_context =
      dynamic_cast<EnvoyQuicProofVerifyContext&>(crypto_stream_factory_.lastVerifyContext().ref());
  EXPECT_DEATH(verify_context.extraValidationContext().callbacks->setTransportSocketIsReadable(),
               "unexpectedly reached");
}

TEST_P(EnvoyQuicClientSessionTest, VerifyContextAbortOnFlushWriteBuffer) {
  auto& verify_context =
      dynamic_cast<EnvoyQuicProofVerifyContext&>(crypto_stream_factory_.lastVerifyContext().ref());
  EXPECT_DEATH(verify_context.extraValidationContext().callbacks->flushWriteBuffer(),
               "unexpectedly reached");
}

TEST_P(EnvoyQuicClientSessionTest, HandlePacketsWithoutDestinationAddress) {
  // Build a STATELESS_RESET packet.
  std::unique_ptr<quic::QuicEncryptedPacket> stateless_reset_packet =
      quic::QuicFramer::BuildIetfStatelessResetPacket(
          quic::test::TestConnectionId(), /*received_packet_length*/ 1200,
          quic::QuicUtils::GenerateStatelessResetToken(quic::test::TestConnectionId()));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose))
      .Times(0);
  for (size_t i = 0; i < 9; ++i) {
    auto buffer = std::make_unique<Buffer::OwnedImpl>(stateless_reset_packet->data(),
                                                      stateless_reset_packet->length());
    quic_connection_->processPacket(nullptr, peer_addr_, std::move(buffer),
                                    time_system_.monotonicTime(), /*tos=*/0, /*saved_cmsg=*/{});
  }
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose))
      .Times(0);
  auto buffer = std::make_unique<Buffer::OwnedImpl>(stateless_reset_packet->data(),
                                                    stateless_reset_packet->length());
  quic_connection_->processPacket(nullptr, peer_addr_, std::move(buffer),
                                  time_system_.monotonicTime(), /*tos=*/0, /*saved_cmsg=*/{});
}

// Tests that receiving a STATELESS_RESET packet on the probing socket doesn't cause crash.
TEST_P(EnvoyQuicClientSessionTest, StatelessResetOnProbingSocket) {
  quic::QuicNewConnectionIdFrame frame;
  frame.connection_id = quic::test::TestConnectionId(1234);
  ASSERT_NE(frame.connection_id, quic_connection_->connection_id());
  frame.stateless_reset_token = quic::QuicUtils::GenerateStatelessResetToken(frame.connection_id);
  frame.retire_prior_to = 0u;
  frame.sequence_number = 1u;
  quic_connection_->OnNewConnectionIdFrame(frame);
  quic_connection_->SetSelfAddress(envoyIpAddressToQuicSocketAddress(self_addr_->ip()));

  // Trigger port migration.
  quic_connection_->OnPathDegradingDetected();
  EXPECT_TRUE(envoy_quic_session_->HasPendingPathValidation());
  quic::QuicPathValidationContext* path_validation_context =
      quic_connection_->GetPathValidationContext();
  const Network::Address::InstanceConstSharedPtr new_self_address =
      quicAddressToEnvoyAddressInstance(path_validation_context->self_address());
  EXPECT_NE(new_self_address->asString(), self_addr_->asString());

  // Send a STATELESS_RESET packet to the probing socket.
  std::unique_ptr<quic::QuicEncryptedPacket> stateless_reset_packet =
      quic::QuicFramer::BuildIetfStatelessResetPacket(
          frame.connection_id, /*received_packet_length*/ 1200,
          quic::QuicUtils::GenerateStatelessResetToken(quic::test::TestConnectionId()));
  Buffer::RawSlice slice;
  slice.mem_ = const_cast<char*>(stateless_reset_packet->data());
  slice.len_ = stateless_reset_packet->length();
  peer_socket_->ioHandle().sendmsg(&slice, 1, 0, peer_addr_->ip(), *new_self_address);

  // Receiving the STATELESS_RESET on the probing socket shouldn't close the connection but should
  // fail the probing.
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::RemoteClose))
      .Times(0);
  while (envoy_quic_session_->HasPendingPathValidation()) {
    // Running event loop to receive the STATELESS_RESET and following socket reads shouldn't cause
    // crash.
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(self_addr_->asString(), quic_connection_->self_address().ToString());
}

TEST_P(EnvoyQuicClientSessionTest, EcnReportingIsEnabled) {
  const Network::ConnectionSocketPtr& socket = quic_connection_->connectionSocket();
  absl::optional<Network::Address::IpVersion> version = socket->ipVersion();
  EXPECT_TRUE(version.has_value());
  int optval;
  socklen_t optlen = sizeof(optval);
  Api::SysCallIntResult rv;
  if (*version == Network::Address::IpVersion::v6) {
    rv = socket->getSocketOption(IPPROTO_IPV6, IPV6_RECVTCLASS, &optval, &optlen);
  } else {
    rv = socket->getSocketOption(IPPROTO_IP, IP_RECVTOS, &optval, &optlen);
  }
  EXPECT_EQ(rv.return_value_, 0);
  EXPECT_EQ(optval, 1);
}

TEST_P(EnvoyQuicClientSessionTest, EcnReporting) {
  absl::optional<Network::Address::IpVersion> version = peer_socket_->ipVersion();
  EXPECT_TRUE(version.has_value());
  // Make the peer socket send ECN marks
  Api::SysCallIntResult rv;
  int optval = 1; // Code point for ECT(1) in RFC3168.
  if (*version == Network::Address::IpVersion::v6) {
    rv = peer_socket_->setSocketOption(IPPROTO_IPV6, IPV6_TCLASS, &optval, sizeof(optval));
  } else {
    rv = peer_socket_->setSocketOption(IPPROTO_IP, IP_TOS, &optval, sizeof(optval));
  }
  EXPECT_EQ(rv.return_value_, 0);
  // This test uses ConstructEncryptedPacket() to build an ECN-marked server
  // response. Unfortunately, that function uses the packet's destination
  // connection ID regardless of whether it is a client or server packet. By
  // setting the connection IDs to be the same, the keys will be correct.
  quic_connection_->set_client_connection_id(quic_connection_->connection_id());
  envoy_quic_session_->connect();
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // The first six bytes of a server hello, which is enough for the client to
  // process the packet successfully.
  char server_hello[] = {
      0x02, 0x00, 0x00, 0x56, 0x03, 0x03,
  };
  auto packet = absl::WrapUnique<quic::QuicEncryptedPacket>(quic::test::ConstructEncryptedPacket(
      quic_connection_->client_connection_id(), quic_connection_->connection_id(),
      /*version_flag=*/true, /*reset_flag=*/false, /*packet_number=*/1,
      std::string(server_hello, sizeof(server_hello)), /*full_padding=*/false,
      quic::CONNECTION_ID_PRESENT, quic::CONNECTION_ID_PRESENT, quic::PACKET_1BYTE_PACKET_NUMBER,
      &quic_version_, quic::Perspective::IS_SERVER));

  Buffer::RawSlice slice;
  // packet->data() is const, so it has to be copied to send to sendmsg.
  char buffer[100];
  memcpy(buffer, packet->data(), packet->length());
  slice.mem_ = buffer;
  slice.len_ = packet->length();
  quic::CrypterPair crypters;
  quic::CryptoUtils::CreateInitialObfuscators(quic::Perspective::IS_CLIENT, quic_version_[0],
                                              quic_connection_->connection_id(), &crypters);
  quic_connection_->InstallDecrypter(quic::ENCRYPTION_INITIAL, std::move(crypters.decrypter));

  peer_socket_->ioHandle().sendmsg(&slice, 1, 0, peer_addr_->ip(), *self_addr_);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  const quic::QuicConnectionStats& stats = quic_connection_->GetStats();
  if (stats.packets_received > 0) {
    // Due to flakiness, the endpoint might not receive any packets, in which case none will be
    // received with ECN marks.
    EXPECT_GT(stats.num_ecn_marks_received.ect1, 0);
  }
}

TEST_P(EnvoyQuicClientSessionTest, UseSocketAddressCache) {
  envoy_quic_session_->connect();
  // The first six bytes of a server hello, which is enough for the client to
  // process the packet successfully.
  char server_hello[] = {
      0x02, 0x00, 0x00, 0x56, 0x03, 0x03,
  };
  auto packet = absl::WrapUnique<quic::QuicEncryptedPacket>(quic::test::ConstructEncryptedPacket(
      quic_connection_->client_connection_id(), quic_connection_->connection_id(),
      /*version_flag=*/true, /*reset_flag=*/false, /*packet_number=*/1,
      std::string(server_hello, sizeof(server_hello)), /*full_padding=*/false,
      quic::CONNECTION_ID_PRESENT, quic::CONNECTION_ID_PRESENT, quic::PACKET_1BYTE_PACKET_NUMBER,
      &quic_version_, quic::Perspective::IS_SERVER));

  Buffer::RawSlice slice;
  slice.mem_ = const_cast<char*>(packet->data());
  slice.len_ = packet->length();

  // Send the same packet twice and ensure both times processPacket() gets the same address
  // instances.
  peer_socket_->ioHandle().sendmsg(&slice, 1, 0, peer_addr_->ip(), *self_addr_);
  while (quic_connection_->packetsReceived() < 1) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  Network::Address::InstanceConstSharedPtr last_peer_address =
      quic_connection_->getLastPeerAddress();
  Network::Address::InstanceConstSharedPtr last_local_address =
      quic_connection_->getLastLocalAddress();

  peer_socket_->ioHandle().sendmsg(&slice, 1, 0, peer_addr_->ip(), *self_addr_);
  while (quic_connection_->packetsReceived() < 2) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(last_local_address.get(), quic_connection_->getLastLocalAddress().get());
  EXPECT_EQ(last_peer_address.get(), quic_connection_->getLastPeerAddress().get());
}

TEST_P(EnvoyQuicClientSessionTest, WriteBlockedAndUnblock) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> singleton_injector{&os_sys_calls};

  // Switch to a real writer, and synthesize a write block on it.
  quic_connection_->SetQuicPacketWriter(
      new EnvoyQuicPacketWriter(std::make_unique<Network::UdpDefaultWriter>(
          quic_connection_->connectionSocket()->ioHandle())),
      true);
  if (quic_connection_->connectionSocket()->ioHandle().wasConnected()) {
    EXPECT_CALL(os_sys_calls, send(_, _, _, _))
        .Times(2)
        .WillOnce(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_AGAIN}));
  } else {
    EXPECT_CALL(os_sys_calls, sendmsg(_, _, _))
        .Times(2)
        .WillOnce(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_AGAIN}));
  }
  // OnCanWrite() would close the connection if the underlying writer is not unblocked.
  EXPECT_CALL(*quic_connection_, SendConnectionClosePacket(quic::QUIC_INTERNAL_ERROR, _, _))
      .Times(0);
  Http::MockResponseDecoder response_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EnvoyQuicClientStream& stream = sendGetRequest(response_decoder, stream_callbacks);
  EXPECT_TRUE(quic_connection_->writer()->IsWriteBlocked());

  // Synthesize a WRITE event.
  EnvoyQuicClientConnectionPeer::onFileEvent(*quic_connection_, Event::FileReadyType::Write,
                                             *quic_connection_->connectionSocket());
  EXPECT_FALSE(quic_connection_->writer()->IsWriteBlocked());
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::LocalReset,
                                              "QUIC_STREAM_REQUEST_REJECTED|FROM_SELF"));
  EXPECT_CALL(*quic_connection_, SendControlFrame(_));
  stream.resetStream(Http::StreamResetReason::LocalReset);
}

TEST_P(EnvoyQuicClientSessionTest, DisableQpack) {
  envoy::config::core::v3::Http3ProtocolOptions http3_options;
  http3_options.set_disable_qpack(true);

  envoy_quic_session_->setHttp3Options(http3_options);

  EXPECT_EQ(envoy_quic_session_->qpack_maximum_dynamic_table_capacity(), 0);
}

class MockOsSysCallsImpl : public Api::OsSysCallsImpl {
public:
  MOCK_METHOD(Api::SysCallSizeResult, recvmsg, (os_fd_t socket, msghdr* msg, int flags),
              (override));
  MOCK_METHOD(Api::SysCallIntResult, recvmmsg,
              (os_fd_t socket, struct mmsghdr* msgvec, unsigned int vlen, int flags,
               struct timespec* timeout),
              (override));
  MOCK_METHOD(bool, supportsUdpGro, (), (const));
};

// Ensures that the Network::Utility::readFromSocket function uses GRO.
// Only Linux platforms support GRO.
TEST_P(EnvoyQuicClientSessionTest, UsesUdpGro) {
  if (!Api::OsSysCallsSingleton::get().supportsUdpGro()) {
    GTEST_SKIP() << "Platform doesn't support GRO.";
  }

  NiceMock<MockOsSysCallsImpl> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> singleton_injector{&os_sys_calls};

  // Have to connect the QUIC session, so that the socket is set up so we can do I/O on it.
  envoy_quic_session_->connect();

  std::string write_data = "abc";
  Buffer::RawSlice slice;
  slice.mem_ = write_data.data();
  slice.len_ = write_data.length();

  // We already skip the test if UDP GRO is not supported, so checking the socket option is safe
  // here.
  int sock_opt;
  socklen_t sock_len = sizeof(int);
  EXPECT_EQ(0, quic_connection_->connectionSocket()
                   ->getSocketOption(SOL_UDP, UDP_GRO, &sock_opt, &sock_len)
                   .return_value_);
  EXPECT_EQ(1, sock_opt);

  // GRO uses `recvmsg`, not `recvmmsg`.
  EXPECT_CALL(os_sys_calls, supportsUdpGro()).WillRepeatedly(Return(true));
  EXPECT_CALL(os_sys_calls, recvmmsg(_, _, _, _, _)).Times(0);
  EXPECT_CALL(os_sys_calls, recvmsg(_, _, _))
      .WillOnce(
          Invoke([&](os_fd_t /*socket*/, msghdr* /*msg*/, int /*flags*/) -> Api::SysCallSizeResult {
            dispatcher_->exit();
            // Return an error so IoSocketHandleImpl::recvmsg() exits early, instead of trying to
            // use the msghdr that would normally have been populated by recvmsg but is not
            // populated by this mock.
            return {-1, SOCKET_ERROR_AGAIN};
          }));

  peer_socket_->ioHandle().sendmsg(&slice, 1, 0, peer_addr_->ip(), *self_addr_);

  EXPECT_LOG_CONTAINS("trace", "starting gro recvmsg with max",
                      dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit));
}

TEST_P(EnvoyQuicClientSessionTest, SetSocketOption) {
  Network::SocketOptionName sockopt_name;
  int val = 1;
  absl::Span<uint8_t> sockopt_val(reinterpret_cast<uint8_t*>(&val), sizeof(val));

  EXPECT_FALSE(envoy_quic_session_->setSocketOption(sockopt_name, sockopt_val));
}

class EnvoyQuicClientSessionDisallowMmsgTest : public EnvoyQuicClientSessionTest {
public:
  EnvoyQuicClientSessionDisallowMmsgTest()
      : is_udp_gro_supported_on_platform_(Api::OsSysCallsSingleton::get().supportsUdpGro()),
        singleton_injector_(&os_sys_calls_) {}

  void SetUp() override {
    EXPECT_CALL(os_sys_calls_, supportsUdpGro()).WillRepeatedly(Return(false));
    EnvoyQuicClientSessionTest::SetUp();
  }

protected:
  NiceMock<MockOsSysCallsImpl> os_sys_calls_;
  const bool is_udp_gro_supported_on_platform_;

private:
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> singleton_injector_;
};

INSTANTIATE_TEST_SUITE_P(EnvoyQuicClientSessionDisallowMmsgTests,
                         EnvoyQuicClientSessionDisallowMmsgTest,
                         testing::Combine(testing::ValuesIn(quic::CurrentSupportedHttp3Versions()),
                                          testing::Bool()));

// Ensures that the Network::Utility::readFromSocket function uses `recvmsg` for client QUIC
// connections when GRO is not supported.
TEST_P(EnvoyQuicClientSessionDisallowMmsgTest, UsesRecvMsgWhenNoGro) {
  // Have to connect the QUIC session, so that the socket is set up so we can do I/O on it.
  envoy_quic_session_->connect();

  std::string write_data = "abc";
  Buffer::RawSlice slice;
  slice.mem_ = write_data.data();
  slice.len_ = write_data.length();

  if (is_udp_gro_supported_on_platform_) {
    // Make sure the option for GRO is *not* set on the socket. This check cannot be called if the
    // platform doesn't support the UDP_GRO socket option; otherwise getSocketOption() will return
    // an error with errno set to "Protocol not supported".
    int sock_opt;
    socklen_t sock_len = sizeof(int);
    EXPECT_EQ(0, quic_connection_->connectionSocket()
                     ->getSocketOption(SOL_UDP, UDP_GRO, &sock_opt, &sock_len)
                     .return_value_);
    EXPECT_EQ(0, sock_opt);
  }

  // Uses `recvmsg`, not `recvmmsg`.
  EXPECT_CALL(os_sys_calls_, recvmmsg(_, _, _, _, _)).Times(0);
  EXPECT_CALL(os_sys_calls_, recvmsg(_, _, _))
      .WillOnce(
          Invoke([&](os_fd_t /*socket*/, msghdr* /*msg*/, int /*flags*/) -> Api::SysCallSizeResult {
            dispatcher_->exit();
            // Return an error so IoSocketHandleImpl::recvmsg() exits early, instead of trying to
            // use the msghdr that would normally have been populated by recvmsg but is not
            // populated by this mock.
            return {-1, SOCKET_ERROR_AGAIN};
          }));

  peer_socket_->ioHandle().sendmsg(&slice, 1, 0, peer_addr_->ip(), *self_addr_);

  EXPECT_LOG_CONTAINS("trace", "starting recvmsg with max",
                      dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit));
}

class EnvoyQuicClientSessionAllowMmsgTest : public EnvoyQuicClientSessionTest {
public:
  void SetUp() override {
    EXPECT_CALL(os_sys_calls_, supportsUdpGro()).WillRepeatedly(Return(false));

    scoped_runtime_.mergeValues(
        {{"envoy.reloadable_features.disallow_quic_client_udp_mmsg", "false"}});
    EnvoyQuicClientSessionTest::SetUp();
  }

protected:
  NiceMock<MockOsSysCallsImpl> os_sys_calls_;

private:
  TestScopedRuntime scoped_runtime_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> singleton_injector_{&os_sys_calls_};
};

INSTANTIATE_TEST_SUITE_P(EnvoyQuicClientSessionAllowMmsgTests, EnvoyQuicClientSessionAllowMmsgTest,
                         testing::Combine(testing::ValuesIn(quic::CurrentSupportedHttp3Versions()),
                                          testing::Bool()));

TEST_P(EnvoyQuicClientSessionAllowMmsgTest, UsesRecvMmsgWhenNoGroAndMmsgAllowed) {
  if (!Api::OsSysCallsSingleton::get().supportsMmsg()) {
    GTEST_SKIP() << "Platform doesn't support recvmmsg.";
  }

  // Have to connect the QUIC session, so that the socket is set up so we can do I/O on it.
  envoy_quic_session_->connect();

  std::string write_data = "abc";
  Buffer::RawSlice slice;
  slice.mem_ = write_data.data();
  slice.len_ = write_data.length();

  // Make sure recvmmsg is used when GRO isn't supported.
  EXPECT_CALL(os_sys_calls_, supportsUdpGro()).WillRepeatedly(Return(false));
  EXPECT_CALL(os_sys_calls_, recvmsg(_, _, _)).Times(0);
  EXPECT_CALL(os_sys_calls_, recvmmsg(_, _, _, _, _))
      .WillRepeatedly(Invoke([&](os_fd_t, struct mmsghdr*, unsigned int, int,
                                 struct timespec*) -> Api::SysCallIntResult {
        dispatcher_->exit();
        // EAGAIN should be returned with -1 but returning 0 here shouldn't cause busy looping.
        return {0, SOCKET_ERROR_AGAIN};
      }));

  peer_socket_->ioHandle().sendmsg(&slice, 1, 0, peer_addr_->ip(), *self_addr_);

  EXPECT_LOG_CONTAINS("trace", "starting recvmmsg with packets",
                      dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit));
}

} // namespace Quic
} // namespace Envoy
