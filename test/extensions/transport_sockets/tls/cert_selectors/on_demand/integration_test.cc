#include <memory>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/filter_state_override/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/static_name/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/secret/v3/sds.pb.h"

#include "test/config/integration/certs/clientcert_hash.h"
#include "test/config/integration/certs/server2cert_hash.h"
#include "test/config/integration/certs/servercert_hash.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/tcp_proxy_integration.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "absl/strings/ascii.h"
#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"

using testing::Eq;
namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace OnDemand {
namespace {

// Hack to force linking of the service: https://github.com/google/protobuf/issues/4221.
const envoy::service::secret::v3::SdsDummy _sds_dummy;

struct TestParams {
  Network::Address::IpVersion ip_version;
  bool upstream;
};

std::string testParamsToString(const testing::TestParamInfo<TestParams>& p) {
  return fmt::format("{}_{}", TestUtility::ipVersionToString(p.param.ip_version),
                     p.param.upstream ? "Upstream" : "Downstream");
}

std::vector<TestParams> testParams() {
  std::vector<TestParams> ret;
  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    ret.push_back(TestParams{ip_version, true});
    ret.push_back(TestParams{ip_version, false});
  }
  return ret;
}

class OnDemandIntegrationTest : public BaseTcpProxySslIntegrationTest,
                                public testing::TestWithParam<TestParams> {
public:
  OnDemandIntegrationTest()
      : BaseTcpProxySslIntegrationTest(GetParam().ip_version),
        upstream_selector_(GetParam().upstream) {}

  void TearDown() override { cleanUpXdsConnection(); }

  void setup(const std::string& config = "") {
    const std::string on_demand_config = config.empty() ? defaultConfig() : config;
    config_helper_.addConfigModifier([this, on_demand_config](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.mutable_static_resources()->add_clusters()->MergeFrom(
          bootstrap.static_resources().clusters(0));
      auto* sds_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      sds_cluster->set_name("sds_cluster");
      sds_cluster->mutable_load_assignment()->set_cluster_name("sds_cluster");
      ConfigHelper::setHttp2(*sds_cluster);
      if (upstream_selector_) {
        bootstrap.mutable_static_resources()
            ->mutable_listeners(0)
            ->mutable_filter_chains(0)
            ->clear_transport_socket();
        auto* backend = bootstrap.mutable_static_resources()->mutable_clusters(1);
        auto* transport_socket = backend->mutable_transport_socket();
        transport_socket->set_name("envoy.transport_sockets.tls");
        envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
        configToUseSds(*tls_context.mutable_common_tls_context(), on_demand_config);
        std::ignore = transport_socket->mutable_typed_config()->PackFrom(tls_context);
        if (!filter_state_value_.empty()) {
          const std::string set_filter_state = fmt::format(R"EOF(
          name: envoy.filters.network.set_filter_state
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.set_filter_state.v3.Config
            on_new_connection:
            - object_key: envoy.tls.certificate_mappers.on_demand_secret
              factory_key: envoy.string
              format_string:
                text_format_source:
                  inline_string: "{}"
              shared_with_upstream: ONCE
          )EOF",
                                                           filter_state_value_);
          envoy::config::listener::v3::Filter filter;
          TestUtility::loadFromYaml(set_filter_state, filter);
          auto* filter_chain =
              bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
          filter_chain->add_filters()->MergeFrom(filter_chain->filters(0));
          filter_chain->mutable_filters(0)->Swap(&filter);
        }
      } else {
        auto* filter_chain =
            bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
        auto* transport_socket = filter_chain->mutable_transport_socket();
        transport_socket->set_name("envoy.transport_sockets.tls");
        envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
        configToUseSds(*tls_context.mutable_common_tls_context(), on_demand_config);
        tls_context.set_disable_stateless_session_resumption(true);
        tls_context.set_disable_stateful_session_resumption(true);
        tls_context.mutable_require_client_certificate()->set_value(mtls_);
        std::ignore = transport_socket->mutable_typed_config()->PackFrom(tls_context);
      }
    });
    BaseTcpProxySslIntegrationTest::initialize();
    test_server_->waitUntilListenersReady();
  }

  std::string defaultConfig() const {
    return R"EOF(
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
      )EOF";
  }

  void configToUseSds(
      envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& common_tls_context,
      const std::string& on_demand_config) {
    common_tls_context.add_alpn_protocols(Http::Utility::AlpnNames::get().Http11);

    if (validation_sds_) {
      auto* validation_context = common_tls_context.mutable_validation_context_sds_secret_config();
      validation_context->set_name(cacert());
      setConfigSource(validation_context->mutable_sds_config());
    } else {
      auto* validation_context = common_tls_context.mutable_validation_context();
      if (upstream_selector_) {
        validation_context->mutable_trusted_ca()->set_filename(
            TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
      } else {
        validation_context->mutable_trusted_ca()->set_filename(
            TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
        validation_context->add_verify_certificate_hash(TEST_CLIENT_CERT_HASH);
      }
    }

    // Parse on-demand TLS cert selector config.
    envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config
        on_demand;
    TestUtility::loadFromYaml(on_demand_config, on_demand);

    // Configure config source
    setConfigSource(on_demand.mutable_config_source());
    common_tls_context.mutable_custom_tls_certificate_selector()->set_name("on-demand-config");
    std::ignore = common_tls_context.mutable_custom_tls_certificate_selector()
                      ->mutable_typed_config()
                      ->PackFrom(on_demand);
  }

  void setConfigSource(envoy::config::core::v3::ConfigSource* config_source) {
    config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    auto* api_config_source = config_source->mutable_api_config_source();
    api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);
    api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
    auto* grpc_service = api_config_source->add_grpc_services();
    grpc_service->mutable_timeout()->set_seconds(300);
    grpc_service->mutable_envoy_grpc()->set_cluster_name("sds_cluster");
  }

  void createUpstreams() override {
    // SDS cluster is H2, while the data cluster is H1.
    addFakeUpstream(Http::CodecType::HTTP2);
    if (upstream_selector_) {
      addFakeUpstream(createUpstreamTlsContext(upstreamConfig()), Http::CodecType::HTTP1, false);
    } else {
      addFakeUpstream(Http::CodecType::HTTP1);
    }
    xds_upstream_ = fake_upstreams_.front().get();
  }

  FakeUpstream* dataStream() override { return fake_upstreams_.back().get(); }

  std::unique_ptr<TestClientConnection> createClientConnection() {
    if (upstream_selector_) {
      return std::make_unique<ClientRawConnection>(*this);
    }
    return std::make_unique<ClientSslConnection>(*this);
  }

  std::string cacert() const { return upstream_selector_ ? "upstreamcacert" : "cacert"; }

protected:
  const bool upstream_selector_;
  bool mtls_{false};
  bool validation_sds_{false};
  std::string filter_state_value_;

  envoy::extensions::transport_sockets::tls::v3::Secret makeSecret(absl::string_view name,
                                                                   absl::string_view cert) {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(name);
    if (cert == "cacert" || cert == "upstreamcacert") {
      auto* validation_context = secret.mutable_validation_context();
      validation_context->mutable_trusted_ca()->set_filename(TestEnvironment::runfilesPath(
          absl::StrCat("test/config/integration/certs/", cert, ".pem")));
    } else {
      auto* tls_certificate = secret.mutable_tls_certificate();
      tls_certificate->mutable_certificate_chain()->set_filename(TestEnvironment::runfilesPath(
          absl::StrCat("test/config/integration/certs/", cert, "cert.pem")));
      tls_certificate->mutable_private_key()->set_filename(TestEnvironment::runfilesPath(
          absl::StrCat("test/config/integration/certs/", cert, "key.pem")));
    }
    return secret;
  }

  FakeStream& waitSendSdsResponse(absl::string_view name, absl::string_view cert = "",
                                  bool fail_fast = false) {
    xds_streams_.emplace_back();
    AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_streams_.back());
    RELEASE_ASSERT(result, result.message());
    auto& xds_stream = *xds_streams_.back();
    xds_stream.startGrpcStream();

    envoy::service::discovery::v3::DeltaDiscoveryRequest delta_discovery_request;
    AssertionResult result2 = xds_stream.waitForGrpcMessage(*dispatcher_, delta_discovery_request);
    RELEASE_ASSERT(result2, result2.message());
    EXPECT_EQ(1, delta_discovery_request.resource_names_subscribe().size())
        << "Should be 1 resource in DELTA_GRPC";
    EXPECT_EQ(name, delta_discovery_request.resource_names_subscribe().at(0))
        << "Secret name doesn't match in the request";
    if (fail_fast) {
      removeSecret(xds_stream, name);
      return xds_stream;
    }
    sendSecret(xds_stream, name, cert.empty() ? name : cert);
    return xds_stream;
  }

  void sendSecret(FakeStream& xds_stream, absl::string_view name, absl::string_view cert) {
    envoy::service::discovery::v3::DeltaDiscoveryResponse discovery_response;
    discovery_response.set_type_url(Config::TestTypeUrl::get().Secret);
    auto* resource = discovery_response.add_resources();
    resource->set_name(name);
    std::ignore = resource->mutable_resource()->PackFrom(makeSecret(name, cert));
    xds_stream.sendGrpcMessage(discovery_response);
  }

  void removeSecret(FakeStream& xds_stream, absl::string_view name) {
    envoy::service::discovery::v3::DeltaDiscoveryResponse discovery_response;
    discovery_response.set_type_url(Config::TestTypeUrl::get().Secret);
    discovery_response.add_removed_resources(name);
    xds_stream.sendGrpcMessage(discovery_response);
    xds_stream.finishGrpcStream(Grpc::Status::Ok);
  }

  void waitCertsRequested(uint32_t count) {
    test_server_->waitForCounter(onDemandStat("cert_requested"), Eq(count),
                                 TestUtility::DefaultTimeout, dispatcher_.get());
  }

  std::string onDemandStat(absl::string_view stat) {
    return upstream_selector_ ? absl::StrCat("cluster.cluster_0.on_demand_secret.", stat)
                              : listenerStatPrefix(absl::StrCat("on_demand_secret.", stat));
  }

  std::vector<FakeStreamPtr> xds_streams_;
};

