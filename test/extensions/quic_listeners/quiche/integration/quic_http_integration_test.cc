#include <cstddef>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"

#include "test/config/utility.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/http/quic_client_push_promise_index.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

#pragma GCC diagnostic pop

#include "extensions/quic_listeners/quiche/envoy_quic_client_session.h"
#include "extensions/quic_listeners/quiche/envoy_quic_client_connection.h"
#include "extensions/quic_listeners/quiche/envoy_quic_fake_proof_verifier.h"
#include "extensions/quic_listeners/quiche/envoy_quic_connection_helper.h"
#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"
#include "extensions/quic_listeners/quiche/envoy_quic_packet_writer.h"
#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "test/extensions/quic_listeners/quiche/test_utils.h"

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
          SetQuicReloadableFlag(quic_enable_version_draft_29, use_http3);
          SetQuicReloadableFlag(quic_disable_version_draft_27, !use_http3);
          SetQuicReloadableFlag(quic_disable_version_draft_25, !use_http3);
          return quic::CurrentSupportedVersions();
        }()),
        crypto_config_(std::make_unique<EnvoyQuicFakeProofVerifier>()), conn_helper_(*dispatcher_),
        alarm_factory_(*dispatcher_, *conn_helper_.GetClock()),
        injected_resource_filename_(TestEnvironment::temporaryPath("injected_resource")),
        file_updater_(injected_resource_filename_) {}

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
        quic_config_, supported_versions_, std::move(connection), server_id_, &crypto_config_,
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

      const std::string overload_config = fmt::format(R"EOF(
        refresh_interval:
          seconds: 0
          nanos: 1000000
        resource_monitors:
          - name: "envoy.resource_monitors.injected_resource"
            typed_config:
              "@type": type.googleapis.com/envoy.config.resource_monitor.injected_resource.v2alpha.InjectedResourceConfig
              filename: "{}"
        actions:
          - name: "envoy.overload_actions.stop_accepting_requests"
            triggers:
              - name: "envoy.resource_monitors.injected_resource"
                threshold:
                  value: 0.95
          - name: "envoy.overload_actions.disable_http_keepalive"
            triggers:
              - name: "envoy.resource_monitors.injected_resource"
                threshold:
                  value: 0.8
          - name: "envoy.overload_actions.stop_accepting_connections"
            triggers:
              - name: "envoy.resource_monitors.injected_resource"
                threshold:
                  value: 0.9
      )EOF",
                                                      injected_resource_filename_);
      *bootstrap.mutable_overload_manager() =
          TestUtility::parseYaml<envoy::config::overload::v3::OverloadManager>(overload_config);
    });
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          EXPECT_EQ(hcm.codec_type(), envoy::extensions::filters::network::http_connection_manager::
                                          v3::HttpConnectionManager::HTTP3);
        });

    updateResource(0);
    HttpIntegrationTest::initialize();
    registerTestServerPorts({"http"});
  }

  void updateResource(double pressure) { file_updater_.update(absl::StrCat(pressure)); }

protected:
  quic::QuicConfig quic_config_;
  quic::QuicServerId server_id_{"example.com", 443, false};
  quic::QuicClientPushPromiseIndex push_promise_index_;
  quic::ParsedQuicVersionVector supported_versions_;
  quic::QuicCryptoClientConfig crypto_config_;
  EnvoyQuicConnectionHelper conn_helper_;
  EnvoyQuicAlarmFactory alarm_factory_;
  CodecClientCallbacksForTest client_codec_callback_;
  Network::Address::InstanceConstSharedPtr server_addr_;
  EnvoyQuicClientConnection* quic_connection_{nullptr};
  bool set_reuse_port_{false};
  const std::string injected_resource_filename_;
  AtomicFileUpdater file_updater_;
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

  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

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

