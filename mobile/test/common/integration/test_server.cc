#include "test_server.h"

#include "source/extensions/transport_sockets/tls/context_config_impl.h"

#include "test/integration/server.h"
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
  ASSERT(upstream_);
  upstream_.reset();
}

int TestServer::getServerPort() {
  ASSERT(upstream_);
  return upstream_->localAddress()->ip()->port();
}

void TestServer::setHeadersAndData(absl::string_view header_key, absl::string_view header_value,
                                   absl::string_view response_body) {
  upstream_->setResponseHeaders(
      std::make_unique<Http::TestResponseHeaderMapImpl>(Http::TestResponseHeaderMapImpl(
          {{std::string(header_key), std::string(header_value)}, {":status", "200"}})));
  upstream_->setResponseBody(std::string(response_body));
}
} // namespace Envoy