TEST_P(OnDemandIntegrationTest, BasicSuccessWithPrefetch) {
  on_server_init_function_ = [&]() {
    createXdsConnection();
    waitSendSdsResponse("server");
  };
  setup(R"EOF(
  certificate_mapper:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
      name: server
  prefetch_secret_names:
  - server
  )EOF");
  // Open two connections sequentially.
  for (int i = 0; i < 2; i++) {
    auto conn = createClientConnection();
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
    conn.reset();
  }
  EXPECT_EQ(1, test_server_->counter(onDemandStat("cert_requested"))->value());
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
  test_server_->waitForCounter("sds.server.update_success", Eq(1));
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(1));
  EXPECT_EQ(0, test_server_->counter("sds.server.update_rejected")->value());
}

TEST_P(OnDemandIntegrationTest, BasicSuccessWithoutPrefetch) {
  setup(R"EOF(
  certificate_mapper:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
      name: server
  )EOF");
  {
    auto conn = createClientConnection();
    if (upstream_selector_) {
      conn->waitForUpstreamConnection();
    }
    waitCertsRequested(1);
    createXdsConnection();
    waitSendSdsResponse("server");
    if (!upstream_selector_) {
      conn->waitForUpstreamConnection();
    }
    conn->sendAndReceiveTlsData("hello", "world");
    conn.reset();
  }

  {
    // Open a second connection, without expecting SDS.
    auto conn2 = createClientConnection();
    conn2->waitForUpstreamConnection();
    conn2->sendAndReceiveTlsData("hello", "world");
    conn2.reset();
  }
  EXPECT_EQ(1, test_server_->counter(onDemandStat("cert_requested"))->value());
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
  test_server_->waitForCounter("sds.server.update_success", Eq(1));
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(1));
  EXPECT_EQ(0, test_server_->counter("sds.server.update_rejected")->value());
}

