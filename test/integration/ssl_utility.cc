#include "test/integration/ssl_utility.h"

#include "common/json/json_loader.h"
#include "common/network/utility.h"
#include "common/ssl/context_config_impl.h"
#include "common/ssl/context_manager_impl.h"
#include "common/ssl/ssl_socket.h"

#include "test/config/utility.h"
#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

namespace Envoy {
namespace Ssl {

Network::TransportSocketFactoryPtr
createClientSslTransportSocketFactory(const ClientSslTransportOptions& options,
                                      ContextManager& context_manager) {
  const std::string yaml_plain = R"EOF(
  common_tls_context:
    tls_certificates:
      certificate_chain:
        filename: "{{ test_rundir }}/test/config/integration/certs/clientcert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/config/integration/certs/clientkey.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
)EOF";

  envoy::api::v2::auth::UpstreamTlsContext tls_context;
  MessageUtil::loadFromYaml(TestEnvironment::substitute(yaml_plain), tls_context);
  auto* common_context = tls_context.mutable_common_tls_context();

  if (options.alpn_) {
    common_context->add_alpn_protocols("h2");
    common_context->add_alpn_protocols("http/1.1");
  }
  if (options.san_) {
    common_context->mutable_validation_context()->add_verify_subject_alt_name(
        "spiffe://lyft.com/backend-team");
  }
  for (const std::string& cipher_suite : options.cipher_suites_) {
    common_context->mutable_tls_params()->add_cipher_suites(cipher_suite);
  }

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
  auto cfg = std::make_unique<ClientContextConfigImpl>(tls_context, mock_factory_ctx);
  static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
  return Network::TransportSocketFactoryPtr{
      new Ssl::ClientSslSocketFactory(std::move(cfg), context_manager, *client_stats_store)};
}

Network::TransportSocketFactoryPtr createUpstreamSslContext(ContextManager& context_manager) {
  envoy::api::v2::auth::DownstreamTlsContext tls_context;
  ConfigHelper::initializeTls(false, *tls_context.mutable_common_tls_context());

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
  auto cfg = std::make_unique<Ssl::ServerContextConfigImpl>(tls_context, mock_factory_ctx);

  static Stats::Scope* upstream_stats_store = new Stats::TestIsolatedStoreImpl();
  return std::make_unique<Ssl::ServerSslSocketFactory>(
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
