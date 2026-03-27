#include <memory>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/listener/set_filter_state/v3/set_filter_state.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_selectors/filter_state/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "test/integration/ssl_utility.h"
#include "test/integration/tcp_proxy_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace FilterState {
namespace {

class FilterStateCertIntegrationTest : public BaseTcpProxySslIntegrationTest,
                                       public testing::TestWithParam<Network::Address::IpVersion> {
public:
  FilterStateCertIntegrationTest() : BaseTcpProxySslIntegrationTest(GetParam()) {}

  void setup() {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->mutable_filter_chains(0);

      // Read PEM cert and key from test files.
      const std::string cert_pem = TestEnvironment::readFileToStringForTest(
          TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"));
      const std::string key_pem = TestEnvironment::readFileToStringForTest(
          TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));

      // Add set_filter_state listener filter to inject PEM into filter state.
      auto* lf = listener->add_listener_filters();
      lf->set_name("envoy.filters.listener.set_filter_state");
      envoy::extensions::filters::listener::set_filter_state::v3::Config set_filter_state_config;
      {
        auto* entry = set_filter_state_config.add_on_new_connection();
        entry->set_object_key("envoy.tls.certificate.cert_chain");
        entry->set_factory_key("envoy.string");
        entry->mutable_format_string()->mutable_text_format_source()->set_inline_string(cert_pem);
      }
      {
        auto* entry = set_filter_state_config.add_on_new_connection();
        entry->set_object_key("envoy.tls.certificate.private_key");
        entry->set_factory_key("envoy.string");
        entry->mutable_format_string()->mutable_text_format_source()->set_inline_string(key_pem);
      }
      lf->mutable_typed_config()->PackFrom(set_filter_state_config);

      // Configure downstream TLS with filter_state cert selector.
      auto* transport_socket = filter_chain->mutable_transport_socket();
      transport_socket->set_name("envoy.transport_sockets.tls");
      envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
      auto* common = tls_context.mutable_common_tls_context();
      common->add_alpn_protocols(Http::Utility::AlpnNames::get().Http11);

      // Validation context for client certs.
      auto* validation_context = common->mutable_validation_context();
      validation_context->mutable_trusted_ca()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
      validation_context->add_verify_certificate_hash(TEST_CLIENT_CERT_HASH);

      // Configure the filter_state cert selector with SNI mapper.
      envoy::extensions::transport_sockets::tls::cert_selectors::filter_state::v3::Config
          selector_config;
      auto* mapper = selector_config.mutable_certificate_mapper();
      mapper->set_name("envoy.tls.certificate_mappers.sni");
      envoy::extensions::transport_sockets::tls::cert_mappers::sni::v3::SNI sni_config;
      sni_config.set_default_value("fallback");
      mapper->mutable_typed_config()->PackFrom(sni_config);

      common->mutable_custom_tls_certificate_selector()->set_name(
          "envoy.tls.certificate_selectors.filter_state");
      common->mutable_custom_tls_certificate_selector()->mutable_typed_config()->PackFrom(
          selector_config);

      tls_context.set_disable_stateless_session_resumption(true);
      tls_context.set_disable_stateful_session_resumption(true);
      transport_socket->mutable_typed_config()->PackFrom(tls_context);
    });
    BaseTcpProxySslIntegrationTest::initialize();
    test_server_->waitUntilListenersReady();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, FilterStateCertIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(FilterStateCertIntegrationTest, BasicSuccess) {
  setup();
  auto conn = std::make_unique<ClientSslConnection>(*this);
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
}

TEST_P(FilterStateCertIntegrationTest, CacheHitOnSecondConnection) {
  setup();
  // First connection — cache miss, creates DynamicContext.
  {
    auto conn = std::make_unique<ClientSslConnection>(*this);
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
    conn.reset();
  }
  // Second connection — cache hit, reuses cached DynamicContext.
  {
    auto conn = std::make_unique<ClientSslConnection>(*this);
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello2", "world2");
    conn.reset();
  }
}

} // namespace
} // namespace FilterState
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
