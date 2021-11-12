#include <openssl/x509_vfy.h>

#include <cstddef>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"

#include "test/common/upstream/utility.h"
#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "quiche/quic/core/http/quic_client_push_promise_index.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/test_tools/quic_test_utils.h"
#include "quiche/quic/test_tools/quic_session_peer.h"
#include "quiche/quic/test_tools/quic_sent_packet_manager_peer.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "source/common/quic/active_quic_listener.h"
#include "source/common/quic/client_connection_factory_impl.h"
#include "source/common/quic/envoy_quic_client_session.h"
#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/quic/envoy_quic_connection_helper.h"
#include "source/common/quic/envoy_quic_alarm_factory.h"
#include "source/common/quic/envoy_quic_packet_writer.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_transport_socket_factory.h"
#include "test/common/quic/test_utils.h"
#include "test/config/integration/certs/clientcert_hash.h"
#include "source/extensions/transport_sockets/tls/context_config_impl.h"

#if defined(ENVOY_CONFIG_COVERAGE)
#define DISABLE_UNDER_COVERAGE return
#else
#define DISABLE_UNDER_COVERAGE                                                                     \
  do {                                                                                             \
  } while (0)
#endif

namespace Envoy {
namespace Quic {

class CodecClientCallbacksForTest : public Http::CodecClientCallbacks {
public:
  void onStreamDestroy() override {}

  void onStreamReset(Http::StreamResetReason reason) override {
    last_stream_reset_reason_ = reason;
  }

  Http::StreamResetReason last_stream_reset_reason_{Http::StreamResetReason::LocalReset};
};

// This class enables testing on QUIC path validation
class TestEnvoyQuicClientConnection : public EnvoyQuicClientConnection {
public:
  TestEnvoyQuicClientConnection(const quic::QuicConnectionId& server_connection_id,
                                Network::Address::InstanceConstSharedPtr& initial_peer_address,
                                quic::QuicConnectionHelperInterface& helper,
                                quic::QuicAlarmFactory& alarm_factory,
                                const quic::ParsedQuicVersionVector& supported_versions,
                                Network::Address::InstanceConstSharedPtr local_addr,
                                Event::Dispatcher& dispatcher,
                                const Network::ConnectionSocket::OptionsSharedPtr& options,
                                bool validation_failure_on_path_response)
      : EnvoyQuicClientConnection(server_connection_id, initial_peer_address, helper, alarm_factory,
                                  supported_versions, local_addr, dispatcher, options),
        dispatcher_(dispatcher),
        validation_failure_on_path_response_(validation_failure_on_path_response) {}

  AssertionResult
  waitForPathResponse(std::chrono::milliseconds timeout = TestUtility::DefaultTimeout) {
    bool timer_fired = false;
    if (!saw_path_response_) {
      Event::TimerPtr timer(dispatcher_.createTimer([this, &timer_fired]() -> void {
        timer_fired = true;
        dispatcher_.exit();
      }));
      timer->enableTimer(timeout);
      waiting_for_path_response_ = true;
      dispatcher_.run(Event::Dispatcher::RunType::Block);
      if (timer_fired) {
        return AssertionFailure() << "Timed out waiting for path response\n";
      }
    }
    return AssertionSuccess();
  }

  bool OnPathResponseFrame(const quic::QuicPathResponseFrame& frame) override {
    saw_path_response_ = true;
    if (waiting_for_path_response_) {
      dispatcher_.exit();
    }
    if (!validation_failure_on_path_response_) {
      return EnvoyQuicClientConnection::OnPathResponseFrame(frame);
    }
    CancelPathValidation();
    return connected();
  }

  AssertionResult
  waitForHandshakeDone(std::chrono::milliseconds timeout = TestUtility::DefaultTimeout) {
    bool timer_fired = false;
    if (!saw_handshake_done_) {
      Event::TimerPtr timer(dispatcher_.createTimer([this, &timer_fired]() -> void {
        timer_fired = true;
        dispatcher_.exit();
      }));
      timer->enableTimer(timeout);
      waiting_for_handshake_done_ = true;
      dispatcher_.run(Event::Dispatcher::RunType::Block);
      if (timer_fired) {
        return AssertionFailure() << "Timed out waiting for handshake done\n";
      }
    }
    return AssertionSuccess();
  }

  bool OnHandshakeDoneFrame(const quic::QuicHandshakeDoneFrame& frame) override {
    saw_handshake_done_ = true;
    if (waiting_for_handshake_done_) {
      dispatcher_.exit();
    }
    return EnvoyQuicClientConnection::OnHandshakeDoneFrame(frame);
  }

private:
  Event::Dispatcher& dispatcher_;
  bool saw_path_response_{false};
  bool saw_handshake_done_{false};
  bool waiting_for_path_response_{false};
  bool waiting_for_handshake_done_{false};
  bool validation_failure_on_path_response_{false};
};

// A test that sets up its own client connection with customized quic version and connection ID.
class QuicHttpIntegrationTest : public HttpIntegrationTest,
                                public testing::TestWithParam<Network::Address::IpVersion> {
public:
  QuicHttpIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP3, GetParam(),
                            ConfigHelper::quicHttpProxyConfig()),
        supported_versions_(quic::CurrentSupportedHttp3Versions()), conn_helper_(*dispatcher_),
        alarm_factory_(*dispatcher_, *conn_helper_.GetClock()) {
    SetQuicReloadableFlag(quic_remove_connection_migration_connection_option, true);
  }

