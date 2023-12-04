#include "test_server.h"

#include "source/common/common/random_generator.h"
#include "source/common/stats/allocator_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/extensions/transport_sockets/tls/context_config_impl.h"
#include "source/server/hot_restart_nop_impl.h"
#include "source/server/instance_impl.h"

#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

namespace Envoy {

Network::DownstreamTransportSocketFactoryPtr TestServer::createQuicUpstreamTlsContext(
    testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>& factory_context) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  Extensions::TransportSockets::Tls::ContextManagerImpl context_manager{time_system_};
  tls_context.mutable_common_tls_context()->add_alpn_protocols("h3");
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* certs =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  certs->mutable_certificate_chain()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcert.pem"));
  certs->mutable_private_key()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamkey.pem"));
  envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport quic_config;
  quic_config.mutable_downstream_tls_context()->MergeFrom(tls_context);

  std::vector<std::string> server_names;
  auto& config_factory = Config::Utility::getAndCheckFactoryByName<
      Server::Configuration::DownstreamTransportSocketConfigFactory>(
      "envoy.transport_sockets.quic");

  return config_factory.createTransportSocketFactory(quic_config, factory_context, server_names);
}

Network::DownstreamTransportSocketFactoryPtr TestServer::createUpstreamTlsContext(
    testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>& factory_context) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* certs =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  certs->mutable_certificate_chain()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcert.pem"));
  certs->mutable_private_key()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamkey.pem"));
  auto* ctx = tls_context.mutable_common_tls_context()->mutable_validation_context();
  ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
  tls_context.mutable_common_tls_context()->add_alpn_protocols("h2");
  auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
      tls_context, factory_context);
  static auto* upstream_stats_store = new Stats::TestIsolatedStoreImpl();
  return std::make_unique<Extensions::TransportSockets::Tls::ServerSslSocketFactory>(
      std::move(cfg), context_manager_, *upstream_stats_store->rootScope(),
      std::vector<std::string>{});
}

TestServer::TestServer()
    : api_(Api::createApiForTest(stats_store_, time_system_)),
      version_(Network::Address::IpVersion::v4), upstream_config_(time_system_), port_(0) {
  std::string runfiles_error;
  runfiles_ = std::unique_ptr<bazel::tools::cpp::runfiles::Runfiles>{
      bazel::tools::cpp::runfiles::Runfiles::CreateForTest(&runfiles_error)};
  RELEASE_ASSERT(TestEnvironment::getOptionalEnvVar("NORUNFILES").has_value() ||
                     runfiles_ != nullptr,
                 runfiles_error);
  TestEnvironment::setRunfiles(runfiles_.get());
  ON_CALL(factory_context_.server_context_, api()).WillByDefault(testing::ReturnRef(*api_));
  ON_CALL(factory_context_, statsScope())
      .WillByDefault(testing::ReturnRef(*stats_store_.rootScope()));
}

void TestServer::startTestServer(TestServerType test_server_type) {
  ASSERT(!upstream_);
  // pre-setup: see https://github.com/envoyproxy/envoy/blob/main/test/test_runner.cc
  Logger::Context logging_state(spdlog::level::level_enum::err,
                                "[%Y-%m-%d %T.%e][%t][%l][%n] [%g:%#] %v", lock, false, false);
  // end pre-setup
  Network::DownstreamTransportSocketFactoryPtr factory;

  switch (test_server_type) {
  case TestServerType::HTTP3:
    upstream_config_.upstream_protocol_ = Http::CodecType::HTTP3;
    upstream_config_.udp_fake_upstream_ = FakeUpstreamConfig::UdpConfig();
    factory = createQuicUpstreamTlsContext(factory_context_);
    break;
  case TestServerType::HTTP2_WITH_TLS:
    upstream_config_.upstream_protocol_ = Http::CodecType::HTTP2;
    factory = createUpstreamTlsContext(factory_context_);
    break;
  case TestServerType::HTTP1_WITHOUT_TLS:
    upstream_config_.upstream_protocol_ = Http::CodecType::HTTP1;
    factory = Network::Test::createRawBufferDownstreamSocketFactory();
    break;
  case TestServerType::HTTP_PROXY: {
    std::string config_path =
        TestEnvironment::writeStringToFileForTest("config.yaml", http_proxy_config);
    test_server_ = IntegrationTestServer::create(config_path, Network::Address::IpVersion::v4,
                                                 nullptr, nullptr, {}, time_system_, *api_);
    test_server_->waitUntilListenersReady();
    ENVOY_LOG_MISC(debug, "Http proxy is now running");
    return;
  }
  case TestServerType::HTTPS_PROXY: {
    std::string config_path =
        TestEnvironment::writeStringToFileForTest("config.yaml", https_proxy_config);
    test_server_ = IntegrationTestServer::create(config_path, Network::Address::IpVersion::v4,
                                                 nullptr, nullptr, {}, time_system_, *api_);
    test_server_->waitUntilListenersReady();
    ENVOY_LOG_MISC(debug, "Https proxy is now running");
    return;
  }
  }

  upstream_ = std::make_unique<AutonomousUpstream>(std::move(factory), port_, version_,
                                                   upstream_config_, true);

  // Legacy behavior for cronet tests.
  if (test_server_type == TestServerType::HTTP3) {
    upstream_->setResponseHeaders(
        std::make_unique<Http::TestResponseHeaderMapImpl>(Http::TestResponseHeaderMapImpl(
            {{":status", "200"},
             {"Cache-Control", "max-age=0"},
             {"Content-Type", "text/plain"},
             {"X-Original-Url", "https://test.example.com:6121/simple.txt"}})));
    upstream_->setResponseBody("This is a simple text file served by QUIC.\n");
  }

  ENVOY_LOG_MISC(debug, "Upstream now listening on {}", upstream_->localAddress()->asString());
}

