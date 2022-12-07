#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>

#include <cstddef>
#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"

#include "source/common/quic/active_quic_listener.h"
#include "source/common/quic/client_connection_factory_impl.h"
#include "source/common/quic/envoy_quic_alarm_factory.h"
#include "source/common/quic/envoy_quic_client_session.h"
#include "source/common/quic/envoy_quic_connection_helper.h"
#include "source/common/quic/envoy_quic_packet_writer.h"
#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_transport_socket_factory.h"
#include "source/extensions/transport_sockets/tls/context_config_impl.h"

#include "test/common/config/dummy_config.pb.h"
#include "test/common/quic/test_utils.h"
#include "test/common/upstream/utility.h"
#include "test/config/integration/certs/clientcert_hash.h"
#include "test/config/utility.h"
#include "test/extensions/transport_sockets/tls/cert_validator/timed_cert_validator.h"
#include "test/integration/http_integration.h"
#include "test/integration/ssl_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "quiche/quic/core/crypto/quic_client_session_cache.h"
#include "quiche/quic/core/http/quic_client_push_promise_index.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/test_tools/quic_sent_packet_manager_peer.h"
#include "quiche/quic/test_tools/quic_session_peer.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

namespace Envoy {

using Extensions::TransportSockets::Tls::ContextImplPeer;

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
                                bool validation_failure_on_path_response,
                                quic::ConnectionIdGeneratorInterface& generator)
      : EnvoyQuicClientConnection(server_connection_id, initial_peer_address, helper, alarm_factory,
                                  supported_versions, local_addr, dispatcher, options, generator),
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
class QuicHttpIntegrationTestBase : public HttpIntegrationTest {
public:
  QuicHttpIntegrationTestBase(Network::Address::IpVersion version, std::string config)
      : HttpIntegrationTest(Http::CodecType::HTTP3, version, config),
        supported_versions_(quic::CurrentSupportedHttp3Versions()), conn_helper_(*dispatcher_),
        alarm_factory_(*dispatcher_, *conn_helper_.GetClock()) {}

  ~QuicHttpIntegrationTestBase() override {
    cleanupUpstreamAndDownstream();
    // Release the client before destroying |conn_helper_|. No such need once |conn_helper_| is
    // moved into a client connection factory in the base test class.
    codec_client_.reset();
  }

  Network::ClientConnectionPtr makeClientConnectionWithOptions(
      uint32_t port, const Network::ConnectionSocket::OptionsSharedPtr& options) override {
    // Setting socket options is not supported.
    return makeClientConnectionWithHost(port, "", options);
  }

