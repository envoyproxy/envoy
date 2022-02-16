#include "quic_test_server.h"

#include "test/test_common/environment.h"

namespace Envoy {

Network::TransportSocketFactoryPtr QuicTestServer::createUpstreamTlsContext(
    testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>& factory_context) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  Extensions::TransportSockets::Tls::ContextManagerImpl context_manager{time_system_};
  const std::string yaml = absl::StrFormat(
      R"EOF(
common_tls_context:
  alpn_protocols: h3
  tls_certificates:
  - certificate_chain:
      filename: ../envoy/test/config/integration/certs/upstreamcert.pem
    private_key:
      filename: ../envoy/test/config/integration/certs/upstreamkey.pem
)EOF");
  TestUtility::loadFromYaml(yaml, tls_context);
  envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport quic_config;
  quic_config.mutable_downstream_tls_context()->MergeFrom(tls_context);

  std::vector<std::string> server_names;
  auto& config_factory = Config::Utility::getAndCheckFactoryByName<
      Server::Configuration::DownstreamTransportSocketConfigFactory>(
      "envoy.transport_sockets.quic");

  return config_factory.createTransportSocketFactory(quic_config, factory_context, server_names);
}

QuicTestServer::QuicTestServer()
    : api_(Api::createApiForTest(stats_store_, time_system_)),
      version_(Network::Address::IpVersion::v4), upstream_config_(time_system_), port_(0) {
  ON_CALL(factory_context_, api()).WillByDefault(testing::ReturnRef(*api_));
  ON_CALL(factory_context_, scope()).WillByDefault(testing::ReturnRef(stats_store_));
  upstream_config_.udp_fake_upstream_ = FakeUpstreamConfig::UdpConfig();
}

void QuicTestServer::startQuicTestServer() {
  ASSERT(!upstream_);
  // pre-setup: see https://github.com/envoyproxy/envoy/blob/main/test/test_runner.cc
  Logger::Context logging_state(spdlog::level::level_enum::err,
                                "[%Y-%m-%d %T.%e][%t][%l][%n] [%g:%#] %v", lock, false, false);
  // end pre-setup

  upstream_config_.upstream_protocol_ = Http::CodecType::HTTP3;

  Network::TransportSocketFactoryPtr factory = createUpstreamTlsContext(factory_context_);

  upstream_ =
      std::make_unique<AutonomousUpstream>(std::move(factory), port_, version_, upstream_config_);

  // see upstream address
  ENVOY_LOG_MISC(debug, "Upstream now listening on {}", upstream_->localAddress()->asString());
}

void QuicTestServer::shutdownQuicTestServer() {
  ASSERT(upstream_);
  upstream_.reset();
}

int QuicTestServer::getServerPort() {
  ASSERT(upstream_);
  return upstream_->localAddress()->ip()->port();
}
} // namespace Envoy