  ~QuicHttpIntegrationTest() override {
    cleanupUpstreamAndDownstream();
    // Release the client before destroying |conn_helper_|. No such need once |conn_helper_| is
    // moved into a client connection factory in the base test class.
    codec_client_.reset();
  }

  Network::ClientConnectionPtr makeClientConnectionWithOptions(
      uint32_t port, const Network::ConnectionSocket::OptionsSharedPtr& options) override {
    // Setting socket options is not supported.
    ASSERT(!options);
    return makeClientConnectionWithHost(port, "");
  }

  Network::ClientConnectionPtr makeClientConnectionWithHost(uint32_t port,
                                                            const std::string& host) {
    // Setting socket options is not supported.
    server_addr_ = Network::Utility::resolveUrl(
        fmt::format("udp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));
    Network::Address::InstanceConstSharedPtr local_addr =
        Network::Test::getCanonicalLoopbackAddress(version_);
    // Initiate a QUIC connection with the highest supported version. If not
    // supported by server, this connection will fail.
    // TODO(danzh) Implement retry upon version mismatch and modify test frame work to specify a
    // different version set on server side to test that.
    auto connection = std::make_unique<TestEnvoyQuicClientConnection>(
        getNextConnectionId(), server_addr_, conn_helper_, alarm_factory_,
        quic::ParsedQuicVersionVector{supported_versions_[0]}, local_addr, *dispatcher_, nullptr,
        validation_failure_on_path_response_);
    quic_connection_ = connection.get();
    ASSERT(quic_connection_persistent_info_ != nullptr);
    auto& persistent_info = static_cast<PersistentQuicInfoImpl&>(*quic_connection_persistent_info_);
    auto session = std::make_unique<EnvoyQuicClientSession>(
        persistent_info.quic_config_, supported_versions_, std::move(connection),
        (host.empty() ? persistent_info.server_id_
                      : quic::QuicServerId{host, static_cast<uint16_t>(port), false}),
        persistent_info.cryptoConfig(), &push_promise_index_, *dispatcher_,
        // Use smaller window than the default one to have test coverage of client codec buffer
        // exceeding high watermark.
        /*send_buffer_limit=*/2 * Http2::Utility::OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE,
        persistent_info.crypto_stream_factory_, quic_stat_names_, stats_store_);
    return session;
  }

  IntegrationCodecClientPtr makeRawHttpConnection(
      Network::ClientConnectionPtr&& conn,
      absl::optional<envoy::config::core::v3::Http2ProtocolOptions> http2_options) override {
    return makeRawHttp3Connection(std::move(conn), http2_options, true);
  }

  // Create Http3 codec client with the option not to wait for 1-RTT key establishment.
  IntegrationCodecClientPtr makeRawHttp3Connection(
      Network::ClientConnectionPtr&& conn,
      absl::optional<envoy::config::core::v3::Http2ProtocolOptions> http2_options,
      bool wait_for_1rtt_key) {
    std::shared_ptr<Upstream::MockClusterInfo> cluster{new NiceMock<Upstream::MockClusterInfo>()};
    cluster->max_response_headers_count_ = 200;
    if (http2_options.has_value()) {
      cluster->http3_options_ = ConfigHelper::http2ToHttp3ProtocolOptions(
          http2_options.value(), quic::kStreamReceiveWindowLimit);
    }
    cluster->http3_options_.set_allow_extended_connect(true);
    *cluster->http3_options_.mutable_quic_protocol_options() = client_quic_options_;
    Upstream::HostDescriptionConstSharedPtr host_description{Upstream::makeTestHostDescription(
        cluster, fmt::format("tcp://{}:80", Network::Test::getLoopbackAddressUrlString(version_)),
        timeSystem())};
    // This call may fail in QUICHE because of INVALID_VERSION. QUIC connection doesn't support
    // in-connection version negotiation.
    auto codec = std::make_unique<IntegrationCodecClient>(*dispatcher_, random_, std::move(conn),
                                                          host_description, downstream_protocol_,
                                                          wait_for_1rtt_key);
    if (codec->disconnected()) {
      // Connection may get closed during version negotiation or handshake.
      // TODO(#8479) QUIC connection doesn't support in-connection version negotiationPropagate
      // INVALID_VERSION error to caller and let caller to use server advertised version list to
      // create a new connection with mutually supported version and make client codec again.
      ENVOY_LOG(error, "Fail to connect to server with error: {}",
                codec->connection()->transportFailureReason());
      return codec;
    }
    codec->setCodecClientCallbacks(client_codec_callback_);
    return codec;
  }

  quic::QuicConnectionId getNextConnectionId() {
    if (designated_connection_ids_.empty()) {
      return quic::QuicUtils::CreateRandomConnectionId();
    }
    quic::QuicConnectionId cid = designated_connection_ids_.front();
    designated_connection_ids_.pop_front();
    return cid;
  }

  void initialize() override {
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          hcm.mutable_drain_timeout()->clear_seconds();
          hcm.mutable_drain_timeout()->set_nanos(500 * 1000 * 1000);
          EXPECT_EQ(hcm.codec_type(), envoy::extensions::filters::network::http_connection_manager::
                                          v3::HttpConnectionManager::HTTP3);
        });

    HttpIntegrationTest::initialize();
    // Latch quic_transport_socket_factory_ which is instantiated in initialize().
    transport_socket_factory_ =
        static_cast<QuicClientTransportSocketFactory*>(quic_transport_socket_factory_.get());
    registerTestServerPorts({"http"});

    ASSERT(&transport_socket_factory_->clientContextConfig());
  }