  Network::ClientConnectionPtr makeClientConnectionWithHost(
      uint32_t port, const std::string& host,
      const Network::ConnectionSocket::OptionsSharedPtr& options = nullptr) {
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
        quic::ParsedQuicVersionVector{supported_versions_[0]}, local_addr, *dispatcher_, options,
        validation_failure_on_path_response_, connection_id_generator_);
    quic_connection_ = connection.get();
    ASSERT(quic_connection_persistent_info_ != nullptr);
    auto& persistent_info = static_cast<PersistentQuicInfoImpl&>(*quic_connection_persistent_info_);
    OptRef<Http::HttpServerPropertiesCache> cache;
    auto session = std::make_unique<EnvoyQuicClientSession>(
        persistent_info.quic_config_, supported_versions_, std::move(connection),
        quic::QuicServerId{
            (host.empty() ? transport_socket_factory_->clientContextConfig()->serverNameIndication()
                          : host),
            static_cast<uint16_t>(port), false},
        transport_socket_factory_->getCryptoConfig(), &push_promise_index_, *dispatcher_,
        // Use smaller window than the default one to have test coverage of client codec buffer
        // exceeding high watermark.
        /*send_buffer_limit=*/2 * Http2::Utility::OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE,
        persistent_info.crypto_stream_factory_, quic_stat_names_, cache, stats_store_, nullptr);
    return session;
  }

  IntegrationCodecClientPtr makeRawHttpConnection(
      Network::ClientConnectionPtr&& conn,
      absl::optional<envoy::config::core::v3::Http2ProtocolOptions> http2_options) override {
    ENVOY_LOG(debug, "Creating a new client {}",
              conn->connectionInfoProvider().localAddress()->asStringView());
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
    registerTestServerPorts({"http"});

    // Initialize the transport socket factory using a customized ssl option.
    ssl_client_option_.setAlpn(true).setSan(san_to_match_).setSni("lyft.com");
    NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context;
    ON_CALL(context, api()).WillByDefault(testing::ReturnRef(*api_));
    ON_CALL(context, scope()).WillByDefault(testing::ReturnRef(stats_store_));
    ON_CALL(context, sslContextManager()).WillByDefault(testing::ReturnRef(context_manager_));
    envoy::extensions::transport_sockets::quic::v3::QuicUpstreamTransport
        quic_transport_socket_config;
    auto* tls_context = quic_transport_socket_config.mutable_upstream_tls_context();
    initializeUpstreamTlsContextConfig(ssl_client_option_, *tls_context);

    envoy::config::core::v3::TransportSocket message;
    message.mutable_typed_config()->PackFrom(quic_transport_socket_config);
    auto& config_factory = Config::Utility::getAndCheckFactory<
        Server::Configuration::UpstreamTransportSocketConfigFactory>(message);
    transport_socket_factory_.reset(static_cast<QuicClientTransportSocketFactory*>(
        config_factory.createTransportSocketFactory(quic_transport_socket_config, context)
            .release()));
    ASSERT(transport_socket_factory_->clientContextConfig());
  }

  void setConcurrency(size_t concurrency) {
    concurrency_ = concurrency;
    if (concurrency > 1) {
      config_helper_.addConfigModifier(
          [=](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
            // SO_REUSEPORT is needed because concurrency > 1.
            bootstrap.mutable_static_resources()
                ->mutable_listeners(0)
                ->mutable_enable_reuse_port()
                ->set_value(true);
          });
    }
  }

  void testMultipleQuicConnections() {
    // Enabling SO_REUSEPORT with 8 workers. Unfortunately this setting makes the test rarely flaky
    // if it is configured to run with --runs_per_test=N where N > 1 but without --jobs=1.
    setConcurrency(8);
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
    if (version_ == Network::Address::IpVersion::v4) {
      test_server_->waitForCounterEq("listener.127.0.0.1_0.downstream_cx_total", 8u, timeout_first);
    } else {
      test_server_->waitForCounterEq("listener.[__1]_0.downstream_cx_total", 8u, timeout_first);
    }
    for (size_t i = 0; i < concurrency_; ++i) {
      if (version_ == Network::Address::IpVersion::v4) {
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
      codec_clients[i]->sendData(request_encoder, 1000, true);
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
  Ssl::ClientSslTransportOptions ssl_client_option_;
  std::unique_ptr<Quic::QuicClientTransportSocketFactory> transport_socket_factory_;
  bool validation_failure_on_path_response_{false};
  quic::DeterministicConnectionIdGenerator connection_id_generator_{
      quic::kQuicDefaultConnectionIdLength};
};

class QuicHttpIntegrationTest : public QuicHttpIntegrationTestBase,
                                public testing::TestWithParam<Network::Address::IpVersion> {
public:
  QuicHttpIntegrationTest()
      : QuicHttpIntegrationTestBase(GetParam(), ConfigHelper::quicHttpProxyConfig()) {}
};

class QuicHttpMultiAddressesIntegrationTest
    : public QuicHttpIntegrationTestBase,
      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  QuicHttpMultiAddressesIntegrationTest()
      : QuicHttpIntegrationTestBase(GetParam(), ConfigHelper::quicHttpProxyConfig(true)) {}

  void testMultipleQuicConnections() {
    // Enabling SO_REUSEPORT with 8 workers. Unfortunately this setting makes the test rarely flaky
    // if it is configured to run with --runs_per_test=N where N > 1 but without --jobs=1.
    setConcurrency(8);
    initialize();
    std::vector<std::string> addresses({"address1", "address2"});
    registerTestServerPorts(addresses);
    std::vector<IntegrationCodecClientPtr> codec_clients1;
    std::vector<IntegrationCodecClientPtr> codec_clients2;
    for (size_t i = 1; i <= concurrency_; ++i) {
      // The BPF filter and ActiveQuicListener::destination() look at the 1st word of connection id
      // in the packet header. And currently all QUIC versions support >= 8 bytes connection id. So
      // create connections with the first 4 bytes of connection id different from each
      // other so they should be evenly distributed.
      designated_connection_ids_.push_back(quic::test::TestConnectionId(i << 32));
      // TODO(sunjayBhatia,wrowe): deserialize this, establishing all connections in parallel
      // (Expected to save ~14s each across 6 tests on Windows)
      codec_clients1.push_back(makeHttpConnection(lookupPort("address1")));

      // Using the same connection id for the address1 and address2 here. Since the multiple
      // addresses listener are expected to create separated `ActiveQuicListener` instance for each
      // address, then expects the `UdpWorkerRouter` will route the connection to the correct
      // `ActiveQuicListener` instance. If the two connections with same connection id are going to
      // the same `ActiveQuicListener`, the test will fail.
      // For the case of the packets from the different addresses in the same listener wants to be
      // the same QUIC connection,(Which mentioned at
      // https://github.com/envoyproxy/envoy/issues/11184#issuecomment-679214885) it doesn't support
      // for now. When someday that case supported by envoy, we can change this testcase.
      designated_connection_ids_.push_back(quic::test::TestConnectionId(i << 32));
      codec_clients2.push_back(makeHttpConnection(lookupPort("address2")));
    }
    constexpr auto timeout_first = std::chrono::seconds(15);
    constexpr auto timeout_subsequent = std::chrono::milliseconds(10);
    if (version_ == Network::Address::IpVersion::v4) {
      test_server_->waitForCounterEq("listener.127.0.0.1_0.downstream_cx_total", 16u,
                                     timeout_first);
    } else {
      test_server_->waitForCounterEq("listener.[__1]_0.downstream_cx_total", 16u, timeout_first);
    }
    for (size_t i = 0; i < concurrency_; ++i) {
      if (version_ == Network::Address::IpVersion::v4) {
        test_server_->waitForGaugeEq(
            fmt::format("listener.127.0.0.1_0.worker_{}.downstream_cx_active", i), 2u,
            timeout_subsequent);
        test_server_->waitForCounterEq(
            fmt::format("listener.127.0.0.1_0.worker_{}.downstream_cx_total", i), 2u,
            timeout_subsequent);
      } else {
        test_server_->waitForGaugeEq(
            fmt::format("listener.[__1]_0.worker_{}.downstream_cx_active", i), 2u,
            timeout_subsequent);
        test_server_->waitForCounterEq(
            fmt::format("listener.[__1]_0.worker_{}.downstream_cx_total", i), 2u,
            timeout_subsequent);
      }
    }
    for (size_t i = 0; i < concurrency_; ++i) {
      fake_upstream_connection_ = nullptr;
      upstream_request_ = nullptr;

      auto encoder_decoder = codec_clients1[i]->startRequest(
          Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":path", "/test/long/url"},
                                         {":scheme", "http"},
                                         {":authority", "host"}});
      auto& request_encoder = encoder_decoder.first;
      auto response = std::move(encoder_decoder.second);
      codec_clients1[i]->sendData(request_encoder, 1000, true);
      waitForNextUpstreamRequest();
      upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                                       {"set-cookie", "foo"},
                                                                       {"set-cookie", "bar"}},
                                       true);

      ASSERT_TRUE(response->waitForEndStream());
      EXPECT_TRUE(response->complete());
      codec_clients1[i]->close();

      auto encoder_decoder2 = codec_clients2[i]->startRequest(
          Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":path", "/test/long/url"},
                                         {":scheme", "http"},
                                         {":authority", "host"}});
      auto& request_encoder2 = encoder_decoder2.first;
      auto response2 = std::move(encoder_decoder2.second);
      codec_clients2[i]->sendData(request_encoder2, 1000, true);
      waitForNextUpstreamRequest();
      upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                                       {"set-cookie", "foo"},
                                                                       {"set-cookie", "bar"}},
                                       true);

      ASSERT_TRUE(response2->waitForEndStream());
      EXPECT_TRUE(response2->complete());
      codec_clients2[i]->close();
    }
  }
};

