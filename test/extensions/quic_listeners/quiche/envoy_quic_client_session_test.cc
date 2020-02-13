#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/crypto/null_encrypter.h"
#include "quiche/quic/test_tools/crypto_test_utils.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

#pragma GCC diagnostic pop

#include "extensions/quic_listeners/quiche/envoy_quic_client_session.h"
#include "extensions/quic_listeners/quiche/envoy_quic_client_connection.h"
#include "extensions/quic_listeners/quiche/codec_impl.h"
#include "extensions/quic_listeners/quiche/envoy_quic_connection_helper.h"
#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"
#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"

#include "envoy/stats/stats_macros.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

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
                                Network::ConnectionSocketPtr&& connection_socket)
      : EnvoyQuicClientConnection(server_connection_id, helper, alarm_factory, &writer, false,
                                  supported_versions, dispatcher, std::move(connection_socket)) {
    SetDefaultEncryptionLevel(quic::ENCRYPTION_FORWARD_SECURE);
    SetEncrypter(quic::ENCRYPTION_FORWARD_SECURE,
                 std::make_unique<quic::NullEncrypter>(quic::Perspective::IS_CLIENT));
  }

  MOCK_METHOD(void, SendConnectionClosePacket, (quic::QuicErrorCode, const std::string&));
  MOCK_METHOD(bool, SendControlFrame, (const quic::QuicFrame& frame));

  using EnvoyQuicClientConnection::connectionStats;
};

class TestQuicCryptoClientStream : public quic::QuicCryptoClientStream {
public:
  TestQuicCryptoClientStream(const quic::QuicServerId& server_id, quic::QuicSession* session,
                             std::unique_ptr<quic::ProofVerifyContext> verify_context,
                             quic::QuicCryptoClientConfig* crypto_config,
                             ProofHandler* proof_handler)
      : quic::QuicCryptoClientStream(server_id, session, std::move(verify_context), crypto_config,
                                     proof_handler) {}

  bool encryption_established() const override { return true; }
};

class TestEnvoyQuicClientSession : public EnvoyQuicClientSession {
public:
  TestEnvoyQuicClientSession(const quic::QuicConfig& config,
                             const quic::ParsedQuicVersionVector& supported_versions,
                             std::unique_ptr<EnvoyQuicClientConnection> connection,
                             const quic::QuicServerId& server_id,
                             quic::QuicCryptoClientConfig* crypto_config,
                             quic::QuicClientPushPromiseIndex* push_promise_index,
                             Event::Dispatcher& dispatcher, uint32_t send_buffer_limit)
      : EnvoyQuicClientSession(config, supported_versions, std::move(connection), server_id,
                               crypto_config, push_promise_index, dispatcher, send_buffer_limit) {}

  std::unique_ptr<quic::QuicCryptoClientStreamBase> CreateQuicCryptoStream() override {
    return std::make_unique<TestQuicCryptoClientStream>(
        server_id(), this, crypto_config()->proof_verifier()->CreateDefaultContext(),
        crypto_config(), this);
  }
};