TEST_P(OnDemandIntegrationTest, BasicSuccessSNI) {
  if (upstream_selector_) {
    GTEST_SKIP() << "SNI mapper only works on downstream";
  };
  ssl_options_.setSni("server");
  setup(R"EOF(
  certificate_mapper:
    name: sni
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
      default_value: "*"
  )EOF");
  auto conn = createClientConnection();
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server");
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
  test_server_->waitForCounter("sds.server.update_success", Eq(1));
  EXPECT_EQ(0, test_server_->counter("sds.server.update_rejected")->value());
}

// Verifies that the cache limit rejects handshakes that require fetching new secrets while
// handshakes using cached secrets are unaffected, and that a secret removal frees a cache slot.
TEST_P(OnDemandIntegrationTest, MaxSecretsOverflow) {
  if (upstream_selector_) {
    GTEST_SKIP() << "SNI mapper only works on downstream";
  }
  ssl_options_.setSni("server");
  setup(R"EOF(
  certificate_mapper:
    name: sni
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
      default_value: "*"
  max_secrets: 1
  )EOF");

  // The first secret fills the cache to its limit.
  auto conn = createClientConnection();
  waitCertsRequested(1);
  createXdsConnection();
  auto& stream = waitSendSdsResponse("server");
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();

  // A handshake that maps to another secret name overflows the cache and is rejected.
  ssl_options_.setSni("server2");
  context_ = Ssl::createClientSslTransportSocketFactory(ssl_options_, *context_manager_, *api_);
  auto conn2 = createClientConnection();
  test_server_->waitForCounter(onDemandStat("cert_overflow"), Eq(1), TestUtility::DefaultTimeout,
                               dispatcher_.get());
  conn2->waitForDisconnect();
  conn2.reset();
  // The TCP proxy dials the upstream eagerly on accept, before the handshake resolves. Claim the
  // rejected client's orphaned upstream connection so that subsequent connections observe their
  // own upstream counterparts.
  FakeRawConnectionPtr rejected_upstream;
  ASSERT_TRUE(dataStream()->waitForRawConnection(rejected_upstream));
  EXPECT_EQ(1, test_server_->counter(onDemandStat("cert_requested"))->value());
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());

  // The cached secret continues to be served.
  ssl_options_.setSni("server");
  context_ = Ssl::createClientSslTransportSocketFactory(ssl_options_, *context_manager_, *api_);
  auto conn3 = createClientConnection();
  conn3->waitForUpstreamConnection();
  conn3->sendAndReceiveTlsData("hello", "world");
  conn3.reset();

  // Removing the cached secret frees a slot for the other secret name.
  removeSecret(stream, "server");
  test_server_->waitForGauge(onDemandStat("cert_active"), Eq(0));
  ssl_options_.setSni("server2");
  context_ = Ssl::createClientSslTransportSocketFactory(ssl_options_, *context_manager_, *api_);
  auto conn4 = createClientConnection();
  waitCertsRequested(2);
  waitSendSdsResponse("server2");
  conn4->waitForUpstreamConnection();
  conn4->sendAndReceiveTlsData("hello", "world");
  conn4.reset();
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