  void testMultipleQuicConnections() {
    concurrency_ = 8;
    initialize();
    std::vector<IntegrationCodecClientPtr> codec_clients;
    for (size_t i = 1; i <= concurrency_; ++i) {
      // The BPF filter and ActiveQuicListener::destination() look at the 1st word of connection id
      // in the packet header. And currently all QUIC versions support >= 8 bytes connection id. So
      // create connections with the first 4 bytes of connection id different from each
      // other so they should be evenly distributed.
      designated_connection_ids_.push_back(quic::test::TestConnectionId(i << 32));
      // TODO(sunjayBhatia,wrowe): deserialize this, establishing all connections in parallel
      // (Expected to save ~14s each across 6 tests on Windows)
      codec_clients.push_back(makeHttpConnection(lookupPort("http")));
    }
    constexpr auto timeout_first = std::chrono::seconds(15);
    constexpr auto timeout_subsequent = std::chrono::milliseconds(10);
    if (GetParam() == Network::Address::IpVersion::v4) {
      test_server_->waitForCounterEq("listener.127.0.0.1_0.downstream_cx_total", 8u, timeout_first);
    } else {
      test_server_->waitForCounterEq("listener.[__1]_0.downstream_cx_total", 8u, timeout_first);
    }
    for (size_t i = 0; i < concurrency_; ++i) {
      if (GetParam() == Network::Address::IpVersion::v4) {
        test_server_->waitForGaugeEq(
            fmt::format("listener.127.0.0.1_0.worker_{}.downstream_cx_active", i), 1u,
            timeout_subsequent);
        test_server_->waitForCounterEq(
            fmt::format("listener.127.0.0.1_0.worker_{}.downstream_cx_total", i), 1u,
            timeout_subsequent);
      } else {
        test_server_->waitForGaugeEq(
            fmt::format("listener.[__1]_0.worker_{}.downstream_cx_active", i), 1u,
            timeout_subsequent);
        test_server_->waitForCounterEq(
            fmt::format("listener.[__1]_0.worker_{}.downstream_cx_total", i), 1u,
            timeout_subsequent);
      }
    }
    for (size_t i = 0; i < concurrency_; ++i) {
      fake_upstream_connection_ = nullptr;
      upstream_request_ = nullptr;
      auto encoder_decoder =
          codec_clients[i]->startRequest(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                        {":path", "/test/long/url"},
                                                                        {":scheme", "http"},
                                                                        {":authority", "host"}});
      auto& request_encoder = encoder_decoder.first;
      auto response = std::move(encoder_decoder.second);
      codec_clients[i]->sendData(request_encoder, 0, true);
      waitForNextUpstreamRequest();
      upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                                       {"set-cookie", "foo"},
                                                                       {"set-cookie", "bar"}},
                                       true);

      ASSERT_TRUE(response->waitForEndStream());
      EXPECT_TRUE(response->complete());
      codec_clients[i]->close();
    }
  }

protected:
  quic::QuicClientPushPromiseIndex push_promise_index_;
  quic::ParsedQuicVersionVector supported_versions_;
  EnvoyQuicConnectionHelper conn_helper_;
  EnvoyQuicAlarmFactory alarm_factory_;
  CodecClientCallbacksForTest client_codec_callback_;
  Network::Address::InstanceConstSharedPtr server_addr_;
  envoy::config::core::v3::QuicProtocolOptions client_quic_options_;
  TestEnvoyQuicClientConnection* quic_connection_{nullptr};
  std::list<quic::QuicConnectionId> designated_connection_ids_;
  Quic::QuicClientTransportSocketFactory* transport_socket_factory_{nullptr};
  bool validation_failure_on_path_response_{false};
};