INSTANTIATE_TEST_SUITE_P(QuicHttpIntegrationTests, QuicHttpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(QuicHttpMultiAddressesIntegrationTest,
                         QuicHttpMultiAddressesIntegrationTest,
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
      "envoy.reloadable_features.FLAGS_envoy_quic_reloadable_flag_quic_disable_version_draft_29",
      "false");
  initialize();

  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  EXPECT_EQ(transport_socket_factory_->clientContextConfig()->serverNameIndication(),
            codec_client_->connection()->requestedServerName());
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  codec_client_->close();
  test_server_->waitForCounterEq("http3.quic_version_h3_29", 1u);
}

TEST_P(QuicHttpIntegrationTest, ZeroRtt) {
  // Make sure all connections use the same PersistentQuicInfoImpl.
  concurrency_ = 1;
  const Http::TestResponseHeaderMapImpl too_early_response_headers{{":status", "425"}};

  initialize();
  // Start the first connection.
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  EXPECT_EQ(transport_socket_factory_->clientContextConfig()->serverNameIndication(),
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
  if (version_ == Network::Address::IpVersion::v4) {
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
  upstream_request_->encodeHeaders(too_early_response_headers, true);
  ASSERT_TRUE(response3->waitForEndStream());
  // This is downstream sending early data, so the 425 response should be forwarded back to the
  // client.
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
  upstream_request_->encodeHeaders(too_early_response_headers, true);
  ASSERT_TRUE(response4->waitForEndStream());
  // 425 response should be forwarded back to the client.
  EXPECT_EQ("425", response4->headers().getStatusValue());
  codec_client_->close();
}

TEST_P(QuicHttpIntegrationTest, EarlyDataDisabled) {
  // Make sure all connections use the same PersistentQuicInfoImpl.
  concurrency_ = 1;
  enable_quic_early_data_ = false;
  initialize();
  // Start the first connection.
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  EXPECT_EQ(transport_socket_factory_->clientContextConfig()->serverNameIndication(),
            codec_client_->connection()->requestedServerName());
  // Send a complete request on the first connection.
  auto response1 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response1->waitForEndStream());
  // Close the first connection.
  codec_client_->close();

  // Start a second connection.
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  // Send a complete request on the second connection.
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response2->waitForEndStream());
  // Ensure the 2nd connection is using resumption ticket but doesn't accept early data.
  EnvoyQuicClientSession* quic_session =
      static_cast<EnvoyQuicClientSession*>(codec_client_->connection());
  EXPECT_TRUE(quic_session->IsResumption());
  EXPECT_FALSE(quic_session->EarlyDataAccepted());
  // Close the second connection.
  codec_client_->close();
}