void TestServer::shutdownTestServer() {
  ASSERT(upstream_ || test_server_);
  upstream_.reset();
  test_server_.reset();
}

int TestServer::getServerPort() {
  ASSERT(upstream_ || test_server_);
  if (upstream_) {
    return upstream_->localAddress()->ip()->port();
  }
  std::atomic<uint32_t> port = 0;
  absl::Notification port_set;
  test_server_->server().dispatcher().post([&]() {
    auto listeners = test_server_->server().listenerManager().listeners();
    auto listener_it = listeners.cbegin();
    auto socket_factory_it = listener_it->get().listenSocketFactories().begin();
    const auto listen_addr = (*socket_factory_it)->localAddress();
    port = listen_addr->ip()->port();
    port_set.Notify();
  });
  port_set.WaitForNotification();
  return port;
}

void TestServer::setHeadersAndData(absl::string_view header_key, absl::string_view header_value,
                                   absl::string_view response_body) {
  ASSERT(upstream_);
  upstream_->setResponseHeaders(
      std::make_unique<Http::TestResponseHeaderMapImpl>(Http::TestResponseHeaderMapImpl(
          {{std::string(header_key), std::string(header_value)}, {":status", "200"}})));
  upstream_->setResponseBody(std::string(response_body));
}

const std::string TestServer::http_proxy_config = R"EOF(
static_resources:
  listeners:
  - name: listener_proxy
    address:
      socket_address: { address: 127.0.0.1, port_value: 0 }
    filter_chains:
      - filters:
        - name: envoy.filters.network.http_connection_manager
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
            stat_prefix: remote_hcm
            route_config:
              name: remote_route
              virtual_hosts:
              - name: remote_service
                domains: ["*"]
                routes:
                - match: { prefix: "/" }
                  route: { cluster: cluster_proxy }
              response_headers_to_add:
                - append_action: OVERWRITE_IF_EXISTS_OR_ADD
                  header:
                    key: x-proxy-response
                    value: 'true'
            http_filters:
              - name: envoy.filters.http.local_error
                typed_config:
                  "@type": type.googleapis.com/envoymobile.extensions.filters.http.local_error.LocalError
              - name: envoy.filters.http.dynamic_forward_proxy
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig
                  dns_cache_config: &dns_cache_config
                    name: base_dns_cache
                    dns_lookup_family: ALL
                    host_ttl: 86400s
                    dns_min_refresh_rate: 20s
                    dns_refresh_rate: 60s
                    dns_failure_refresh_rate:
                      base_interval: 2s
                      max_interval: 10s
                    dns_query_timeout: 25s
                    typed_dns_resolver_config:
                      name: envoy.network.dns_resolver.getaddrinfo
                      typed_config: {"@type":"type.googleapis.com/envoy.extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig"}
              - name: envoy.router
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  clusters:
  - name: cluster_proxy
    connect_timeout: 30s
    lb_policy: CLUSTER_PROVIDED
    dns_lookup_family: ALL
    cluster_type:
      name: envoy.clusters.dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
        dns_cache_config: *dns_cache_config
layered_runtime:
  layers:
    - name: static_layer_0
      static_layer:
        envoy:
          # This disables envoy bug stats, which are filtered out of our stats inclusion list anyway
          # Global stats do not play well with engines with limited lifetimes
          disallow_global_stats: true
)EOF";

const std::string TestServer::https_proxy_config = R"EOF(
static_resources:
  listeners:
  - name: listener_proxy
    address:
      socket_address: { address: 127.0.0.1, port_value: 0 }
    filter_chains:
      - filters:
        - name: envoy.filters.network.http_connection_manager
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
            stat_prefix: remote_hcm
            route_config:
              name: remote_route
              virtual_hosts:
              - name: remote_service
                domains: ["*"]
                routes:
                - match: { connect_matcher: {} }
                  route:
                    cluster: cluster_proxy
                    upgrade_configs:
                    - upgrade_type: CONNECT
                      connect_config:
              response_headers_to_add:
                - append_action: OVERWRITE_IF_EXISTS_OR_ADD
                  header:
                    key: x-response-header-that-should-be-stripped
                    value: 'true'
            http_filters:
              - name: envoy.filters.http.local_error
                typed_config:
                  "@type": type.googleapis.com/envoymobile.extensions.filters.http.local_error.LocalError
              - name: envoy.filters.http.dynamic_forward_proxy
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig
                  dns_cache_config: &dns_cache_config
                    name: base_dns_cache
                    dns_lookup_family: ALL
                    host_ttl: 86400s
                    dns_min_refresh_rate: 20s
                    dns_refresh_rate: 60s
                    dns_failure_refresh_rate:
                      base_interval: 2s
                      max_interval: 10s
                    dns_query_timeout: 25s
                    typed_dns_resolver_config:
                      name: envoy.network.dns_resolver.getaddrinfo
                      typed_config: {"@type":"type.googleapis.com/envoy.extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig"}
              - name: envoy.router
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  clusters:
  - name: cluster_proxy
    connect_timeout: 30s
    lb_policy: CLUSTER_PROVIDED
    dns_lookup_family: ALL
    cluster_type:
      name: envoy.clusters.dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
        dns_cache_config: *dns_cache_config
layered_runtime:
  layers:
    - name: static_layer_0
      static_layer:
        envoy:
          # This disables envoy bug stats, which are filtered out of our stats inclusion list anyway
          # Global stats do not play well with engines with limited lifetimes
          disallow_global_stats: true
)EOF";

} // namespace Envoy