// Verifies that a secret unused by handshakes for the idle timeout is evicted, that connections
// established with the certificate are unaffected by the eviction, and that a subsequent
// handshake fetches the secret again.
TEST_P(OnDemandIntegrationTest, IdleTimeoutEviction) {
  setup(R"EOF(
  certificate_mapper:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
      name: server
  cache_idle_timeout: 0.2s
  )EOF");
  // Establish a connection and keep it open across the eviction.
  auto conn = createClientConnection();
  if (upstream_selector_) {
    conn->waitForUpstreamConnection();
  }
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server");
  if (!upstream_selector_) {
    conn->waitForUpstreamConnection();
  }

  // With no further handshakes, the secret is evicted after the idle timeout.
  test_server_->waitForCounter(onDemandStat("cert_evicted"), Eq(1), TestUtility::DefaultTimeout,
                               dispatcher_.get());
  test_server_->waitForGauge(onDemandStat("cert_active"), Eq(0));

  // The established connection continues to work after the eviction.
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();

  // The next handshake fetches the secret again.
  auto conn2 = createClientConnection();
  if (upstream_selector_) {
    conn2->waitForUpstreamConnection();
  }
  waitCertsRequested(2);
  waitSendSdsResponse("server");
  if (!upstream_selector_) {
    conn2->waitForUpstreamConnection();
  }
  conn2->sendAndReceiveTlsData("lorem", "ipsum");
  conn2.reset();
  // Do not assert cert_active here: on a slow run the secret may already be evicted again.
  EXPECT_EQ(2, test_server_->counter(onDemandStat("cert_requested"))->value());
}

// Verifies that prefetched secrets are pinned in the cache while an on-demand canary secret in
// the same cache is evicted, proving that idle sweeps ran and skipped the pinned entry.
TEST_P(OnDemandIntegrationTest, IdleTimeoutPrefetchPinned) {
  if (upstream_selector_) {
    GTEST_SKIP() << "SNI mapper only works on downstream";
  }
  on_server_init_function_ = [&]() {
    createXdsConnection();
    waitSendSdsResponse("server");
  };
  ssl_options_.setSni("server2");
  setup(R"EOF(
  certificate_mapper:
    name: sni
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
      default_value: "*"
  prefetch_secret_names:
  - server
  cache_idle_timeout: 0.2s
  )EOF");
  // Create a canary secret via a handshake for a non-prefetched name.
  auto conn = createClientConnection();
  waitCertsRequested(2);
  waitSendSdsResponse("server2");
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();

  // The canary is evicted once idle; the prefetched secret must survive the same sweeps.
  test_server_->waitForCounter(onDemandStat("cert_evicted"), Eq(1), TestUtility::DefaultTimeout,
                               dispatcher_.get());
  test_server_->waitForGauge(onDemandStat("cert_active"), Eq(1));

  // The prefetched secret still serves handshakes from the cache, without a new SDS request.
  ssl_options_.setSni("server");
  context_ = Ssl::createClientSslTransportSocketFactory(ssl_options_, *context_manager_, *api_);
  auto conn2 = createClientConnection();
  conn2->waitForUpstreamConnection();
  conn2->sendAndReceiveTlsData("hello", "world");
  conn2.reset();
  EXPECT_EQ(2, test_server_->counter(onDemandStat("cert_requested"))->value());
  EXPECT_EQ(1, test_server_->counter(onDemandStat("cert_evicted"))->value());
}

