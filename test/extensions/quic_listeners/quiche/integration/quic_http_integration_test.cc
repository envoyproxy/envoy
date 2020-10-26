#include <openssl/x509_vfy.h>

#include <cstddef>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"

#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/integration/ssl_utility.h"
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

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "extensions/quic_listeners/quiche/envoy_quic_client_session.h"
#include "extensions/quic_listeners/quiche/envoy_quic_client_connection.h"
#include "extensions/quic_listeners/quiche/envoy_quic_proof_verifier.h"
#include "extensions/quic_listeners/quiche/envoy_quic_connection_helper.h"
#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"
#include "extensions/quic_listeners/quiche/envoy_quic_packet_writer.h"
#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "extensions/quic_listeners/quiche/quic_transport_socket_factory.h"
#include "test/extensions/quic_listeners/quiche/test_utils.h"
#include "extensions/transport_sockets/tls/context_config_impl.h"

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

void updateResource(AtomicFileUpdater& updater, double pressure) {
  updater.update(absl::StrCat(pressure));
}

std::unique_ptr<QuicClientTransportSocketFactory>
createQuicClientTransportSocketFactory(const Ssl::ClientSslTransportOptions& options, Api::Api& api,
                                       const std::string& san_to_match) {
  std::string yaml_plain = R"EOF(
  common_tls_context:
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
)EOF";
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml_plain), tls_context);
  auto* common_context = tls_context.mutable_common_tls_context();

  if (options.alpn_) {
    common_context->add_alpn_protocols("h3");
  }
  if (options.san_) {
    common_context->mutable_validation_context()->add_match_subject_alt_names()->set_exact(
        san_to_match);
  }
  for (const std::string& cipher_suite : options.cipher_suites_) {
    common_context->mutable_tls_params()->add_cipher_suites(cipher_suite);
  }
  if (!options.sni_.empty()) {
    tls_context.set_sni(options.sni_);
  }

  common_context->mutable_tls_params()->set_tls_minimum_protocol_version(options.tls_version_);
  common_context->mutable_tls_params()->set_tls_maximum_protocol_version(options.tls_version_);

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
  ON_CALL(mock_factory_ctx, api()).WillByDefault(testing::ReturnRef(api));
  auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ClientContextConfigImpl>(
      tls_context, options.sigalgs_, mock_factory_ctx);
  return std::make_unique<QuicClientTransportSocketFactory>(std::move(cfg));
}

