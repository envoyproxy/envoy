#include "test/common/integration/test_server.h"

#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.h"
#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"
#include "envoy/extensions/network/dns_resolver/getaddrinfo/v3/getaddrinfo_dns_resolver.pb.h"

#include "source/common/listener_manager/connection_handler_impl.h"
#include "source/common/listener_manager/listener_manager_impl.h"
#include "source/common/quic/quic_server_transport_socket_factory.h"
#include "source/common/quic/server_codec_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_context_impl.h"
#include "source/extensions/quic/connection_id_generator/envoy_deterministic_connection_id_generator_config.h"
#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_server_stream.h"
#include "source/extensions/quic/proof_source/envoy_quic_proof_source_factory_impl.h"
#include "source/extensions/transport_sockets/raw_buffer/config.h"
#include "source/extensions/udp_packet_writer/default/config.h"
#include "source/server/hot_restart_nop_impl.h"
#include "source/server/instance_impl.h"

#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

#include "extension_registry.h"
#include "library/common/engine_common.h"

namespace Envoy {
namespace {

std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap>
baseProxyConfig(Network::Address::IpVersion version, bool http, int port) {
  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> bootstrap =
      std::make_unique<envoy::config::bootstrap::v3::Bootstrap>();

  auto* static_resources = bootstrap->mutable_static_resources();

  auto* listener = static_resources->add_listeners();
  listener->set_name("base_api_listener");
  auto* base_address = listener->mutable_address();
  base_address->mutable_socket_address()->set_protocol(envoy::config::core::v3::SocketAddress::TCP);
  base_address->mutable_socket_address()->set_address(
      version == Network::Address::IpVersion::v4 ? "127.0.0.1" : "::1");
  base_address->mutable_socket_address()->set_port_value(port);

  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager hcm;
  hcm.set_stat_prefix("remote hcm");
  auto* route_config = hcm.mutable_route_config();
  route_config->set_name("remote_route");
  auto* remote_service = route_config->add_virtual_hosts();
  remote_service->set_name("remote_service");
  remote_service->add_domains("*");
  auto* route = remote_service->add_routes();
  route->mutable_route()->set_cluster("cluster_proxy");
  if (http) {
    route->mutable_match()->set_prefix("/");
  } else {
    route->mutable_match()->mutable_connect_matcher();
    auto* upgrade_config = route->mutable_route()->add_upgrade_configs();
    upgrade_config->set_upgrade_type("CONNECT");
    upgrade_config->mutable_connect_config();
  }

  auto* header_value_option = route->mutable_response_headers_to_add()->Add();
  header_value_option->set_append_action(
      envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
  header_value_option->mutable_header()->set_value("true");
  if (http) {
    header_value_option->mutable_header()->set_key("x-proxy-response");
  } else {
    header_value_option->mutable_header()->set_key("x-response-header-that-should-be-stripped");
  }

  envoy::extensions::filters::http::dynamic_forward_proxy::v3::FilterConfig dfp_config;
  auto* dns_cache_config = dfp_config.mutable_dns_cache_config();
  dns_cache_config->set_name("base_dns_cache");
  dns_cache_config->set_dns_lookup_family(envoy::config::cluster::v3::Cluster::ALL);
  dns_cache_config->mutable_host_ttl()->set_seconds(86400);
  dns_cache_config->mutable_dns_min_refresh_rate()->set_seconds(20);
  dns_cache_config->mutable_dns_refresh_rate()->set_seconds(60);
  dns_cache_config->mutable_dns_failure_refresh_rate()->mutable_base_interval()->set_seconds(2);
  dns_cache_config->mutable_dns_failure_refresh_rate()->mutable_max_interval()->set_seconds(10);
  dns_cache_config->mutable_dns_query_timeout()->set_seconds(25);

  envoy::extensions::network::dns_resolver::getaddrinfo::v3::GetAddrInfoDnsResolverConfig
      resolver_config;
  dns_cache_config->mutable_typed_dns_resolver_config()->set_name(
      "envoy.network.dns_resolver.getaddrinfo");
  dns_cache_config->mutable_typed_dns_resolver_config()->mutable_typed_config()->PackFrom(
      resolver_config);

  auto* dfp_filter = hcm.add_http_filters();
  dfp_filter->set_name("envoy.filters.http.dynamic_forward_proxy");
  dfp_filter->mutable_typed_config()->PackFrom(dfp_config);

  auto* router_filter = hcm.add_http_filters();
  envoy::extensions::filters::http::router::v3::Router router_config;
  router_filter->set_name("envoy.router");
  router_filter->mutable_typed_config()->PackFrom(router_config);

  envoy::config::listener::v3::FilterChain* filter_chain = listener->add_filter_chains();
  auto* filter = filter_chain->add_filters();
  filter->set_name("envoy.filters.network.http_connection_manager");
  filter->mutable_typed_config()->PackFrom(hcm);

  // Base cluster config (DFP cluster config)
  auto* base_cluster = static_resources->add_clusters();
  base_cluster->set_name("cluster_proxy");
  base_cluster->mutable_connect_timeout()->set_seconds(30);
  base_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
  envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig base_cluster_config;
  envoy::config::cluster::v3::Cluster::CustomClusterType base_cluster_type;
  base_cluster_config.mutable_dns_cache_config()->CopyFrom(*dns_cache_config);
  base_cluster_type.set_name("envoy.clusters.dynamic_forward_proxy");
  base_cluster_type.mutable_typed_config()->PackFrom(base_cluster_config);
  base_cluster->mutable_cluster_type()->CopyFrom(base_cluster_type);

  return bootstrap;
}

} // namespace

TestServer::TestServer()
    : api_(Api::createApiForTest(stats_store_, time_system_)),
      version_(TestEnvironment::getIpVersionsForTest()[0]), upstream_config_(time_system_),
      port_(0) {
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
  ON_CALL(factory_context_, sslContextManager())
      .WillByDefault(testing::ReturnRef(context_manager_));

  Envoy::ExtensionRegistry::registerFactories();
}

void TestServer::start(TestServerType type, int port) {
  port_ = port;
  ASSERT(!upstream_);
  // pre-setup: see https://github.com/envoyproxy/envoy/blob/main/test/test_runner.cc
  Logger::Context logging_state(spdlog::level::level_enum::err,
                                "[%Y-%m-%d %T.%e][%t][%l][%n] [%g:%#] %v", lock_, false, false);
  // end pre-setup
  Network::DownstreamTransportSocketFactoryPtr factory;

  Extensions::TransportSockets::Tls::forceRegisterServerContextFactoryImpl();
  switch (type) {
  case TestServerType::HTTP3:
    // Make sure if extensions aren't statically linked QUIC will work.
    Quic::forceRegisterQuicServerTransportSocketConfigFactory();
    Network::forceRegisterUdpDefaultWriterFactoryFactory();
    Quic::forceRegisterQuicHttpServerConnectionFactoryImpl();
    Quic::forceRegisterEnvoyQuicCryptoServerStreamFactoryImpl();
    Quic::forceRegisterQuicServerTransportSocketConfigFactory();
    Quic::forceRegisterEnvoyQuicProofSourceFactoryImpl();
    Quic::forceRegisterEnvoyDeterministicConnectionIdGeneratorConfigFactory();

    // envoy.quic.crypto_stream.server.quiche
    upstream_config_.upstream_protocol_ = Http::CodecType::HTTP3;
    upstream_config_.udp_fake_upstream_ = FakeUpstreamConfig::UdpConfig();
    factory = createQuicUpstreamTlsContext(factory_context_);
    break;
  case TestServerType::HTTP2_WITH_TLS:
    upstream_config_.upstream_protocol_ = Http::CodecType::HTTP2;
    factory = createUpstreamTlsContext(factory_context_, /* add_alpn= */ true);
    break;
  case TestServerType::HTTP1_WITHOUT_TLS:
    upstream_config_.upstream_protocol_ = Http::CodecType::HTTP1;
    factory = Network::Test::createRawBufferDownstreamSocketFactory();
    break;
  case TestServerType::HTTP1_WITH_TLS:
    upstream_config_.upstream_protocol_ = Http::CodecType::HTTP1;
    factory = createUpstreamTlsContext(factory_context_, /* add_alpn= */ false);
    break;
  case TestServerType::HTTP_PROXY: {
    Server::forceRegisterDefaultListenerManagerFactoryImpl();
    Extensions::TransportSockets::RawBuffer::forceRegisterDownstreamRawBufferSocketFactory();
    Server::forceRegisterConnectionHandlerFactoryImpl();
#if !defined(ENVOY_ENABLE_FULL_PROTOS)
    registerMobileProtoDescriptors();
#endif
    test_server_ = IntegrationTestServer::create(
        "", version_, nullptr, nullptr, {}, time_system_, *api_, false, absl::nullopt,
        Server::FieldValidationConfig(), 1, std::chrono::seconds(1), Server::DrainStrategy::Gradual,
        nullptr, false, false, baseProxyConfig(version_, true, port_));
    test_server_->waitUntilListenersReady();
    ENVOY_LOG_MISC(debug, "Http proxy is now running");
    return;
  }
  case TestServerType::HTTPS_PROXY: {
    Server::forceRegisterDefaultListenerManagerFactoryImpl();
    Extensions::TransportSockets::RawBuffer::forceRegisterDownstreamRawBufferSocketFactory();
    Server::forceRegisterConnectionHandlerFactoryImpl();
#if !defined(ENVOY_ENABLE_FULL_PROTOS)
    registerMobileProtoDescriptors();
#endif
    test_server_ = IntegrationTestServer::create(
        "", version_, nullptr, nullptr, {}, time_system_, *api_, false, absl::nullopt,
        Server::FieldValidationConfig(), 1, std::chrono::seconds(1), Server::DrainStrategy::Gradual,
        nullptr, false, false, baseProxyConfig(version_, false, port_));
    test_server_->waitUntilListenersReady();
    ENVOY_LOG_MISC(debug, "Https proxy is now running");
    return;
  }
  }

// We have series of Cronvoy tests which don't bind to port 0, and often hit
// port conflicts with other processes using 127.0.0.1. Default non-apple
// builds to 127.0.0.1 (this fails on iOS and probably OSX with Can't assign
// requested address)
#if !defined(__APPLE__)
  if (version_ == Network::Address::IpVersion::v4) {
#else
  if (false) {
#endif
    auto address = Network::Utility::parseInternetAddressNoThrow("127.0.0.3", port_);
    upstream_ =
        std::make_unique<AutonomousUpstream>(std::move(factory), address, upstream_config_, true);
  } else {
    upstream_ = std::make_unique<AutonomousUpstream>(std::move(factory), port_, version_,
                                                     upstream_config_, true);
  }

  // Legacy behavior for cronet tests.
  if (type == TestServerType::HTTP3) {
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

void TestServer::shutdown() {
  ASSERT(upstream_ || test_server_);
  upstream_.reset();
  test_server_.reset();
}

std::string TestServer::getAddress() const {
  ASSERT(upstream_);
  return upstream_->localAddress()->asString();
}

std::string TestServer::getIpAddress() const {
  if (upstream_) {
    return upstream_->localAddress()->ip()->addressAsString();
  }
  // Return the proxy server IP address.
  ASSERT(test_server_);
  return version_ == Network::Address::IpVersion::v4 ? "127.0.0.1" : "::1";
}

int TestServer::getPort() const {
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

void TestServer::setResponse(const absl::flat_hash_map<std::string, std::string>& headers,
                             absl::string_view body,
                             const absl::flat_hash_map<std::string, std::string>& trailers) {
  ASSERT(upstream_);
  Http::TestResponseHeaderMapImpl new_headers;
  for (const auto& [key, value] : headers) {
    new_headers.addCopy(key, value);
  }
  new_headers.addCopy(":status", "200");
  upstream_->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(new_headers));
  upstream_->setResponseBody(std::string(body));
  Http::TestResponseTrailerMapImpl new_trailers;
  for (const auto& [key, value] : trailers) {
    new_trailers.addCopy(key, value);
  }
  upstream_->setResponseTrailers(std::make_unique<Http::TestResponseTrailerMapImpl>(new_trailers));
}

Network::DownstreamTransportSocketFactoryPtr TestServer::createQuicUpstreamTlsContext(
    testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>& factory_context) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  Extensions::TransportSockets::Tls::ContextManagerImpl context_manager{server_factory_context_};
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

  return *config_factory.createTransportSocketFactory(quic_config, factory_context, server_names);
}

Network::DownstreamTransportSocketFactoryPtr TestServer::createUpstreamTlsContext(
    testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>& factory_context,
    bool add_alpn) {
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
  if (add_alpn) {
    tls_context.mutable_common_tls_context()->add_alpn_protocols("h2");
  }
  auto cfg = *Extensions::TransportSockets::Tls::ServerContextConfigImpl::create(
      tls_context, factory_context, false);
  static auto* upstream_stats_store = new Stats::TestIsolatedStoreImpl();
  return *Extensions::TransportSockets::Tls::ServerSslSocketFactory::create(
      std::move(cfg), context_manager_, *upstream_stats_store->rootScope(),
      std::vector<std::string>{});
}

} // namespace Envoy