// Ensure multiple quic connections work, regardless of platform BPF support
TEST_P(QuicHttpIntegrationTest, MultipleQuicConnectionsDefaultMode) {
  testMultipleQuicConnections();
}

// Ensure multiple quic connections work, regardless of platform BPF support
TEST_P(QuicHttpMultiAddressesIntegrationTest, MultipleQuicConnectionsDefaultMode) {
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

TEST_P(QuicHttpMultiAddressesIntegrationTest, MultipleQuicConnectionsNoBPF) {
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
  setConcurrency(8);
  config_helper_.addConfigModifier([=](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    // SO_REUSEPORT is needed because concurrency > 1.
    bootstrap.mutable_static_resources()
        ->mutable_listeners(0)
        ->mutable_enable_reuse_port()
        ->set_value(true);
  });

  initialize();
  // Setup 9-byte CID for the next connection.
  designated_connection_ids_.push_back(quic::test::TestConnectionIdNineBytesLong(2u));
  testRouterHeaderOnlyRequestAndResponse();
}

TEST_P(QuicHttpIntegrationTest, PortMigration) {
  setConcurrency(2);
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
  setConcurrency(2);
  initialize();
  client_quic_options_.mutable_num_timeouts_to_trigger_port_migration()->set_value(2);
  uint32_t old_port = lookupPort("http");
  auto options = std::make_shared<Network::Socket::Options>();
  auto option = std::make_shared<Network::MockSocketOption>();
  options->push_back(option);
  EXPECT_CALL(*option, setOption(_, _)).Times(3u);
  codec_client_ = makeHttpConnection(makeClientConnectionWithOptions(old_port, options));

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
  EXPECT_CALL(*option, setOption(_, _)).Times(3u);
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
  setConcurrency(2);
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
  setConcurrency(2);
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
  EXPECT_FALSE(response->complete());

  // Verify stream error counters are correctly incremented.
  std::string counter_scope = GetParam() == Network::Address::IpVersion::v4
                                  ? "listener.127.0.0.1_0.http3.downstream.tx."
                                  : "listener.[__1]_0.http3.downstream.tx.";
  std::string error_code = "quic_connection_close_error_code_QUIC_HTTP_FRAME_ERROR";
  test_server_->waitForCounterEq(absl::StrCat(counter_scope, error_code), 1U);
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

TEST_P(QuicHttpIntegrationTest, AsyncCertVerificationSucceeds) {
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.tls_async_cert_validation")) {
    return;
  }

  // Config the client to defer cert validation by 5ms.
  envoy::config::core::v3::TypedExtensionConfig* custom_validator_config =
      new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: "envoy.tls.cert_validator.timed_cert_validator"
typed_config:
  "@type": type.googleapis.com/test.common.config.DummyConfig
  )EOF"),
                            *custom_validator_config);
  ssl_client_option_.setCustomCertValidatorConfig(custom_validator_config);
  initialize();
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  EXPECT_TRUE(codec_client_->connected());
}