TEST_P(QuicHttpIntegrationTest, MultipleQuicListenersWithBPF) {
#if defined(SO_ATTACH_REUSEPORT_CBPF) && defined(__linux__)
  concurrency_ = 8;
  set_reuse_port_ = true;
  initialize();
  std::vector<IntegrationCodecClientPtr> codec_clients;
  for (size_t i = 1; i <= concurrency_; ++i) {
    // The BPF filter looks at the 1st word of connection id in the packet
    // header. And currently all QUIC versions support 8 bytes connection id. So
    // create connections with the first 4 bytes of connection id different from each
    // other so they should be evenly distributed.
    designated_connection_ids_.push_back(quic::test::TestConnectionId(i << 32));
    codec_clients.push_back(makeHttpConnection(lookupPort("http")));
  }
  if (GetParam().first == Network::Address::IpVersion::v4) {
    test_server_->waitForCounterEq("listener.0.0.0.0_0.downstream_cx_total", 8u);
  } else {
    test_server_->waitForCounterEq("listener.[__]_0.downstream_cx_total", 8u);
  }
  for (size_t i = 0; i < concurrency_; ++i) {
    if (GetParam().first == Network::Address::IpVersion::v4) {
      test_server_->waitForGaugeEq(
          fmt::format("listener.0.0.0.0_0.worker_{}.downstream_cx_active", i), 1u);
      test_server_->waitForCounterEq(
          fmt::format("listener.0.0.0.0_0.worker_{}.downstream_cx_total", i), 1u);
    } else {
      test_server_->waitForGaugeEq(fmt::format("listener.[__]_0.worker_{}.downstream_cx_active", i),
                                   1u);
      test_server_->waitForCounterEq(
          fmt::format("listener.[__]_0.worker_{}.downstream_cx_total", i), 1u);
    }
  }
  for (size_t i = 0; i < concurrency_; ++i) {
    codec_clients[i]->close();
  }
#endif
}

#ifndef __APPLE__
TEST_P(QuicHttpIntegrationTest, MultipleQuicListenersNoBPF) {
  concurrency_ = 8;
  set_reuse_port_ = true;
  initialize();
#ifdef SO_ATTACH_REUSEPORT_CBPF
#define SO_ATTACH_REUSEPORT_CBPF_TMP SO_ATTACH_REUSEPORT_CBPF
#undef SO_ATTACH_REUSEPORT_CBPF
#endif
  std::vector<IntegrationCodecClientPtr> codec_clients;
  for (size_t i = 1; i <= concurrency_; ++i) {
    // The BPF filter looks at the 1st byte of connection id in the packet
    // header. And currently all QUIC versions support 8 bytes connection id. So
    // create connections with the first 4 bytes of connection id different from each
    // other so they should be evenly distributed.
    designated_connection_ids_.push_back(quic::test::TestConnectionId(i << 32));
    codec_clients.push_back(makeHttpConnection(lookupPort("http")));
  }
  if (GetParam().first == Network::Address::IpVersion::v4) {
    test_server_->waitForCounterEq("listener.0.0.0.0_0.downstream_cx_total", 8u);
  } else {
    test_server_->waitForCounterEq("listener.[__]_0.downstream_cx_total", 8u);
  }
  // Even without BPF support, these connections should more or less distributed
  // across different workers.
  for (size_t i = 0; i < concurrency_; ++i) {
    if (GetParam().first == Network::Address::IpVersion::v4) {
      EXPECT_LT(
          test_server_->gauge(fmt::format("listener.0.0.0.0_0.worker_{}.downstream_cx_active", i))
              ->value(),
          8u);
      EXPECT_LT(
          test_server_->counter(fmt::format("listener.0.0.0.0_0.worker_{}.downstream_cx_total", i))
              ->value(),
          8u);
    } else {
      EXPECT_LT(
          test_server_->gauge(fmt::format("listener.[__]_0.worker_{}.downstream_cx_active", i))
              ->value(),
          8u);
      EXPECT_LT(
          test_server_->counter(fmt::format("listener.[__]_0.worker_{}.downstream_cx_total", i))
              ->value(),
          8u);
    }
  }
  for (size_t i = 0; i < concurrency_; ++i) {
    codec_clients[i]->close();
  }
#ifdef SO_ATTACH_REUSEPORT_CBPF_TMP
#define SO_ATTACH_REUSEPORT_CBPF SO_ATTACH_REUSEPORT_CBPF_TMP
#endif
}
#endif

#if defined(SO_ATTACH_REUSEPORT_CBPF) && defined(__linux__)
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
#endif

TEST_P(QuicHttpIntegrationTest, StopAcceptingConnectionsWhenOverloaded) {
  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  // Put envoy in overloaded state and check that it doesn't accept the new client connection.
  updateResource(0.9);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_connections.active",
                               1);
  codec_client_ = makeRawHttpConnection(makeClientConnection((lookupPort("http"))), absl::nullopt);
  EXPECT_TRUE(codec_client_->disconnected());

  // Reduce load a little to allow the connection to be accepted connection.
  updateResource(0.8);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_connections.active",
                               0);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  // Send response headers, but hold response body for now.
  upstream_request_->encodeHeaders(default_response_headers_, /*end_stream=*/false);

  updateResource(0.95);
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

TEST_P(QuicHttpIntegrationTest, AdminDrainDrainsListeners) {
  testAdminDrain(Http::CodecClient::Type::HTTP1);
}

} // namespace Quic
} // namespace Envoy