INSTANTIATE_TEST_SUITE_P(QuicHttpIntegrationTests, QuicHttpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(QuicHttpIntegrationTest, GetRequestAndEmptyResponse) {
  testRouterHeaderOnlyRequestAndResponse();
}

TEST_P(QuicHttpIntegrationTest, Draft29NotSupportedByDefault) {
  supported_versions_ = {quic::ParsedQuicVersion::Draft29()};
  initialize();
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  EXPECT_TRUE(codec_client_->disconnected());
  EXPECT_EQ(quic::QUIC_INVALID_VERSION,
            static_cast<EnvoyQuicClientSession*>(codec_client_->connection())->error());
}

TEST_P(QuicHttpIntegrationTest, RuntimeEnableDraft29) {
  supported_versions_ = {quic::ParsedQuicVersion::Draft29()};
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.FLAGS_quic_reloadable_flag_quic_disable_version_draft_29",
      "false");
  initialize();

  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  EXPECT_EQ(transport_socket_factory_->clientContextConfig().serverNameIndication(),
            codec_client_->connection()->requestedServerName());
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  codec_client_->close();
  test_server_->waitForCounterEq("http3.quic_version_h3_29", 1u);
}

TEST_P(QuicHttpIntegrationTest, ZeroRtt) {
  // Make sure both connections use the same PersistentQuicInfoImpl.
  concurrency_ = 1;
  initialize();
  // Start the first connection.
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  EXPECT_EQ(transport_socket_factory_->clientContextConfig().serverNameIndication(),
            codec_client_->connection()->requestedServerName());
  // Send a complete request on the first connection.
  auto response1 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response1->waitForEndStream());
  // Close the first connection.
  codec_client_->close();
  // Start a second connection.
  codec_client_ = makeRawHttp3Connection(makeClientConnection((lookupPort("http"))), absl::nullopt,
                                         /*wait_for_1rtt_key*/ false);
  // Send a complete request on the second connection.
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  EXPECT_THAT(upstream_request_->headers(), HeaderValueOf(Http::Headers::get().EarlyData, "1"));
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response2->waitForEndStream());
  // Ensure 0-RTT was used by second connection.
  EnvoyQuicClientSession* quic_session =
      static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  EXPECT_TRUE(static_cast<quic::QuicCryptoClientStream*>(
                  quic::test::QuicSessionPeer::GetMutableCryptoStream(quic_session))
                  ->EarlyDataAccepted());
  EXPECT_NE(quic_session->ssl(), nullptr);
  EXPECT_TRUE(quic_session->ssl()->peerCertificateValidated());
  // Close the second connection.
  codec_client_->close();
  if (GetParam() == Network::Address::IpVersion::v4) {
    test_server_->waitForCounterEq(
        "listener.127.0.0.1_0.http3.downstream.rx.quic_connection_close_error_"
        "code_QUIC_NO_ERROR",
        2u);
  } else {
    test_server_->waitForCounterEq("listener.[__1]_0.http3.downstream.rx.quic_connection_close_"
                                   "error_code_QUIC_NO_ERROR",
                                   2u);
  }

  test_server_->waitForCounterEq("http3.quic_version_rfc_v1", 2u);

  // Start the third connection.
  codec_client_ = makeRawHttp3Connection(makeClientConnection((lookupPort("http"))), absl::nullopt,
                                         /*wait_for_1rtt_key*/ false);
  auto response3 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  EXPECT_THAT(upstream_request_->headers(), HeaderValueOf(Http::Headers::get().EarlyData, "1"));
  const Http::TestResponseHeaderMapImpl response_headers{{":status", "425"}};
  upstream_request_->encodeHeaders(response_headers, true);
  ASSERT_TRUE(response3->waitForEndStream());
  // Without retry, 425 should be forwarded back to the client.
  EXPECT_EQ("425", response3->headers().getStatusValue());
  codec_client_->close();

  // Start the fourth connection.
  codec_client_ = makeRawHttp3Connection(makeClientConnection((lookupPort("http"))), absl::nullopt,
                                         /*wait_for_1rtt_key*/ false);
  Http::TestRequestHeaderMapImpl request{{":method", "GET"},
                                         {":path", "/test/long/url"},
                                         {":scheme", "http"},
                                         {":authority", "host"},
                                         {"Early-Data", "2"}};
  auto response4 = codec_client_->makeHeaderOnlyRequest(request);
  waitForNextUpstreamRequest(0);
  // If the request already has Early-Data header, no additional Early-Data header should be added
  // and the header should be forwarded as is.
  EXPECT_THAT(upstream_request_->headers(), HeaderValueOf(Http::Headers::get().EarlyData, "2"));
  upstream_request_->encodeHeaders(response_headers, true);
  ASSERT_TRUE(response3->waitForEndStream());
  // 425 response should be forwarded back to the client.
  EXPECT_EQ("425", response3->headers().getStatusValue());
  codec_client_->close();
}

// Ensure multiple quic connections work, regardless of platform BPF support
TEST_P(QuicHttpIntegrationTest, MultipleQuicConnectionsDefaultMode) {
  testMultipleQuicConnections();
}

TEST_P(QuicHttpIntegrationTest, MultipleQuicConnectionsNoBPF) {
  // Note: This setting is a no-op on platforms without BPF
  class DisableBpf {
  public:
    DisableBpf() { ActiveQuicListenerFactory::setDisableKernelBpfPacketRoutingForTest(true); }
    ~DisableBpf() { ActiveQuicListenerFactory::setDisableKernelBpfPacketRoutingForTest(false); }
  };

  DisableBpf disable;
  testMultipleQuicConnections();
}

