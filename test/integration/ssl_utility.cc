#include "test/integration/ssl_utility.h"

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "common/json/json_loader.h"
#include "common/network/utility.h"

#include "extensions/transport_sockets/tls/context_config_impl.h"
#include "extensions/transport_sockets/tls/context_manager_impl.h"
#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "test/config/utility.h"
#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {
namespace Ssl {

Network::TransportSocketFactoryPtr
createClientSslTransportSocketFactory(const ClientSslTransportOptions& options,
                                      ContextManager& context_manager, Api::Api& api) {
  std::string yaml_plain = R"EOF(
  common_tls_context:
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
)EOF";
  if (options.client_ecdsa_cert_) {
    yaml_plain += R"EOF(
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/config/integration/certs/client_ecdsacert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/config/integration/certs/client_ecdsakey.pem"
)EOF";
  } else {
    yaml_plain += R"EOF(
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/config/integration/certs/clientcert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/config/integration/certs/clientkey.pem"
)EOF";
  }

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml_plain), tls_context);
  auto* common_context = tls_context.mutable_common_tls_context();

  if (options.alpn_) {
    common_context->add_alpn_protocols("h2");
    common_context->add_alpn_protocols("http/1.1");
  }
  if (options.san_) {
    common_context->mutable_validation_context()
        ->add_hidden_envoy_deprecated_verify_subject_alt_name("spiffe://lyft.com/backend-team");
  }
  for (const std::string& cipher_suite : options.cipher_suites_) {
    common_context->mutable_tls_params()->add_cipher_suites(cipher_suite);
  }

  common_context->mutable_tls_params()->set_tls_minimum_protocol_version(options.tls_version_);
  common_context->mutable_tls_params()->set_tls_maximum_protocol_version(options.tls_version_);

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
  ON_CALL(mock_factory_ctx, api()).WillByDefault(ReturnRef(api));
  auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ClientContextConfigImpl>(
      tls_context, options.sigalgs_, mock_factory_ctx);
  static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
  return Network::TransportSocketFactoryPtr{
      new Extensions::TransportSockets::Tls::ClientSslSocketFactory(std::move(cfg), context_manager,
                                                                    *client_stats_store)};
}

Network::TransportSocketFactoryPtr createUpstreamSslContext(ContextManager& context_manager,
                                                            Api::Api& api) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  ConfigHelper::initializeTls({}, *tls_context.mutable_common_tls_context());

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
  ON_CALL(mock_factory_ctx, api()).WillByDefault(ReturnRef(api));
  auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
      tls_context, mock_factory_ctx);

  static Stats::Scope* upstream_stats_store = new Stats::TestIsolatedStoreImpl();
  return std::make_unique<Extensions::TransportSockets::Tls::ServerSslSocketFactory>(
      std::move(cfg), context_manager, *upstream_stats_store, std::vector<std::string>{});
}

Network::Address::InstanceConstSharedPtr getSslAddress(const Network::Address::IpVersion& version,
                                                       int port) {
  std::string url =
      "tcp://" + Network::Test::getLoopbackAddressUrlString(version) + ":" + std::to_string(port);
  return Network::Utility::resolveUrl(url);
}

} // namespace Ssl
} // namespace Envoy