class EnvoyQuicClientSessionTest : public testing::TestWithParam<bool> {
public:
  EnvoyQuicClientSessionTest()
      : api_(Api::createApiForTest(time_system_)), dispatcher_(api_->allocateDispatcher()),
        connection_helper_(*dispatcher_),
        alarm_factory_(*dispatcher_, *connection_helper_.GetClock()), quic_version_([]() {
          SetQuicReloadableFlag(quic_enable_version_q099, GetParam());
          return quic::ParsedVersionOfIndex(quic::CurrentSupportedVersions(), 0);
        }()),
        peer_addr_(Network::Utility::getAddressWithPort(*Network::Utility::getIpv6LoopbackAddress(),
                                                        12345)),
        self_addr_(Network::Utility::getAddressWithPort(*Network::Utility::getIpv6LoopbackAddress(),
                                                        54321)),
        quic_connection_(new TestEnvoyQuicClientConnection(
            quic::test::TestConnectionId(), connection_helper_, alarm_factory_, writer_,
            quic_version_, *dispatcher_, createConnectionSocket(peer_addr_, self_addr_, nullptr))),
        crypto_config_(quic::test::crypto_test_utils::ProofVerifierForTesting()),
        envoy_quic_session_(quic_config_, quic_version_,
                            std::unique_ptr<TestEnvoyQuicClientConnection>(quic_connection_),
                            quic::QuicServerId("example.com", 443, false), &crypto_config_, nullptr,
                            *dispatcher_,
                            /*send_buffer_limit*/ 1024 * 1024),
        http_connection_(envoy_quic_session_, http_connection_callbacks_) {
    EXPECT_EQ(time_system_.systemTime(), envoy_quic_session_.streamInfo().startTime());
    EXPECT_EQ(EMPTY_STRING, envoy_quic_session_.nextProtocol());
    EXPECT_EQ(Http::Protocol::Http3, http_connection_.protocol());

    time_system_.sleep(std::chrono::milliseconds(1));
    ON_CALL(writer_, WritePacket(_, _, _, _, _))
        .WillByDefault(testing::Return(quic::WriteResult(quic::WRITE_STATUS_OK, 1)));
  }

  void SetUp() override {
    envoy_quic_session_.Initialize();
    envoy_quic_session_.addConnectionCallbacks(network_connection_callbacks_);
    envoy_quic_session_.setConnectionStats(
        {read_total_, read_current_, write_total_, write_current_, nullptr, nullptr});
    EXPECT_EQ(&read_total_, &quic_connection_->connectionStats().read_total_);
  }

  void TearDown() override {
    if (quic_connection_->connected()) {
      EXPECT_CALL(*quic_connection_,
                  SendConnectionClosePacket(quic::QUIC_NO_ERROR, "Closed by application"));
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
    stream.encodeHeaders(request_headers, true);
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
  TestEnvoyQuicClientConnection* quic_connection_;
  quic::QuicConfig quic_config_;
  quic::QuicCryptoClientConfig crypto_config_;
  TestEnvoyQuicClientSession envoy_quic_session_;
  Network::MockConnectionCallbacks network_connection_callbacks_;
  Http::MockServerConnectionCallbacks http_connection_callbacks_;
  testing::StrictMock<Stats::MockCounter> read_total_;
  testing::StrictMock<Stats::MockGauge> read_current_;
  testing::StrictMock<Stats::MockCounter> write_total_;
  testing::StrictMock<Stats::MockGauge> write_current_;
  QuicHttpClientConnectionImpl http_connection_;
};

INSTANTIATE_TEST_SUITE_P(EnvoyQuicClientSessionTests, EnvoyQuicClientSessionTest,
                         testing::ValuesIn({true, false}));

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
        EXPECT_EQ("200", decoded_headers->Status()->value().getStringView());
      }));
  stream.OnStreamHeaderList(/*fin=*/true, headers.uncompressed_header_bytes(), headers);
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
  stream.OnStreamReset(rst1);
}

TEST_P(EnvoyQuicClientSessionTest, ConnectionClose) {
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

TEST_P(EnvoyQuicClientSessionTest, ConnectionCloseWithActiveStream) {
  Http::MockResponseDecoder response_decoder;
  Http::MockStreamCallbacks stream_callbacks;
  EnvoyQuicClientStream& stream = sendGetRequest(response_decoder, stream_callbacks);
  EXPECT_CALL(*quic_connection_,
              SendConnectionClosePacket(quic::QUIC_NO_ERROR, "Closed by application"));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
  EXPECT_CALL(stream_callbacks, onResetStream(Http::StreamResetReason::ConnectionTermination, _));
  envoy_quic_session_.close(Network::ConnectionCloseType::NoFlush);
  EXPECT_EQ(Network::Connection::State::Closed, envoy_quic_session_.state());
  EXPECT_TRUE(stream.write_side_closed() && stream.reading_stopped());
}

} // namespace Quic
} // namespace Envoy
