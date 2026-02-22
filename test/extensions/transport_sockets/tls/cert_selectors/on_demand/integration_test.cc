#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

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
#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/tcp_proxy_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

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

uint64_t percentileMillis(std::vector<uint64_t> values, double percentile) {
  if (values.empty()) {
    return 0;
  }
  const double clamped = std::max(0.0, std::min(100.0, percentile));
  const size_t idx =
      static_cast<size_t>((clamped / 100.0) * static_cast<double>(values.size() - 1));
  std::nth_element(values.begin(), values.begin() + idx, values.end());
  return values[idx];
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
        transport_socket->mutable_typed_config()->PackFrom(tls_context);
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
        transport_socket->mutable_typed_config()->PackFrom(tls_context);
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

  std::string localSignerConfig(absl::string_view mapped_name, bool strict_hostname_validation = false) const {
    return fmt::format(R"EOF(
      local_signer:
        ca_cert_path: {}
        ca_key_path: {}
        runtime_key_prefix: on_demand_secret.test.local_signer
        hostname_validation: {}
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: {}
      )EOF",
                       TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"),
                       TestEnvironment::runfilesPath("test/config/integration/certs/cakey.pem"),
                       strict_hostname_validation ? "HOSTNAME_VALIDATION_STRICT"
                                                  : "HOSTNAME_VALIDATION_PERMISSIVE",
                       mapped_name);
  }

  std::string localSignerSniConfig(absl::string_view ca_cert_path, absl::string_view ca_key_path,
                                   absl::string_view ca_reload_failure_policy) const {
    return fmt::format(R"EOF(
      local_signer:
        ca_cert_path: {}
        ca_key_path: {}
        ca_reload_failure_policy: {}
      certificate_mapper:
        name: sni
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
          default_value: fallback.example.com
      )EOF",
                       ca_cert_path, ca_key_path, ca_reload_failure_policy);
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
    common_tls_context.mutable_custom_tls_certificate_selector()->mutable_typed_config()->PackFrom(
        on_demand);
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
    resource->mutable_resource()->PackFrom(makeSecret(name, cert));
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
    test_server_->waitForCounterEq(onDemandStat("cert_requested"), count,
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
  test_server_->waitForCounterEq("sds.server.update_success", 1);
  test_server_->waitForCounterEq(onDemandStat("cert_updated"), 1);
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
  test_server_->waitForCounterEq("sds.server.update_success", 1);
  test_server_->waitForCounterEq(onDemandStat("cert_updated"), 1);
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
  test_server_->waitForCounterEq("sds.server.update_success", 1);
  EXPECT_EQ(0, test_server_->counter("sds.server.update_rejected")->value());
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
  test_server_->waitForCounterEq("sds.server.update_success", 1);
  test_server_->waitForCounterEq("sds.server2.update_success", 1);
  EXPECT_EQ(2, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, LocalSignerDefaultPermissiveAllowsUnderscoreName) {
  if (upstream_selector_) {
    GTEST_SKIP() << "local signer behavior is validated on downstream";
  }

  setup(localSignerConfig("server_name"));
  auto conn = createClientConnection();
  waitCertsRequested(1);
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, LocalSignerRuntimeStrictHostnameBlocksUnderscoreName) {
  if (upstream_selector_) {
    GTEST_SKIP() << "local signer behavior is validated on downstream";
  }

  config_helper_.addRuntimeOverride("on_demand_secret.test.local_signer.hostname_validation", "2");
  setup(localSignerConfig("server_name"));
  auto conn = createClientConnection();
  waitCertsRequested(1);
  conn->waitForDisconnect();
  test_server_->waitForGaugeEq(onDemandStat("cert_active"), 0);
}

TEST_P(OnDemandIntegrationTest, LocalSignerRuntimePermissiveOverridesStrictConfig) {
  if (upstream_selector_) {
    GTEST_SKIP() << "local signer behavior is validated on downstream";
  }

  config_helper_.addRuntimeOverride("on_demand_secret.test.local_signer.hostname_validation", "1");
  setup(localSignerConfig("server_name", true));
  auto conn = createClientConnection();
  waitCertsRequested(1);
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, LocalSignerFailOpenOnCaReloadErrorKeepsServing) {
  if (upstream_selector_) {
    GTEST_SKIP() << "local signer behavior is validated on downstream";
  }
  validation_sds_ = true;
  FakeStream* ca_stream = nullptr;
  const std::string ca_cert_content = TestEnvironment::readFileToStringForTest(
      TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
  const std::string ca_key_content = TestEnvironment::readFileToStringForTest(
      TestEnvironment::runfilesPath("test/config/integration/certs/cakey.pem"));
  const std::string ca_cert_path = TestEnvironment::writeStringToFileForTest(
      TestUtility::uniqueFilename("local_signer_ca_cert"), ca_cert_content);
  const std::string ca_key_path = TestEnvironment::writeStringToFileForTest(
      TestUtility::uniqueFilename("local_signer_ca_key"), ca_key_content);

  on_server_init_function_ = [&]() {
    createXdsConnection();
    ca_stream = &waitSendSdsResponse(cacert());
  };
  setup(localSignerSniConfig(ca_cert_path, ca_key_path, "CA_RELOAD_FAILURE_POLICY_FAIL_OPEN"));

  auto conn1 = createClientConnection();
  waitCertsRequested(1);
  conn1->waitForUpstreamConnection();
  conn1->sendAndReceiveTlsData("hello", "world");
  conn1.reset();
  test_server_->waitForCounterEq(onDemandStat("cert_updated"), 1);

  TestEnvironment::writeStringToFileForTest(ca_key_path, "not-a-valid-key", true);

  sendSecret(*ca_stream, cacert(), cacert());
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(1, test_server_->counter(onDemandStat("cert_updated"))->value());

  auto conn2 = createClientConnection();
  conn2->waitForUpstreamConnection();
  conn2->sendAndReceiveTlsData("hello", "world");
  conn2.reset();
}

TEST_P(OnDemandIntegrationTest, LocalSignerFailClosedOnCaReloadErrorBlocksNewSecret) {
  if (upstream_selector_) {
    GTEST_SKIP() << "local signer behavior is validated on downstream";
  }
  validation_sds_ = true;
  FakeStream* ca_stream = nullptr;
  const std::string ca_cert_content = TestEnvironment::readFileToStringForTest(
      TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
  const std::string ca_key_content = TestEnvironment::readFileToStringForTest(
      TestEnvironment::runfilesPath("test/config/integration/certs/cakey.pem"));
  const std::string ca_cert_path = TestEnvironment::writeStringToFileForTest(
      TestUtility::uniqueFilename("local_signer_ca_cert"), ca_cert_content);
  const std::string ca_key_path = TestEnvironment::writeStringToFileForTest(
      TestUtility::uniqueFilename("local_signer_ca_key"), ca_key_content);

  on_server_init_function_ = [&]() {
    createXdsConnection();
    ca_stream = &waitSendSdsResponse(cacert());
  };
  setup(localSignerSniConfig(ca_cert_path, ca_key_path, "CA_RELOAD_FAILURE_POLICY_FAIL_CLOSED"));

  auto conn1 = createClientConnection();
  waitCertsRequested(1);
  conn1->waitForUpstreamConnection();
  conn1->sendAndReceiveTlsData("hello", "world");
  conn1.reset();
  test_server_->waitForCounterEq(onDemandStat("cert_updated"), 1);

  TestEnvironment::writeStringToFileForTest(ca_key_path, "not-a-valid-key", true);
  sendSecret(*ca_stream, cacert(), cacert());
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(1, test_server_->counter(onDemandStat("cert_updated"))->value());

  auto conn2 = createClientConnection();
  conn2->waitForUpstreamConnection();
  conn2->sendAndReceiveTlsData("hello", "world");
  conn2->close();
}

TEST_P(OnDemandIntegrationTest, LocalSignerHandshakeLatencyUnderCertChurn) {
  if (upstream_selector_) {
    GTEST_SKIP() << "local signer behavior is validated on downstream";
  }
  setup(localSignerSniConfig(
      TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"),
      TestEnvironment::runfilesPath("test/config/integration/certs/cakey.pem"),
      "CA_RELOAD_FAILURE_POLICY_FAIL_OPEN"));

  constexpr size_t connection_count = 20;
  std::vector<uint64_t> latencies_ms;
  latencies_ms.reserve(connection_count);

  for (size_t i = 0; i < connection_count; ++i) {
    ssl_options_.setSni(fmt::format("churn-{}.example.com", i));
    const auto start = std::chrono::steady_clock::now();
    auto conn = createClientConnection();
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
    conn.reset();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    latencies_ms.push_back(static_cast<uint64_t>(elapsed));
  }

  const uint64_t p50 = percentileMillis(latencies_ms, 50.0);
  const uint64_t p95 = percentileMillis(latencies_ms, 95.0);
  const uint64_t p99 = percentileMillis(latencies_ms, 99.0);
  // Guardrail bounds: this is a correctness/perf-regression detector, not a microbenchmark.
  EXPECT_LT(p95, 5000);
  EXPECT_LT(p99, 8000);
  ENVOY_LOG_MISC(info,
                 "on_demand local signer churn latency: count={} p50_ms={} p95_ms={} p99_ms={}",
                 connection_count, p50, p95, p99);
}

TEST_P(OnDemandIntegrationTest, LocalSignerThroughputBurstUniqueCerts) {
  if (upstream_selector_) {
    GTEST_SKIP() << "local signer behavior is validated on downstream";
  }
  setup(localSignerSniConfig(
      TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"),
      TestEnvironment::runfilesPath("test/config/integration/certs/cakey.pem"),
      "CA_RELOAD_FAILURE_POLICY_FAIL_OPEN"));

  constexpr size_t connection_count = 32;
  const auto start = std::chrono::steady_clock::now();
  for (size_t i = 0; i < connection_count; ++i) {
    ssl_options_.setSni(fmt::format("burst-{}.example.com", i));
    auto conn = createClientConnection();
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
    conn.reset();
  }
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count();
  EXPECT_LT(elapsed_ms, 30000);
}

TEST_P(OnDemandIntegrationTest, LocalSignerFailOpenConcurrentPendingNewSecretSucceeds) {
  if (upstream_selector_) {
    GTEST_SKIP() << "local signer behavior is validated on downstream";
  }
  validation_sds_ = true;
  FakeStream* ca_stream = nullptr;
  const std::string ca_cert_content = TestEnvironment::readFileToStringForTest(
      TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
  const std::string ca_key_content = TestEnvironment::readFileToStringForTest(
      TestEnvironment::runfilesPath("test/config/integration/certs/cakey.pem"));
  const std::string ca_cert_path = TestEnvironment::writeStringToFileForTest(
      TestUtility::uniqueFilename("local_signer_ca_cert"), ca_cert_content);
  const std::string ca_key_path = TestEnvironment::writeStringToFileForTest(
      TestUtility::uniqueFilename("local_signer_ca_key"), ca_key_content);
  on_server_init_function_ = [&]() {
    createXdsConnection();
    ca_stream = &waitSendSdsResponse(cacert());
  };
  setup(localSignerSniConfig(ca_cert_path, ca_key_path, "CA_RELOAD_FAILURE_POLICY_FAIL_OPEN"));

  ssl_options_.setSni("warm.example.com");
  auto warm = createClientConnection();
  waitCertsRequested(1);
  warm->waitForUpstreamConnection();
  warm->sendAndReceiveTlsData("hello", "world");
  warm.reset();

  TestEnvironment::writeStringToFileForTest(ca_key_path, "not-a-valid-key", true);
  sendSecret(*ca_stream, cacert(), cacert());
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  ssl_options_.setSni("concurrent-new.example.com");
  std::vector<std::unique_ptr<TestClientConnection>> conns;
  conns.reserve(8);
  for (int i = 0; i < 8; ++i) {
    conns.emplace_back(createClientConnection());
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  for (auto& conn : conns) {
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
    conn.reset();
  }
}

TEST_P(OnDemandIntegrationTest, LocalSignerFailClosedConcurrentPendingNewSecretFails) {
  if (upstream_selector_) {
    GTEST_SKIP() << "local signer behavior is validated on downstream";
  }
  validation_sds_ = true;
  FakeStream* ca_stream = nullptr;
  const std::string ca_cert_content = TestEnvironment::readFileToStringForTest(
      TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
  const std::string ca_key_content = TestEnvironment::readFileToStringForTest(
      TestEnvironment::runfilesPath("test/config/integration/certs/cakey.pem"));
  const std::string ca_cert_path = TestEnvironment::writeStringToFileForTest(
      TestUtility::uniqueFilename("local_signer_ca_cert"), ca_cert_content);
  const std::string ca_key_path = TestEnvironment::writeStringToFileForTest(
      TestUtility::uniqueFilename("local_signer_ca_key"), ca_key_content);
  on_server_init_function_ = [&]() {
    createXdsConnection();
    ca_stream = &waitSendSdsResponse(cacert());
  };
  setup(localSignerSniConfig(ca_cert_path, ca_key_path, "CA_RELOAD_FAILURE_POLICY_FAIL_CLOSED"));

  ssl_options_.setSni("warm.example.com");
  auto warm = createClientConnection();
  waitCertsRequested(1);
  warm->waitForUpstreamConnection();
  warm->sendAndReceiveTlsData("hello", "world");
  warm.reset();

  TestEnvironment::writeStringToFileForTest(ca_key_path, "not-a-valid-key", true);
  sendSecret(*ca_stream, cacert(), cacert());
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  ssl_options_.setSni("blocked-new.example.com");
  std::vector<std::unique_ptr<TestClientConnection>> conns;
  conns.reserve(8);
  for (int i = 0; i < 8; ++i) {
    conns.emplace_back(createClientConnection());
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
  for (auto& conn : conns) {
    conn->close();
    conn.reset();
  }
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
  test_server_->waitForGaugeEq(onDemandStat("cert_active"), 0);
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
  test_server_->waitForCounterEq(
      listenerStatPrefix("downstream_cx_transport_socket_connect_timeout"), 1,
      TestUtility::DefaultTimeout, dispatcher_.get());
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
  test_server_->waitForGaugeEq(onDemandStat("cert_active"), 0);

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
  test_server_->waitForCounterEq(onDemandStat("cert_updated"), 2);

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
  test_server_->waitForCounterEq(onDemandStat("cert_updated"), 1);

  // Send a wrong CA via validation SDS and open a new connection that fails.
  {
    sendSecret(*ca_stream, cacert(), upstream_selector_ ? "cacert" : "upstreamcacert");
    test_server_->waitForCounterEq(onDemandStat("cert_updated"), 2);
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

INSTANTIATE_TEST_SUITE_P(TcpProxyIntegrationTestParams, OnDemandIntegrationTest,
                         testing::ValuesIn(testParams()), testParamsToString);

} // namespace
} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