// Verifies that an abandoned fetch for a secret unknown to the SDS server cannot permanently
// occupy the cache: a later handshake for a different name reclaims the slot.
TEST_P(OnDemandIntegrationTest, CachePoisonRecovery) {
  if (upstream_selector_) {
    GTEST_SKIP() << "SNI mapper only works on downstream";
  }
  // A connection paused in certificate selection is only reaped by the transport socket connect
  // timeout, which releases its pending handshake handle.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* filter_chain =
        bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
    filter_chain->mutable_transport_socket_connect_timeout()->set_seconds(1);
  });
  ssl_options_.setSni("junk");
  setup(R"EOF(
  certificate_mapper:
    name: sni
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
      default_value: "*"
  max_secrets: 1
  )EOF");
  // A handshake for an unknown name occupies the only cache slot; the SDS server receives the
  // subscription but never responds.
  auto conn = createClientConnection();
  waitCertsRequested(1);
  createXdsConnection();
  xds_streams_.emplace_back();
  AssertionResult stream_result =
      xds_connection_->waitForNewStream(*dispatcher_, xds_streams_.back());
  RELEASE_ASSERT(stream_result, stream_result.message());
  xds_streams_.back()->startGrpcStream();
  envoy::service::discovery::v3::DeltaDiscoveryRequest request;
  AssertionResult message_result = xds_streams_.back()->waitForGrpcMessage(*dispatcher_, request);
  RELEASE_ASSERT(message_result, message_result.message());
  EXPECT_EQ("junk", request.resource_names_subscribe().at(0));

  // The server reaps the paused connection, releasing the pending handshake handle while the
  // abandoned subscription stays cached.
  test_server_->waitForCounter(listenerStatPrefix("downstream_cx_transport_socket_connect_timeout"),
                               Eq(1), TestUtility::DefaultTimeout, dispatcher_.get());
  test_server_->waitForCounter(listenerStatPrefix("downstream_cx_destroy"), Eq(1),
                               TestUtility::DefaultTimeout, dispatcher_.get());
  conn->waitForDisconnect();
  conn.reset();
  FakeRawConnectionPtr junk_upstream;
  ASSERT_TRUE(dataStream()->waitForRawConnection(junk_upstream));

  // A handshake for a known name reclaims the abandoned slot instead of being rejected.
  ssl_options_.setSni("server");
  context_ = Ssl::createClientSslTransportSocketFactory(ssl_options_, *context_manager_, *api_);
  auto conn2 = createClientConnection();
  test_server_->waitForCounter(onDemandStat("cert_reclaimed"), Eq(1), TestUtility::DefaultTimeout,
                               dispatcher_.get());
  waitSendSdsResponse("server");
  conn2->waitForUpstreamConnection();
  conn2->sendAndReceiveTlsData("hello", "world");
  conn2.reset();
  EXPECT_EQ(0, test_server_->counter(onDemandStat("cert_overflow"))->value());
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