class QuicHttpIntegrationTest : public HttpIntegrationTest, public QuicMultiVersionTest {
public:
  QuicHttpIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP3, GetParam().first,
                            ConfigHelper::quicHttpProxyConfig()),
        supported_versions_([]() {
          if (GetParam().second == QuicVersionType::GquicQuicCrypto) {
            return quic::CurrentSupportedVersionsWithQuicCrypto();
          }
          bool use_http3 = GetParam().second == QuicVersionType::Iquic;
          SetQuicReloadableFlag(quic_disable_version_draft_29, !use_http3);
          SetQuicReloadableFlag(quic_disable_version_draft_27, !use_http3);
          return quic::CurrentSupportedVersions();
        }()),
        conn_helper_(*dispatcher_), alarm_factory_(*dispatcher_, *conn_helper_.GetClock()),
        injected_resource_filename_1_(TestEnvironment::temporaryPath("injected_resource_1")),
        injected_resource_filename_2_(TestEnvironment::temporaryPath("injected_resource_2")),
        file_updater_1_(injected_resource_filename_1_),
        file_updater_2_(injected_resource_filename_2_) {}

  ~QuicHttpIntegrationTest() override { cleanupUpstreamAndDownstream(); }

  Network::ClientConnectionPtr makeClientConnectionWithOptions(
      uint32_t port, const Network::ConnectionSocket::OptionsSharedPtr& options) override {
    // Setting socket options is not supported.
    ASSERT(!options);
    server_addr_ = Network::Utility::resolveUrl(
        fmt::format("udp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));
    Network::Address::InstanceConstSharedPtr local_addr =
        Network::Test::getCanonicalLoopbackAddress(version_);
    // Initiate a QUIC connection with the highest supported version. If not
    // supported by server, this connection will fail.
    // TODO(danzh) Implement retry upon version mismatch and modify test frame work to specify a
    // different version set on server side to test that.
    auto connection = std::make_unique<EnvoyQuicClientConnection>(
        getNextConnectionId(), server_addr_, conn_helper_, alarm_factory_,
        quic::ParsedQuicVersionVector{supported_versions_[0]}, local_addr, *dispatcher_, nullptr);
    quic_connection_ = connection.get();
    auto session = std::make_unique<EnvoyQuicClientSession>(
        quic_config_, supported_versions_, std::move(connection), server_id_, crypto_config_.get(),
        &push_promise_index_, *dispatcher_, 0);
    session->Initialize();
    return session;
  }

  // This call may fail because of INVALID_VERSION, because QUIC connection doesn't support
  // in-connection version negotiation.
  // TODO(#8479) Propagate INVALID_VERSION error to caller and let caller to use server advertised
  // version list to create a new connection with mutually supported version and make client codec
  // again.
  IntegrationCodecClientPtr makeRawHttpConnection(
      Network::ClientConnectionPtr&& conn,
      absl::optional<envoy::config::core::v3::Http2ProtocolOptions> http2_options) override {
    IntegrationCodecClientPtr codec =
        HttpIntegrationTest::makeRawHttpConnection(std::move(conn), http2_options);
    if (codec->disconnected()) {
      // Connection may get closed during version negotiation or handshake.
      ENVOY_LOG(error, "Fail to connect to server with error: {}",
                codec->connection()->transportFailureReason());
    } else {
      codec->setCodecClientCallbacks(client_codec_callback_);
    }
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
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport
          quic_transport_socket_config;
      auto tls_context = quic_transport_socket_config.mutable_downstream_tls_context();
      ConfigHelper::initializeTls(ConfigHelper::ServerSslOptions().setRsaCert(true).setTlsV13(true),
                                  *tls_context->mutable_common_tls_context());
      auto* filter_chain =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
      auto* transport_socket = filter_chain->mutable_transport_socket();
      transport_socket->mutable_typed_config()->PackFrom(quic_transport_socket_config);

      bootstrap.mutable_static_resources()->mutable_listeners(0)->set_reuse_port(set_reuse_port_);

      const std::string overload_config =
          fmt::format(R"EOF(
        refresh_interval:
          seconds: 0
          nanos: 1000000
        resource_monitors:
          - name: "envoy.resource_monitors.injected_resource_1"
            typed_config:
              "@type": type.googleapis.com/envoy.config.resource_monitor.injected_resource.v2alpha.InjectedResourceConfig
              filename: "{}"
          - name: "envoy.resource_monitors.injected_resource_2"
            typed_config:
              "@type": type.googleapis.com/envoy.config.resource_monitor.injected_resource.v2alpha.InjectedResourceConfig
              filename: "{}"
        actions:
          - name: "envoy.overload_actions.stop_accepting_requests"
            triggers:
              - name: "envoy.resource_monitors.injected_resource_1"
                threshold:
                  value: 0.95
          - name: "envoy.overload_actions.stop_accepting_connections"
            triggers:
              - name: "envoy.resource_monitors.injected_resource_1"
                threshold:
                  value: 0.9
          - name: "envoy.overload_actions.disable_http_keepalive"
            triggers:
              - name: "envoy.resource_monitors.injected_resource_2"
                threshold:
                  value: 0.8
      )EOF",
                      injected_resource_filename_1_, injected_resource_filename_2_);
      *bootstrap.mutable_overload_manager() =
          TestUtility::parseYaml<envoy::config::overload::v3::OverloadManager>(overload_config);
    });
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          hcm.mutable_drain_timeout()->clear_seconds();
          hcm.mutable_drain_timeout()->set_nanos(500 * 1000 * 1000);
          EXPECT_EQ(hcm.codec_type(), envoy::extensions::filters::network::http_connection_manager::
                                          v3::HttpConnectionManager::HTTP3);
        });

    updateResource(file_updater_1_, 0);
    updateResource(file_updater_2_, 0);
    HttpIntegrationTest::initialize();
    registerTestServerPorts({"http"});
    crypto_config_ =
        std::make_unique<quic::QuicCryptoClientConfig>(std::make_unique<EnvoyQuicProofVerifier>(
            stats_store_,
            createQuicClientTransportSocketFactory(
                Ssl::ClientSslTransportOptions().setAlpn(true).setSan(true), *api_, san_to_match_)
                ->clientContextConfig(),
            timeSystem()));
  }

  void testMultipleQuicConnections() {
    concurrency_ = 8;
    set_reuse_port_ = true;
    initialize();
    std::vector<IntegrationCodecClientPtr> codec_clients;
    for (size_t i = 1; i <= concurrency_; ++i) {
      // The BPF filter and ActiveQuicListener::destination() look at the 1st word of connection id
      // in the packet header. And currently all QUIC versions support 8 bytes connection id. So
      // create connections with the first 4 bytes of connection id different from each
      // other so they should be evenly distributed.
      designated_connection_ids_.push_back(quic::test::TestConnectionId(i << 32));
      codec_clients.push_back(makeHttpConnection(lookupPort("http")));
    }
    constexpr auto timeout_first = std::chrono::seconds(15);
    constexpr auto timeout_subsequent = std::chrono::milliseconds(10);
    if (GetParam().first == Network::Address::IpVersion::v4) {
      test_server_->waitForCounterEq("listener.0.0.0.0_0.downstream_cx_total", 8u, timeout_first);
    } else {
      test_server_->waitForCounterEq("listener.[__]_0.downstream_cx_total", 8u, timeout_first);
    }
    for (size_t i = 0; i < concurrency_; ++i) {
      if (GetParam().first == Network::Address::IpVersion::v4) {
        test_server_->waitForGaugeEq(
            fmt::format("listener.0.0.0.0_0.worker_{}.downstream_cx_active", i), 1u,
            timeout_subsequent);
        test_server_->waitForCounterEq(
            fmt::format("listener.0.0.0.0_0.worker_{}.downstream_cx_total", i), 1u,
            timeout_subsequent);
      } else {
        test_server_->waitForGaugeEq(
            fmt::format("listener.[__]_0.worker_{}.downstream_cx_active", i), 1u,
            timeout_subsequent);
        test_server_->waitForCounterEq(
            fmt::format("listener.[__]_0.worker_{}.downstream_cx_total", i), 1u,
            timeout_subsequent);
      }
    }
    for (size_t i = 0; i < concurrency_; ++i) {
      codec_clients[i]->close();
    }
  }

