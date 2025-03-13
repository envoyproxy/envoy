#include "test/integration/ssl_utility.h"

#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"

#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/common/network/utility.h"
#include "source/common/tls/client_ssl_socket.h"
#include "source/common/tls/context_manager_impl.h"
#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_ssl_socket.h"

#include "test/config/utility.h"
#include "test/integration/server.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {
namespace Ssl {

void initializeUpstreamTlsContextConfig(
    const ClientSslTransportOptions& options,
    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext& tls_context,
    bool connect_to_upstream) {
  const std::string rundir = TestEnvironment::runfilesDirectory();
  if (connect_to_upstream) {
    tls_context.mutable_common_tls_context()
        ->mutable_validation_context()
        ->mutable_trusted_ca()
        ->set_filename(rundir + "/test/config/integration/certs/upstreamcacert.pem");
    tls_context.set_sni("foo.lyft.com");
    return;
  }

  tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->mutable_trusted_ca()
      ->set_filename(rundir + "/test/config/integration/certs/cacert.pem");
  auto* certs = tls_context.mutable_common_tls_context()->add_tls_certificates();
  std::string chain;
  std::string key;
  if (options.client_ecdsa_cert_) {
    chain = rundir + "/test/config/integration/certs/client_ecdsacert.pem";
    key = rundir + "/test/config/integration/certs/client_ecdsakey.pem";
  } else if (options.use_expired_spiffe_cert_) {
    chain = rundir + "/test/common/tls/test_data/expired_spiffe_san_cert.pem";
    key = rundir + "/test/common/tls/test_data/expired_spiffe_san_key.pem";
  } else if (options.client_with_intermediate_cert_) {
    chain = rundir + "/test/config/integration/certs/client2_chain.pem";
    key = rundir + "/test/config/integration/certs/client2key.pem";
  } else {
    chain = rundir + "/test/config/integration/certs/clientcert.pem";
    key = rundir + "/test/config/integration/certs/clientkey.pem";
  }
  certs->mutable_certificate_chain()->set_filename(chain);
  certs->mutable_private_key()->set_filename(key);

  auto* common_context = tls_context.mutable_common_tls_context();

  if (options.alpn_) {
    common_context->add_alpn_protocols(Http::Utility::AlpnNames::get().Http2);
    common_context->add_alpn_protocols(Http::Utility::AlpnNames::get().Http11);
    common_context->add_alpn_protocols(Http::Utility::AlpnNames::get().Http3);
  }
  if (!options.san_.empty()) {
    envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher* matcher =
        common_context->mutable_validation_context()->add_match_typed_subject_alt_names();
    matcher->mutable_matcher()->set_exact(options.san_);
    matcher->set_san_type(
        envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::DNS);
    matcher = common_context->mutable_validation_context()->add_match_typed_subject_alt_names();
    matcher->mutable_matcher()->set_exact(options.san_);
    matcher->set_san_type(
        envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::URI);
    matcher = common_context->mutable_validation_context()->add_match_typed_subject_alt_names();
    matcher->mutable_matcher()->set_exact(options.san_);
    matcher->set_san_type(
        envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::EMAIL);
    matcher = common_context->mutable_validation_context()->add_match_typed_subject_alt_names();
    matcher->mutable_matcher()->set_exact(options.san_);
    matcher->set_san_type(
        envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::IP_ADDRESS);
  }
  for (const std::string& cipher_suite : options.cipher_suites_) {
    common_context->mutable_tls_params()->add_cipher_suites(cipher_suite);
  }
  for (const std::string& algorithm : options.sigalgs_) {
    common_context->mutable_tls_params()->add_signature_algorithms(algorithm);
  }
  for (const std::string& curve : options.curves_) {
    common_context->mutable_tls_params()->add_ecdh_curves(curve);
  }
  if (!options.sni_.empty()) {
    tls_context.set_sni(options.sni_);
  }
  if (options.custom_validator_config_) {
    *common_context->mutable_validation_context()->mutable_custom_validator_config() =
        *options.custom_validator_config_;
  }

  common_context->mutable_tls_params()->set_tls_minimum_protocol_version(options.tls_version_);
  common_context->mutable_tls_params()->set_tls_maximum_protocol_version(options.tls_version_);
}

Network::UpstreamTransportSocketFactoryPtr
createClientSslTransportSocketFactory(const ClientSslTransportOptions& options,
                                      ContextManager& context_manager, Api::Api& api) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  initializeUpstreamTlsContextConfig(options, tls_context);

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
  ON_CALL(mock_factory_ctx.server_context_, api()).WillByDefault(ReturnRef(api));
  auto cfg = *Extensions::TransportSockets::Tls::ClientContextConfigImpl::create(tls_context,
                                                                                 mock_factory_ctx);
  static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
  return Network::UpstreamTransportSocketFactoryPtr{
      THROW_OR_RETURN_VALUE(Extensions::TransportSockets::Tls::ClientSslSocketFactory::create(
                                std::move(cfg), context_manager, *client_stats_store->rootScope()),
                            Network::UpstreamTransportSocketFactoryPtr)};
}