// Tests that the packets from a connection with CID longer than 8 bytes are routed to the same
// worker.
TEST_P(QuicHttpIntegrationTest, MultiWorkerWithLongConnectionId) {
  concurrency_ = 8;
  initialize();
  // Setup 9-byte CID for the next connection.
  designated_connection_ids_.push_back(quic::test::TestConnectionIdNineBytesLong(2u));
  testRouterHeaderOnlyRequestAndResponse();
}

TEST_P(QuicHttpIntegrationTest, PortMigration) {
  concurrency_ = 2;
  initialize();
  uint32_t old_port = lookupPort("http");
  codec_client_ = makeHttpConnection(old_port);
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, 1024u, false);
  while (!quic_connection_->IsHandshakeConfirmed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Change to a new port by switching socket, and connection should still continue.
  Network::Address::InstanceConstSharedPtr local_addr =
      Network::Test::getCanonicalLoopbackAddress(version_);
  quic_connection_->switchConnectionSocket(
      createConnectionSocket(server_addr_, local_addr, nullptr));
  EXPECT_NE(old_port, local_addr->ip()->port());
  // Send the rest data.
  codec_client_->sendData(*request_encoder_, 1024u, true);
  waitForNextUpstreamRequest(0, TestUtility::DefaultTimeout);
  // Send response headers, and end_stream if there is no response body.
  const Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  size_t response_size{5u};
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData(response_size, true);
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", response_headers, std::string(response_size, 'a'));

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024u * 2, upstream_request_->bodyLength());

  // Switch to a socket with bad socket options.
  auto option = std::make_shared<Network::MockSocketOption>();
  EXPECT_CALL(*option, setOption(_, _))
      .WillRepeatedly(
          Invoke([](Network::Socket&, envoy::config::core::v3::SocketOption::SocketState state) {
            if (state == envoy::config::core::v3::SocketOption::STATE_LISTENING) {
              return false;
            }
            return true;
          }));
  auto options = std::make_shared<Network::Socket::Options>();
  options->push_back(option);
  quic_connection_->switchConnectionSocket(
      createConnectionSocket(server_addr_, local_addr, options));
  EXPECT_TRUE(codec_client_->disconnected());
  cleanupUpstreamAndDownstream();
}

TEST_P(QuicHttpIntegrationTest, PortMigrationOnPathDegrading) {
  concurrency_ = 2;
  initialize();
  client_quic_options_.mutable_num_timeouts_to_trigger_port_migration()->set_value(2);
  uint32_t old_port = lookupPort("http");
  codec_client_ = makeHttpConnection(old_port);

  // Make sure that the port migration config is plumbed through.
  EXPECT_EQ(2u, quic::test::QuicSentPacketManagerPeer::GetNumPtosForPathDegrading(
                    &quic_connection_->sent_packet_manager()));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, 1024u, false);

  ASSERT_TRUE(quic_connection_->waitForHandshakeDone());
  auto old_self_addr = quic_connection_->self_address();
  quic_connection_->OnPathDegradingDetected();
  ASSERT_TRUE(quic_connection_->waitForPathResponse());
  auto self_addr = quic_connection_->self_address();
  EXPECT_NE(old_self_addr, self_addr);

  // Send the rest data.
  codec_client_->sendData(*request_encoder_, 1024u, true);
  waitForNextUpstreamRequest(0, TestUtility::DefaultTimeout);
  // Send response headers, and end_stream if there is no response body.
  const Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  size_t response_size{5u};
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData(response_size, true);
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", response_headers, std::string(response_size, 'a'));

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024u * 2, upstream_request_->bodyLength());
}

TEST_P(QuicHttpIntegrationTest, NoPortMigrationWithoutConfig) {
  concurrency_ = 2;
  initialize();
  client_quic_options_.mutable_num_timeouts_to_trigger_port_migration()->set_value(0);
  uint32_t old_port = lookupPort("http");
  codec_client_ = makeHttpConnection(old_port);
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, 1024u, false);

  ASSERT_TRUE(quic_connection_->waitForHandshakeDone());
  auto old_self_addr = quic_connection_->self_address();
  quic_connection_->OnPathDegradingDetected();
  ASSERT_FALSE(quic_connection_->waitForPathResponse(std::chrono::milliseconds(2000)));
  auto self_addr = quic_connection_->self_address();
  EXPECT_EQ(old_self_addr, self_addr);

  // Send the rest data.
  codec_client_->sendData(*request_encoder_, 1024u, true);
  waitForNextUpstreamRequest(0, TestUtility::DefaultTimeout);
  // Send response headers, and end_stream if there is no response body.
  const Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  size_t response_size{5u};
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData(response_size, true);
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", response_headers, std::string(response_size, 'a'));

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024u * 2, upstream_request_->bodyLength());
}