// Verifies that a resolved secret for a peer-controlled name cannot permanently occupy the cache
// when the idle timeout is configured: the resolved entry idles out, freeing the slot for a
// legitimate name.
TEST_P(OnDemandIntegrationTest, CachePoisonRecoveryResolved) {
  if (upstream_selector_) {
    GTEST_SKIP() << "SNI mapper only works on downstream";
  }
  ssl_options_.setSni("junk");
  setup(R"EOF(
  certificate_mapper:
    name: sni
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
      default_value: "*"
  max_secrets: 1
  cache_idle_timeout: 0.2s
  )EOF");
  // The SDS server resolves the attacker-controlled name, filling the only cache slot.
  auto conn = createClientConnection();
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("junk", "server");
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();

  // The resolved entry is not reclaimable but idles out, freeing the slot.
  test_server_->waitForCounter(onDemandStat("cert_evicted"), Eq(1), TestUtility::DefaultTimeout,
                               dispatcher_.get());
  test_server_->waitForGauge(onDemandStat("cert_active"), Eq(0));

  // A legitimate name is admitted afterwards.
  ssl_options_.setSni("server");
  context_ = Ssl::createClientSslTransportSocketFactory(ssl_options_, *context_manager_, *api_);
  auto conn2 = createClientConnection();
  waitCertsRequested(2);
  waitSendSdsResponse("server");
  conn2->waitForUpstreamConnection();
  conn2->sendAndReceiveTlsData("hello", "world");
  conn2.reset();
  EXPECT_EQ(0, test_server_->counter(onDemandStat("cert_overflow"))->value());
}

// Verifies the overflow rejection for both the downstream and the upstream selector: a pinned,
// resolved prefetched secret fills the cache, so a handshake mapping to another name is rejected.
TEST_P(OnDemandIntegrationTest, MaxSecretsOverflowPrefetched) {
  on_server_init_function_ = [&]() {
    createXdsConnection();
    waitSendSdsResponse("server");
  };
  setup(R"EOF(
  certificate_mapper:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
      name: server2
  prefetch_secret_names:
  - server
  max_secrets: 1
  )EOF");
  auto conn = createClientConnection();
  if (upstream_selector_) {
    // Claim the upstream connection so that the fake upstream starts reading and the TLS
    // handshake reaches certificate selection.
    conn->waitForUpstreamConnection();
  }
  test_server_->waitForCounter(onDemandStat("cert_overflow"), Eq(1), TestUtility::DefaultTimeout,
                               dispatcher_.get());
  conn->waitForDisconnect();
  conn.reset();
  EXPECT_EQ(1, test_server_->counter(onDemandStat("cert_requested"))->value());
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, BasicSuccessMixed) {
  setup(R"EOF(
  certificate_mapper:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
      name: server
  prefetch_secret_names:
  - server2
  )EOF");

  createXdsConnection();
  waitSendSdsResponse("server2");
  auto conn = createClientConnection();
  if (upstream_selector_) {
    conn->waitForUpstreamConnection();
  }
  waitCertsRequested(2);
  waitSendSdsResponse("server");
  if (!upstream_selector_) {
    conn->waitForUpstreamConnection();
  }
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
  test_server_->waitForCounter("sds.server.update_success", Eq(1));
  test_server_->waitForCounter("sds.server2.update_success", Eq(1));
  EXPECT_EQ(2, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, BasicFail) {
  setup();
  auto conn = createClientConnection();
  if (upstream_selector_) {
    conn->waitForUpstreamConnection();
  }
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server", "", true);
  conn->waitForDisconnect();
  test_server_->waitForGauge(onDemandStat("cert_active"), Eq(0));
}

TEST_P(OnDemandIntegrationTest, TwoPendingConnections) {
  setup();
  // Queue two connections in pending state.
  auto conn1 = createClientConnection();
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  auto conn2 = createClientConnection();
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  if (upstream_selector_) {
    conn1->waitForUpstreamConnection();
    conn2->waitForUpstreamConnection();
  }
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server");
  if (!upstream_selector_) {
    conn1->waitForUpstreamConnection();
    conn2->waitForUpstreamConnection();
    conn1->sendAndReceiveTlsData("hello", "world");
    conn1.reset();
    conn2->sendAndReceiveTlsData("lorem", "ipsum");
  } else {
    conn1->close();
    conn2->close();
  }
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, ClientInterruptedHandshake) {
  setup();
  auto conn1 = createClientConnection();
  if (upstream_selector_) {
    conn1->waitForUpstreamConnection();
  }
  waitCertsRequested(1);
  conn1->close();
  conn1.reset();
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  // SDS request is still outstanding, so we can respond to it, and it will be used later.
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
  createXdsConnection();
  waitSendSdsResponse("server");
}

TEST_P(OnDemandIntegrationTest, ListenerConnectTimeout) {
  if (upstream_selector_) {
    GTEST_SKIP() << "Upstream selector does not depend on listener socket timeout";
  }
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* filter_chain =
        bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
    auto* connect_timeout = filter_chain->mutable_transport_socket_connect_timeout();
    connect_timeout->set_seconds(1);
  });
  setup();
  auto conn = createClientConnection();
  test_server_->waitForCounter(listenerStatPrefix("downstream_cx_transport_socket_connect_timeout"),
                               Eq(1), TestUtility::DefaultTimeout, dispatcher_.get());
  conn->close();
  conn.reset();
  // SDS request is still outstanding, so we can respond to it, and it will be used later.
  createXdsConnection();
  waitSendSdsResponse("server");
}