Network::DownstreamTransportSocketFactoryPtr
createUpstreamSslContext(ContextManager& context_manager, Api::Api& api, bool use_http3) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  ConfigHelper::initializeTls({}, *tls_context.mutable_common_tls_context(), use_http3);

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
  ON_CALL(mock_factory_ctx.server_context_, api()).WillByDefault(ReturnRef(api));
  auto cfg = *Extensions::TransportSockets::Tls::ServerContextConfigImpl::create(
      tls_context, mock_factory_ctx, false);

  static auto* upstream_stats_store = new Stats::TestIsolatedStoreImpl();
  if (!use_http3) {
    return *Extensions::TransportSockets::Tls::ServerSslSocketFactory::create(
        std::move(cfg), context_manager, *upstream_stats_store->rootScope(),
        std::vector<std::string>{});
  }
  envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport quic_config;
  quic_config.mutable_downstream_tls_context()->MergeFrom(tls_context);
  ON_CALL(mock_factory_ctx, statsScope())
      .WillByDefault(ReturnRef(*upstream_stats_store->rootScope()));
  ON_CALL(mock_factory_ctx, sslContextManager()).WillByDefault(ReturnRef(context_manager));

  std::vector<std::string> server_names;
  auto& config_factory = Config::Utility::getAndCheckFactoryByName<
      Server::Configuration::DownstreamTransportSocketConfigFactory>(
      "envoy.transport_sockets.quic");
  return *config_factory.createTransportSocketFactory(quic_config, mock_factory_ctx, server_names);
}

Network::DownstreamTransportSocketFactoryPtr createFakeUpstreamSslContext(
    const std::string& upstream_cert_name, ContextManager& context_manager,
    Server::Configuration::TransportSocketFactoryContext& factory_context) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  auto* common_tls_context = tls_context.mutable_common_tls_context();
  auto* tls_cert = common_tls_context->add_tls_certificates();
  tls_cert->mutable_certificate_chain()->set_filename(TestEnvironment::runfilesPath(
      fmt::format("test/config/integration/certs/{}cert.pem", upstream_cert_name)));
  tls_cert->mutable_private_key()->set_filename(TestEnvironment::runfilesPath(
      fmt::format("test/config/integration/certs/{}key.pem", upstream_cert_name)));

  auto cfg = *Extensions::TransportSockets::Tls::ServerContextConfigImpl::create(
      tls_context, factory_context, false);

  static auto* upstream_stats_store = new Stats::IsolatedStoreImpl();
  return *Extensions::TransportSockets::Tls::ServerSslSocketFactory::create(
      std::move(cfg), context_manager, *upstream_stats_store->rootScope(),
      std::vector<std::string>{});
}
Network::Address::InstanceConstSharedPtr getSslAddress(const Network::Address::IpVersion& version,
                                                       int port) {
  std::string url =
      "tcp://" + Network::Test::getLoopbackAddressUrlString(version) + ":" + std::to_string(port);
  return *Network::Utility::resolveUrl(url);
}

} // namespace Ssl
} // namespace Envoy