TEST_P(QuicHttpIntegrationTest, PortMigrationFailureOnPathDegrading) {
  concurrency_ = 2;
  validation_failure_on_path_response_ = true;
  initialize();
  uint32_t old_port = lookupPort("http");
  codec_client_ = makeHttpConnection(old_port);
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, 1024u, false);

  ASSERT_TRUE(quic_connection_->waitForHandshakeDone());
  auto old_self_addr = quic_connection_->self_address();
  quic_connection_->OnPathDegradingDetected();
  ASSERT_TRUE(quic_connection_->waitForPathResponse());
  auto self_addr = quic_connection_->self_address();
  // The path validation will fail and thus client self address will not change.
  EXPECT_EQ(old_self_addr, self_addr);

  // Send the rest data.
  codec_client_->sendData(*request_encoder_, 1024u, true);
  waitForNextUpstreamRequest(0, TestUtility::DefaultTimeout);
  // Send response headers, and end_stream if there is no response body.
  const Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  size_t response_size{5u};
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData(response_size, true);
  ASSERT_TRUE(response->waitForEndStream());
  verifyResponse(std::move(response), "200", response_headers, std::string(response_size, 'a'));

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024u * 2, upstream_request_->bodyLength());
}

TEST_P(QuicHttpIntegrationTest, AdminDrainDrainsListeners) {
  testAdminDrain(Http::CodecType::HTTP1);
}

TEST_P(QuicHttpIntegrationTest, CertVerificationFailure) {
  san_to_match_ = "www.random_domain.com";
  initialize();
  codec_client_ = makeRawHttpConnection(makeClientConnection((lookupPort("http"))), absl::nullopt);
  EXPECT_FALSE(codec_client_->connected());
  std::string failure_reason = "QUIC_TLS_CERTIFICATE_UNKNOWN with details: TLS handshake failure "
                               "(ENCRYPTION_HANDSHAKE) 46: "
                               "certificate unknown";
  EXPECT_EQ(failure_reason, codec_client_->connection()->transportFailureReason());
}

// HTTP3 doesn't support 101 SwitchProtocol response code, the client should
// reset the request.
TEST_P(QuicHttpIntegrationTest, Reset101SwitchProtocolResponse) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.set_proxy_100_continue(true); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                                 {":path", "/dynamo/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"},
                                                                 {"expect", "100-continue"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // Wait for the request headers to be received upstream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "101"}}, false);
  ASSERT_TRUE(response->waitForReset());
  codec_client_->close();
  EXPECT_FALSE(response->complete());

  // Verify stream error counters are correctly incremented.
  std::string counter_scope = GetParam() == Network::Address::IpVersion::v4
                                  ? "listener.127.0.0.1_0.http3.downstream.rx."
                                  : "listener.[__1]_0.http3.downstream.rx.";
  std::string error_code = "quic_reset_stream_error_code_QUIC_STREAM_GENERAL_PROTOCOL_ERROR";
  test_server_->waitForCounterEq(absl::StrCat(counter_scope, error_code), 1U);
}

TEST_P(QuicHttpIntegrationTest, ResetRequestWithoutAuthorityHeader) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/dynamo/url"}, {":scheme", "http"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(response->waitForEndStream());
  codec_client_->close();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("400", response->headers().getStatusValue());
}

TEST_P(QuicHttpIntegrationTest, ResetRequestWithInvalidCharacter) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  std::string value = std::string(1, 2);
  EXPECT_FALSE(Http::HeaderUtility::headerValueIsValid(value));
  default_request_headers_.addCopy("illegal_header", value);
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(response->waitForReset());
}

TEST_P(QuicHttpIntegrationTest, Http3ClientKeepalive) {
  initialize();

  constexpr uint64_t max_interval_sec = 5;
  constexpr uint64_t initial_interval_sec = 1;
  // Set connection idle network timeout to be a little larger than max interval.
  dynamic_cast<Quic::PersistentQuicInfoImpl&>(*quic_connection_persistent_info_)
      .quic_config_.SetIdleNetworkTimeout(quic::QuicTime::Delta::FromSeconds(max_interval_sec + 2));
  client_quic_options_.mutable_connection_keepalive()->mutable_max_interval()->set_seconds(
      max_interval_sec);
  client_quic_options_.mutable_connection_keepalive()->mutable_initial_interval()->set_seconds(
      initial_interval_sec);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Wait for 10s before sending back response. If keepalive is disabled, the
  // connection would have idle timed out.
  Event::TimerPtr timer(dispatcher_->createTimer([this]() -> void { dispatcher_->exit(); }));
  timer->enableTimer(std::chrono::seconds(10));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                                   {"set-cookie", "foo"},
                                                                   {"set-cookie", "bar"}},
                                   true);
  EXPECT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  // First 6 PING frames should be sent every 1s, and the following ones less frequently.
  EXPECT_LE(quic_connection_->GetStats().ping_frames_sent, 8u);
}