TEST_P(OnDemandIntegrationTest, SecretAddRemove) {
  setup();
  // Add successfully.
  auto conn = createClientConnection();
  if (upstream_selector_) {
    conn->waitForUpstreamConnection();
  }
  waitCertsRequested(1);
  createXdsConnection();
  auto& stream = waitSendSdsResponse("server");
  if (!upstream_selector_) {
    conn->waitForUpstreamConnection();
  }
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());

  // Remove.
  removeSecret(stream, "server");
  test_server_->waitForGauge(onDemandStat("cert_active"), Eq(0));

  // Request again.
  auto conn2 = createClientConnection();
  if (upstream_selector_) {
    conn2->waitForUpstreamConnection();
  }
  waitSendSdsResponse("server");
  if (!upstream_selector_) {
    conn2->waitForUpstreamConnection();
  }
  conn2->sendAndReceiveTlsData("hello", "world");
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, SecretUpdate) {
  setup();
  auto conn1 = createClientConnection();
  if (upstream_selector_) {
    conn1->waitForUpstreamConnection();
  }
  waitCertsRequested(1);
  createXdsConnection();
  auto& stream = waitSendSdsResponse("server");
  if (!upstream_selector_) {
    conn1->waitForUpstreamConnection();
  }
  conn1->sendAndReceiveTlsData("hello", "world");
  conn1.reset();

  // Update with another valid secret.
  sendSecret(stream, "server", "server2");
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(2));

  auto conn2 = createClientConnection();
  conn2->waitForUpstreamConnection();
  conn2->sendAndReceiveTlsData("hello", "world");
  conn2.reset();

  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, BasicSuccessMtlsSuccess) {
  if (upstream_selector_) {
    GTEST_SKIP() << "mTLS only applies to downstream listener config";
  }
  mtls_ = true;
  setup();
  auto conn = createClientConnection();
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server");
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  // Ensure that the session ID is not issued: this is an indirect evidence that TLS resumption
  // is disabled on the server-side.
  EXPECT_EQ("", conn->tlsSessionId())
      << "Unexpected TLS session ID: " << conn->tlsSessionId().value_or("<none>");
  conn.reset();
}

TEST_P(OnDemandIntegrationTest, BasicSuccessMtlsFail) {
  if (upstream_selector_) {
    GTEST_SKIP() << "mTLS only applies to downstream listener config";
  }
  mtls_ = true;
  ssl_options_.no_cert_ = true;
  useListenerAccessLog("%DOWNSTREAM_TRANSPORT_FAILURE_REASON%");
  setup();
  auto conn = createClientConnection();
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server");
  conn->waitForDisconnect();
  auto log_result = waitForAccessLog(listener_access_log_name_);
  EXPECT_THAT(log_result, ::testing::HasSubstr("PEER_DID_NOT_RETURN_A_CERTIFICATE"));
}

TEST_P(OnDemandIntegrationTest, ValidationContextUpdate) {
  mtls_ = true;
  validation_sds_ = true;
  FakeStream* ca_stream = nullptr;
  on_server_init_function_ = [&]() {
    createXdsConnection();
    // This is a valid CA cert.
    ca_stream = &waitSendSdsResponse(cacert());
  };
  setup();
  // Connection should work as-expected.
  {
    auto conn = createClientConnection();
    if (upstream_selector_) {
      conn->waitForUpstreamConnection();
    }
    waitCertsRequested(1);
    waitSendSdsResponse("server");
    if (!upstream_selector_) {
      conn->waitForUpstreamConnection();
    }
    conn->sendAndReceiveTlsData("hello", "world");
    conn.reset();
  }
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(1));

  // Send a wrong CA via validation SDS and open a new connection that fails.
  {
    sendSecret(*ca_stream, cacert(), upstream_selector_ ? "cacert" : "upstreamcacert");
    test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(2));
    auto conn = createClientConnection();
    if (upstream_selector_) {
      conn->waitForUpstreamConnection();
    }
    conn->waitForDisconnect();
    conn.reset();
  }
}