protected:
  quic::QuicConfig quic_config_;
  quic::QuicServerId server_id_{"lyft.com", 443, false};
  std::string san_to_match_{"spiffe://lyft.com/backend-team"};
  quic::QuicClientPushPromiseIndex push_promise_index_;
  quic::ParsedQuicVersionVector supported_versions_;
  std::unique_ptr<quic::QuicCryptoClientConfig> crypto_config_;
  EnvoyQuicConnectionHelper conn_helper_;
  EnvoyQuicAlarmFactory alarm_factory_;
  CodecClientCallbacksForTest client_codec_callback_;
  Network::Address::InstanceConstSharedPtr server_addr_;
  EnvoyQuicClientConnection* quic_connection_{nullptr};
  bool set_reuse_port_{false};
  const std::string injected_resource_filename_1_;
  const std::string injected_resource_filename_2_;
  AtomicFileUpdater file_updater_1_;
  AtomicFileUpdater file_updater_2_;
  std::list<quic::QuicConnectionId> designated_connection_ids_;
};

INSTANTIATE_TEST_SUITE_P(QuicHttpIntegrationTests, QuicHttpIntegrationTest,
                         testing::ValuesIn(generateTestParam()), testParamsToString);

TEST_P(QuicHttpIntegrationTest, GetRequestAndEmptyResponse) {
  testRouterHeaderOnlyRequestAndResponse();
}

TEST_P(QuicHttpIntegrationTest, GetRequestAndResponseWithBody) {
  initialize();
  sendRequestAndVerifyResponse(default_request_headers_, /*request_size=*/0,
                               default_response_headers_, /*response_size=*/1024,
                               /*backend_index*/ 0);
}

TEST_P(QuicHttpIntegrationTest, PostRequestAndResponseWithBody) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(QuicHttpIntegrationTest, PostRequestWithBigHeadersAndResponseWithBody) {
  testRouterRequestAndResponseWithBody(1024, 512, true);
}

TEST_P(QuicHttpIntegrationTest, RouterUpstreamDisconnectBeforeRequestcomplete) {
  testRouterUpstreamDisconnectBeforeRequestComplete();
}

TEST_P(QuicHttpIntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete();
  EXPECT_EQ(Http::StreamResetReason::RemoteReset, client_codec_callback_.last_stream_reset_reason_);
}

TEST_P(QuicHttpIntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete();
}

TEST_P(QuicHttpIntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete();
}

TEST_P(QuicHttpIntegrationTest, RouterUpstreamResponseBeforeRequestComplete) {
  testRouterUpstreamResponseBeforeRequestComplete();
}

TEST_P(QuicHttpIntegrationTest, Retry) { testRetry(); }

TEST_P(QuicHttpIntegrationTest, UpstreamReadDisabledOnGiantResponseBody) {
  config_helper_.setBufferLimits(/*upstream_buffer_limit=*/1024, /*downstream_buffer_limit=*/1024);
  testRouterRequestAndResponseWithBody(/*request_size=*/512, /*response_size=*/1024 * 1024, false);
}

TEST_P(QuicHttpIntegrationTest, DownstreamReadDisabledOnGiantPost) {
  config_helper_.setBufferLimits(/*upstream_buffer_limit=*/1024, /*downstream_buffer_limit=*/1024);
  testRouterRequestAndResponseWithBody(/*request_size=*/1024 * 1024, /*response_size=*/1024, false);
}