TEST_P(QuicHttpIntegrationTest, Http3ClientKeepaliveDisabled) {
  initialize();

  constexpr uint64_t max_interval_sec = 0;
  constexpr uint64_t initial_interval_sec = 1;
  // Set connection idle network timeout to be a little larger than max interval.
  dynamic_cast<Quic::PersistentQuicInfoImpl&>(*quic_connection_persistent_info_)
      .quic_config_.SetIdleNetworkTimeout(quic::QuicTime::Delta::FromSeconds(5));
  client_quic_options_.mutable_connection_keepalive()->mutable_max_interval()->set_seconds(
      max_interval_sec);
  client_quic_options_.mutable_connection_keepalive()->mutable_initial_interval()->set_seconds(
      initial_interval_sec);
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // As keepalive is disabled, the connection will timeout after 5s.
  EXPECT_TRUE(response->waitForReset());
  EXPECT_EQ(quic_connection_->GetStats().ping_frames_sent, 0u);
}

TEST_P(QuicHttpIntegrationTest, Http3DownstreamKeepalive) {
  constexpr uint64_t max_interval_sec = 5;
  constexpr uint64_t initial_interval_sec = 1;
  config_helper_.addConfigModifier(
      [=](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        auto* keepalive_options = hcm.mutable_http3_protocol_options()
                                      ->mutable_quic_protocol_options()
                                      ->mutable_connection_keepalive();
        keepalive_options->mutable_initial_interval()->set_seconds(initial_interval_sec);
        keepalive_options->mutable_max_interval()->set_seconds(max_interval_sec);
      });
  // Set connection idle network timeout to be a little larger than max interval.
  config_helper_.addConfigModifier([=](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    bootstrap.mutable_static_resources()
        ->mutable_listeners(0)
        ->mutable_udp_listener_config()
        ->mutable_quic_options()
        ->mutable_idle_timeout()
        ->set_seconds(max_interval_sec + 2);
  });
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Wait for 10s before sending back response. If keepalive is disabled, the
  // connection would have idle timed out.
  Event::TimerPtr timer(dispatcher_->createTimer([this]() -> void { dispatcher_->exit(); }));
  timer->enableTimer(std::chrono::seconds(10));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                                   {"set-cookie", "foo"},
                                                                   {"set-cookie", "bar"}},
                                   true);
  EXPECT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
}

TEST_P(QuicHttpIntegrationTest, NoInitialStreams) {
  // Set the fake upstream to start with 0 streams available.
  setUpstreamProtocol(Http::CodecType::HTTP3);
  envoy::config::listener::v3::QuicProtocolOptions options;
  options.mutable_quic_protocol_options()->mutable_max_concurrent_streams()->set_value(0);
  mergeOptions(options);
  initialize();

  // Create the client connection and send a request.
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // There should now be an upstream connection, but no upstream stream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_FALSE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_,
                                                           std::chrono::milliseconds(100)));

  // Update the upstream to have 1 stream available. Now Envoy should ship the
  // original request upstream.
  fake_upstream_connection_->updateConcurrentStreams(1);
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  // Make sure the standard request/response pipeline works as expected.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(QuicHttpIntegrationTest, NoStreams) {
  // Tighten the stream idle timeout, as it defaults to 5m
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        hcm.mutable_stream_idle_timeout()->set_seconds(0);
        hcm.mutable_stream_idle_timeout()->set_nanos(400 * 1000 * 1000);
      });

  // Set the fake upstream to start with 0 streams available.
  setUpstreamProtocol(Http::CodecType::HTTP3);
  envoy::config::listener::v3::QuicProtocolOptions options;
  options.mutable_quic_protocol_options()->mutable_max_concurrent_streams()->set_value(0);
  mergeOptions(options);
  initialize();

  // Create the client connection and send a request.
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // Make sure the time out closes the stream.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
}

class QuicInplaceLdsIntegrationTest : public QuicHttpIntegrationTest {
public:
  void inplaceInitialize(bool add_default_filter_chain = false) {
    autonomous_upstream_ = true;
    setUpstreamCount(2);

    config_helper_.addConfigModifier([add_default_filter_chain](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* filter_chain_0 =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
      *filter_chain_0->mutable_filter_chain_match()->mutable_server_names()->Add() = "www.lyft.com";
      auto* filter_chain_1 = bootstrap.mutable_static_resources()
                                 ->mutable_listeners(0)
                                 ->mutable_filter_chains()
                                 ->Add();
      filter_chain_1->MergeFrom(*filter_chain_0);

      // filter chain 1 route to cluster_1
      *filter_chain_1->mutable_filter_chain_match()->mutable_server_names(0) = "lyft.com";

      filter_chain_0->set_name("filter_chain_0");
      filter_chain_1->set_name("filter_chain_1");

      auto* config_blob = filter_chain_1->mutable_filters(0)->mutable_typed_config();

      ASSERT_TRUE(config_blob->Is<envoy::extensions::filters::network::http_connection_manager::v3::
                                      HttpConnectionManager>());
      auto hcm_config = MessageUtil::anyConvert<
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager>(
          *config_blob);
      hcm_config.mutable_route_config()
          ->mutable_virtual_hosts(0)
          ->mutable_routes(0)
          ->mutable_route()
          ->set_cluster("cluster_1");
      config_blob->PackFrom(hcm_config);
      bootstrap.mutable_static_resources()->mutable_clusters()->Add()->MergeFrom(
          *bootstrap.mutable_static_resources()->mutable_clusters(0));
      bootstrap.mutable_static_resources()->mutable_clusters(1)->set_name("cluster_1");

      if (add_default_filter_chain) {
        auto default_filter_chain = bootstrap.mutable_static_resources()
                                        ->mutable_listeners(0)
                                        ->mutable_default_filter_chain();
        default_filter_chain->MergeFrom(*filter_chain_0);
        default_filter_chain->set_name("filter_chain_default");
      }
    });

    QuicHttpIntegrationTest::initialize();
  }

