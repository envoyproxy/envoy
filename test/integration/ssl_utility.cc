#include "test/integration/ssl_utility.h"

#include "common/json/json_loader.h"
#include "common/network/utility.h"
#include "common/ssl/context_config_impl.h"
#include "common/ssl/context_manager_impl.h"
#include "common/ssl/ssl_socket.h"

#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

namespace Envoy {
namespace Ssl {

Network::TransportSocketFactoryPtr
createClientSslTransportSocketFactory(bool alpn, bool san, ContextManager& context_manager) {
  const std::string json_plain = R"EOF(
{
  "ca_cert_file": "{{ test_rundir }}/test/config/integration/certs/cacert.pem",
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/clientcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/clientkey.pem"
}
)EOF";

  const std::string json_alpn = R"EOF(
{
  "ca_cert_file": "{{ test_rundir }}/test/config/integration/certs/cacert.pem",
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/clientcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/clientkey.pem",
  "alpn_protocols": "h2,http/1.1"
}
)EOF";

  const std::string json_san = R"EOF(
{
  "ca_cert_file": "{{ test_rundir }}/test/config/integration/certs/cacert.pem",
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/clientcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/clientkey.pem",
  "verify_subject_alt_name": [ "spiffe://lyft.com/backend-team" ]
}
)EOF";

  const std::string json_alpn_san = R"EOF(
{
  "ca_cert_file": "{{ test_rundir }}/test/config/integration/certs/cacert.pem",
  "cert_chain_file": "{{ test_rundir }}/test/config/integration/certs/clientcert.pem",
  "private_key_file": "{{ test_rundir }}/test/config/integration/certs/clientkey.pem",
  "alpn_protocols": "h2,http/1.1",
  "verify_subject_alt_name": [ "spiffe://lyft.com/backend-team" ]
}
)EOF";

  std::string target;
  if (alpn) {
    target = san ? json_alpn_san : json_alpn;
  } else {
    target = san ? json_san : json_plain;
  }
  Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(target);
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
  auto cfg = std::make_unique<ClientContextConfigImpl>(*loader, mock_factory_ctx);
  static auto* client_stats_store = new Stats::TestIsolatedStoreImpl();
  return Network::TransportSocketFactoryPtr{
      new Ssl::ClientSslSocketFactory(std::move(cfg), context_manager, *client_stats_store)};
}

Network::TransportSocketFactoryPtr createUpstreamSslContext(ContextManager& context_manager) {
  envoy::api::v2::auth::DownstreamTlsContext tls_context;
  auto* common_tls_context = tls_context.mutable_common_tls_context();
  common_tls_context->add_alpn_protocols("h2");
  common_tls_context->add_alpn_protocols("http/1.1");
  common_tls_context->mutable_deprecated_v1()->set_alt_alpn_protocols("http/1.1");

  auto* validation_context = common_tls_context->mutable_validation_context();
  validation_context->mutable_trusted_ca()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
  validation_context->add_verify_certificate_hash(
      "E0:F3:C8:CE:5E:2E:A3:05:F0:70:1F:F5:12:E3:6E:2E:"
      "97:92:82:84:A2:28:BC:F7:73:32:D3:39:30:A1:B6:FD");

  auto* tls_certificate = common_tls_context->add_tls_certificates();
  tls_certificate->mutable_certificate_chain()->set_filename(
      TestEnvironment::runfilesPath("/test/config/integration/certs/servercert.pem"));
  tls_certificate->mutable_private_key()->set_filename(
      TestEnvironment::runfilesPath("/test/config/integration/certs/serverkey.pem"));

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