// Tests that a connection idle times out after 1s and starts delayed close.
TEST_P(QuicHttpIntegrationTest, TestDelayedConnectionTeardownTimeoutTrigger) {
  config_helper_.addFilter("{ name: envoy.filters.http.dynamo, typed_config: { \"@type\": "
                           "type.googleapis.com/google.protobuf.Empty } }");
  config_helper_.setBufferLimits(1024, 1024);
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        // 200ms.
        hcm.mutable_delayed_close_timeout()->set_nanos(200000000);
        hcm.mutable_drain_timeout()->set_seconds(1);
        hcm.mutable_common_http_protocol_options()->mutable_idle_timeout()->set_seconds(1);
      });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/test/long/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  codec_client_->sendData(*request_encoder_, 1024 * 65, false);

  response->waitForEndStream();
  // The delayed close timeout should trigger since client is not closing the connection.
  EXPECT_TRUE(codec_client_->waitForDisconnect(std::chrono::milliseconds(5000)));
  EXPECT_EQ(codec_client_->lastConnectionEvent(), Network::ConnectionEvent::RemoteClose);
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_delayed_close_timeout")->value(),
            1);
}

TEST_P(QuicHttpIntegrationTest, MultipleQuicConnectionsWithBPF) { testMultipleQuicConnections(); }

TEST_P(QuicHttpIntegrationTest, MultipleQuicConnectionsNoBPF) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.prefer_quic_kernel_bpf_packet_routing", "false");

  testMultipleQuicConnections();
}

TEST_P(QuicHttpIntegrationTest, ConnectionMigration) {
  concurrency_ = 2;
  set_reuse_port_ = true;
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
  response->waitForEndStream();
  verifyResponse(std::move(response), "200", response_headers, std::string(response_size, 'a'));

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(1024u * 2, upstream_request_->bodyLength());
  cleanupUpstreamAndDownstream();
}

TEST_P(QuicHttpIntegrationTest, StopAcceptingConnectionsWhenOverloaded) {
  initialize();

  // Put envoy in overloaded state and check that it doesn't accept the new client connection.
  updateResource(file_updater_1_, 0.9);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_connections.active",
                               1);
  codec_client_ = makeRawHttpConnection(makeClientConnection((lookupPort("http"))), absl::nullopt);
  EXPECT_TRUE(codec_client_->disconnected());

  // Reduce load a little to allow the connection to be accepted connection.
  updateResource(file_updater_1_, 0.8);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_connections.active",
                               0);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  // Send response headers, but hold response body for now.
  upstream_request_->encodeHeaders(default_response_headers_, /*end_stream=*/false);

  updateResource(file_updater_1_, 0.95);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_requests.active", 1);
  // Existing request should be able to finish.
  upstream_request_->encodeData(10, true);
  response->waitForEndStream();
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // New request should be rejected.
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  response2->waitForEndStream();
  EXPECT_EQ("503", response2->headers().getStatusValue());
  EXPECT_EQ("envoy overloaded", response2->body());
  codec_client_->close();

  EXPECT_TRUE(makeRawHttpConnection(makeClientConnection((lookupPort("http"))), absl::nullopt)
                  ->disconnected());
}

TEST_P(QuicHttpIntegrationTest, NoNewStreamsWhenOverloaded) {
  initialize();
  updateResource(file_updater_1_, 0.7);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  // Send a complete request and start a second.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();

  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);

  // Enable the disable-keepalive overload action. This should send a shutdown notice before
  // encoding the headers.
  updateResource(file_updater_2_, 0.9);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.disable_http_keepalive.active", 1);

  upstream_request_->encodeHeaders(default_response_headers_, /*end_stream=*/false);
  upstream_request_->encodeData(10, true);

  response2->waitForHeaders();
  EXPECT_TRUE(codec_client_->waitForDisconnect());

  EXPECT_TRUE(codec_client_->sawGoAway());
  codec_client_->close();
}

TEST_P(QuicHttpIntegrationTest, AdminDrainDrainsListeners) {
  testAdminDrain(Http::CodecClient::Type::HTTP1);
}

TEST_P(QuicHttpIntegrationTest, CertVerificationFailure) {
  san_to_match_ = "www.random_domain.com";
  initialize();
  codec_client_ = makeRawHttpConnection(makeClientConnection((lookupPort("http"))), absl::nullopt);
  EXPECT_FALSE(codec_client_->connected());
  std::string failure_reason =
      GetParam().second == QuicVersionType::GquicQuicCrypto
          ? "QUIC_PROOF_INVALID with details: Proof invalid: X509_verify_cert: certificate "
            "verification error at depth 0: ok"
          : "QUIC_HANDSHAKE_FAILED with details: TLS handshake failure (ENCRYPTION_HANDSHAKE) 46: "
            "certificate unknown";
  EXPECT_EQ(failure_reason, codec_client_->connection()->transportFailureReason());
}

} // namespace Quic
} // namespace Envoy