TEST_P(OnDemandIntegrationTest, ValidationContextUpdateWithPending) {
  if (upstream_selector_) {
    GTEST_SKIP() << "Cannot test because upstream selector validates with the CA prior to sending "
                    "a client cert";
  }
  mtls_ = true;
  validation_sds_ = true;
  FakeStream* ca_stream = nullptr;
  on_server_init_function_ = [&]() {
    createXdsConnection();
    // This is an invalid CA cert.
    ca_stream = &waitSendSdsResponse(cacert(), upstream_selector_ ? "cacert" : "upstreamcacert");
  };
  setup();
  // Queue a pending connection, then issue a context config update, and unblock the connection.
  // In this case, the original context might reference an older context config.
  auto conn = createClientConnection();
  waitCertsRequested(1);
  // Fix the CA cert, then send the actual server cert.
  sendSecret(*ca_stream, cacert(), cacert());
  waitSendSdsResponse("server");
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
}

TEST_P(OnDemandIntegrationTest, BasicSuccessFilterStateOverride) {
  if (!upstream_selector_) {
    GTEST_SKIP() << "Filter state mapper only works on upstream";
  };
  filter_state_value_ = "server";
  setup(R"EOF(
  certificate_mapper:
    name: filter_state_override
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.filter_state_override.v3.Config
      default_value: "*"
  )EOF");
  auto conn = createClientConnection();
  conn->waitForUpstreamConnection();
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server");
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
}

// Exercises the RFC 8879 compressed-certificate cache together with the on-demand selector,
// verifying the correct certificate is served as the certificate behind a name changes.
//
// Each certificate gets its own SSL_CTX (the on-demand selector switches to it via
// SSL_set_SSL_CTX during the handshake) with its own compressed-cert cache, keyed by
// (algorithm, certificate chain). Two sequential connections per certificate exercise a
// cache miss then a cache hit; after the secret is updated to a different chain, the selector
// serves it from a new SSL_CTX. Each connection validates that the certificate presented to
// the client is the one currently configured -- never a stale cached chain.
TEST_P(OnDemandIntegrationTest, CertCompressionCacheAcrossContexts) {
  if (upstream_selector_) {
    GTEST_SKIP() << "Certificate compression applies to the downstream server certificate path";
  }
  config_helper_.addRuntimeOverride("envoy.reloadable_features.tls_certificate_compression_brotli",
                                    "true");
  setup();

  // sha256PeerCertificateDigest() is lowercase, separator-free hex; the cert hash headers are
  // uppercase and colon-separated.
  const auto normalize = [](absl::string_view hash) {
    return absl::AsciiStrToLower(absl::StrReplaceAll(hash, {{":", ""}}));
  };
  const std::string server_digest = normalize(TEST_SERVER_CERT_HASH);
  const std::string server2_digest = normalize(TEST_SERVER2_CERT_HASH);

  // First connection: cache miss compresses and stores the chain for "server".
  auto conn1 = createClientConnection();
  waitCertsRequested(1);
  createXdsConnection();
  auto& stream = waitSendSdsResponse("server");
  conn1->waitForUpstreamConnection();
  conn1->sendAndReceiveTlsData("hello", "world");
  EXPECT_EQ(server_digest, conn1->peerCertificateSha256Digest().value_or(""));
  conn1.reset();

  // Second connection on the same, unchanged certificate: cache hit on the same SSL_CTX.
  auto conn1b = createClientConnection();
  conn1b->waitForUpstreamConnection();
  conn1b->sendAndReceiveTlsData("hello", "world");
  EXPECT_EQ(server_digest, conn1b->peerCertificateSha256Digest().value_or(""));
  conn1b.reset();

  // Update "server" to a different certificate chain. The on-demand selector builds a new
  // SSL_CTX (with a fresh, empty compressed-cert cache) for it.
  sendSecret(stream, "server", "server2");
  test_server_->waitForCounter(onDemandStat("cert_updated"), Eq(2));

  // Two more connections now serve the new chain: miss then hit on the new SSL_CTX. Each must
  // present the new certificate, proving no stale chain is served from a cache.
  for (int i = 0; i < 2; i++) {
    auto conn = createClientConnection();
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
    EXPECT_EQ(server2_digest, conn->peerCertificateSha256Digest().value_or(""));
    conn.reset();
  }
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

INSTANTIATE_TEST_SUITE_P(TcpProxyIntegrationTestParams, OnDemandIntegrationTest,
                         testing::ValuesIn(testParams()), testParamsToString);

} // namespace
} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