  void makeRequestAndWaitForResponse(IntegrationCodecClient& codec_client) {
    IntegrationStreamDecoderPtr response =
        codec_client.makeHeaderOnlyRequest(default_request_headers_);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_FALSE(codec_client.sawGoAway());
  }
};

INSTANTIATE_TEST_SUITE_P(QuicHttpIntegrationTests, QuicInplaceLdsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(QuicInplaceLdsIntegrationTest, ReloadConfigUpdateNonDefaultFilterChain) {
  inplaceInitialize(/*add_default_filter_chain=*/false);

  auto codec_client_0 =
      makeHttpConnection(makeClientConnectionWithHost(lookupPort("http"), "www.lyft.com"));
  auto codec_client_1 =
      makeHttpConnection(makeClientConnectionWithHost(lookupPort("http"), "lyft.com"));

  // Remove filter_chain_1.
  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        listener->mutable_filter_chains()->RemoveLast();
      });

  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 1);
  test_server_->waitForGaugeGe("listener_manager.total_filter_chains_draining", 1);
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 0);
  makeRequestAndWaitForResponse(*codec_client_0);
  EXPECT_TRUE(codec_client_1->sawGoAway());
  codec_client_1->close();

  auto codec_client_2 =
      makeHttpConnection(makeClientConnectionWithHost(lookupPort("http"), "www.lyft.com"));
  makeRequestAndWaitForResponse(*codec_client_2);
  codec_client_2->close();

  // Update filter chain again to add back filter_chain_1.
  config_helper_.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 2);
  test_server_->waitForCounterGe("listener_manager.listener_create_success", 3);

  auto codec_client_3 =
      makeHttpConnection(makeClientConnectionWithHost(lookupPort("http"), "lyft.com"));
  makeRequestAndWaitForResponse(*codec_client_3);
  makeRequestAndWaitForResponse(*codec_client_0);
  codec_client_0->close();
  codec_client_3->close();
}

// Verify that the connection received GO_AWAY after its filter chain gets deleted during the
// listener update.
TEST_P(QuicInplaceLdsIntegrationTest, ReloadConfigUpdateDefaultFilterChain) {
  inplaceInitialize(/*add_default_filter_chain=*/true);

  auto codec_client_0 =
      makeHttpConnection(makeClientConnectionWithHost(lookupPort("http"), "www.lyft.com"));

  // Remove filter_chain_1.
  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        listener->mutable_filter_chains()->RemoveLast();
      });

  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 1);
  test_server_->waitForGaugeGe("listener_manager.total_filter_chains_draining", 1);

  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 0);
  // This connection should pick up the default filter chain.
  auto codec_client_default =
      makeHttpConnection(makeClientConnectionWithHost(lookupPort("http"), "lyft.com"));
  makeRequestAndWaitForResponse(*codec_client_default);
  makeRequestAndWaitForResponse(*codec_client_0);

  // Modify the default filter chain.
  ConfigHelper new_config_helper1(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(new_config_helper.bootstrap()));
  new_config_helper1.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap)
                                           -> void {
    auto default_filter_chain =
        bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_default_filter_chain();
    default_filter_chain->set_name("default_filter_chain_v3");
  });

  new_config_helper1.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 2);
  test_server_->waitForGaugeGe("listener_manager.total_filter_chains_draining", 1);
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 0);

  makeRequestAndWaitForResponse(*codec_client_0);
  EXPECT_TRUE(codec_client_default->sawGoAway());
  codec_client_default->close();

  // This connection should pick up the new default filter chain.
  auto codec_client_1 =
      makeHttpConnection(makeClientConnectionWithHost(lookupPort("http"), "lyft.com"));
  makeRequestAndWaitForResponse(*codec_client_1);

  // Remove the default filter chain.
  ConfigHelper new_config_helper2(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(new_config_helper1.bootstrap()));
  new_config_helper2.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        listener->clear_default_filter_chain();
      });

  new_config_helper2.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 3);
  test_server_->waitForGaugeGe("listener_manager.total_filter_chains_draining", 1);
  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 0);

  makeRequestAndWaitForResponse(*codec_client_0);
  codec_client_0->close();
  EXPECT_TRUE(codec_client_1->sawGoAway());
  codec_client_1->close();
}

} // namespace Quic
} // namespace Envoy