TEST_P(QuicHttpIntegrationTest, AsyncCertVerificationAfterDisconnect) {
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.tls_async_cert_validation")) {
    return;
  }

  envoy::config::core::v3::TypedExtensionConfig* custom_validator_config =
      new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: "envoy.tls.cert_validator.timed_cert_validator"
typed_config:
  "@type": type.googleapis.com/test.common.config.DummyConfig
  )EOF"),
                            *custom_validator_config);
  ssl_client_option_.setCustomCertValidatorConfig(custom_validator_config);

  // Change the configured cert validation to defer 1s.
  auto* cert_validator_factory =
      Registry::FactoryRegistry<Extensions::TransportSockets::Tls::CertValidatorFactory>::
          getFactory("envoy.tls.cert_validator.timed_cert_validator");
  static_cast<Extensions::TransportSockets::Tls::TimedCertValidatorFactory*>(cert_validator_factory)
      ->resetForTest();
  static_cast<Extensions::TransportSockets::Tls::TimedCertValidatorFactory*>(cert_validator_factory)
      ->setValidationTimeOutMs(std::chrono::milliseconds(1000));
  initialize();
  // Change the handshake timeout to be 500ms to fail the handshake while the cert validation is
  // pending.
  quic::QuicTime::Delta connect_timeout = quic::QuicTime::Delta::FromMilliseconds(500);
  auto& persistent_info = static_cast<PersistentQuicInfoImpl&>(*quic_connection_persistent_info_);
  persistent_info.quic_config_.set_max_idle_time_before_crypto_handshake(connect_timeout);
  persistent_info.quic_config_.set_max_time_before_crypto_handshake(connect_timeout);
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  EXPECT_TRUE(codec_client_->disconnected());

  Envoy::Ssl::ClientContextSharedPtr client_ssl_ctx = transport_socket_factory_->sslCtx();
  auto& cert_validator = static_cast<const Extensions::TransportSockets::Tls::TimedCertValidator&>(
      ContextImplPeer::getCertValidator(
          static_cast<Extensions::TransportSockets::Tls::ClientContextImpl&>(*client_ssl_ctx)));
  EXPECT_TRUE(cert_validator.validationPending());
  while (cert_validator.validationPending()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_P(QuicHttpIntegrationTest, AsyncCertVerificationAfterTearDown) {
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.tls_async_cert_validation")) {
    return;
  }

  envoy::config::core::v3::TypedExtensionConfig* custom_validator_config =
      new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: "envoy.tls.cert_validator.timed_cert_validator"
typed_config:
  "@type": type.googleapis.com/test.common.config.DummyConfig
  )EOF"),
                            *custom_validator_config);
  ssl_client_option_.setCustomCertValidatorConfig(custom_validator_config);
  // Change the configured cert validation to defer 1s.
  auto cert_validator_factory =
      Registry::FactoryRegistry<Extensions::TransportSockets::Tls::CertValidatorFactory>::
          getFactory("envoy.tls.cert_validator.timed_cert_validator");
  static_cast<Extensions::TransportSockets::Tls::TimedCertValidatorFactory*>(cert_validator_factory)
      ->resetForTest();
  static_cast<Extensions::TransportSockets::Tls::TimedCertValidatorFactory*>(cert_validator_factory)
      ->setValidationTimeOutMs(std::chrono::milliseconds(1000));
  initialize();
  // Change the handshake timeout to be 500ms to fail the handshake while the cert validation is
  // pending.
  quic::QuicTime::Delta connect_timeout = quic::QuicTime::Delta::FromMilliseconds(500);
  auto& persistent_info = static_cast<PersistentQuicInfoImpl&>(*quic_connection_persistent_info_);
  persistent_info.quic_config_.set_max_idle_time_before_crypto_handshake(connect_timeout);
  persistent_info.quic_config_.set_max_time_before_crypto_handshake(connect_timeout);
  codec_client_ = makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  EXPECT_TRUE(codec_client_->disconnected());

  Envoy::Ssl::ClientContextSharedPtr client_ssl_ctx = transport_socket_factory_->sslCtx();
  auto& cert_validator = static_cast<const Extensions::TransportSockets::Tls::TimedCertValidator&>(
      ContextImplPeer::getCertValidator(
          static_cast<Extensions::TransportSockets::Tls::ClientContextImpl&>(*client_ssl_ctx)));
  EXPECT_TRUE(cert_validator.validationPending());
  codec_client_.reset();
  while (cert_validator.validationPending()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_P(QuicHttpIntegrationTest, MultipleNetworkFilters) {
  config_helper_.addNetworkFilter(R"EOF(
      name: envoy.test.test_network_filter
      typed_config:
        "@type": type.googleapis.com/test.integration.filters.TestNetworkFilterConfig
)EOF");
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  test_server_->waitForCounterEq("test_network_filter.on_new_connection", 1);
  EXPECT_EQ(test_server_->counter("test_network_filter.on_data")->value(), 0);
  codec_client_->close();
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

TEST_P(QuicInplaceLdsIntegrationTest, EnableAndDisableEarlyData) {
  enable_quic_early_data_ = true;
  inplaceInitialize(/*add_default_filter_chain=*/false);

  auto codec_client_0 = makeRawHttp3Connection(
      makeClientConnectionWithHost(lookupPort("http"), "www.lyft.com"), absl::nullopt,
      /*wait_for_1rtt_key*/ true);
  makeRequestAndWaitForResponse(*codec_client_0);
  codec_client_0->close();

  // Modify 1st transport socket factory to disable early data.
  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
  new_config_helper.addQuicDownstreamTransportSocketConfig(/*enable_early_data=*/false);

  new_config_helper.setLds("1");
  test_server_->waitForCounterGe("listener_manager.listener_in_place_updated", 1);
  test_server_->waitForGaugeGe("listener_manager.total_filter_chains_draining", 1);

  test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 0);
  // The 2nd connection should try to do 0-RTT but get rejected and QUICHE will transparently retry
  // the request after handshake completes.
  auto codec_client_2 = makeRawHttp3Connection(
      makeClientConnectionWithHost(lookupPort("http"), "www.lyft.com"), absl::nullopt,
      /*wait_for_1rtt_key*/ false);
  makeRequestAndWaitForResponse(*codec_client_2);

  EnvoyQuicClientSession* quic_session =
      static_cast<EnvoyQuicClientSession*>(codec_client_2->connection());
  EXPECT_FALSE(quic_session->EarlyDataAccepted());
  codec_client_2->close();
}

TEST_P(QuicInplaceLdsIntegrationTest, StatelessResetOldConnection) {
  enable_quic_early_data_ = true;
  inplaceInitialize(/*add_default_filter_chain=*/false);

  auto codec_client0 =
      makeHttpConnection(makeClientConnectionWithHost(lookupPort("http"), "www.lyft.com"));
  makeRequestAndWaitForResponse(*codec_client0);
  // Make the next connection use the same connection ID.
  designated_connection_ids_.push_back(quic_connection_->connection_id());
  codec_client0->close();
  if (version_ == Network::Address::IpVersion::v4) {
    test_server_->waitForGaugeEq("listener.127.0.0.1_0.downstream_cx_active", 0u);
  } else {
    test_server_->waitForGaugeEq("listener.[__1]_0.downstream_cx_active", 0u);
  }

  // This new connection would be reset.
  auto codec_client1 =
      makeRawHttpConnection(makeClientConnection(lookupPort("http")), absl::nullopt);
  EXPECT_TRUE(codec_client1->disconnected());

  quic::QuicErrorCode error =
      static_cast<EnvoyQuicClientSession*>(codec_client1->connection())->error();
  EXPECT_TRUE(error == quic::QUIC_NETWORK_IDLE_TIMEOUT || error == quic::QUIC_HANDSHAKE_TIMEOUT);
  if (version_ == Network::Address::IpVersion::v4) {
    test_server_->waitForCounterGe(
        "listener.127.0.0.1_0.quic.dispatcher.stateless_reset_packets_sent", 1u);
  } else {
    test_server_->waitForCounterGe("listener.[__1]_0.quic.dispatcher.stateless_reset_packets_sent",
                                   1u);
  }
}

} // namespace Quic
} // namespace Envoy
