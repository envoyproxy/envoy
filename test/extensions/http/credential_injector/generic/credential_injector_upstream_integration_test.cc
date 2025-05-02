#include "envoy/extensions/filters/http/credential_injector/v3/credential_injector.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "test/integration/tcp_tunneling_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {
namespace {

class CredentialInjectorUpstreamIntegrationTest : public BaseTcpTunnelingIntegrationTest {};

INSTANTIATE_TEST_SUITE_P(
    Protocols, CredentialInjectorUpstreamIntegrationTest,
    testing::ValuesIn(BaseTcpTunnelingIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    BaseTcpTunnelingIntegrationTest::protocolTestParamsToString);

TEST_P(CredentialInjectorUpstreamIntegrationTest, InjectProxyAuthorizationHeader) {
  if (!(GetParam().tunneling_with_upstream_filters)) {
    return;
  }

  // add credential injector to cluster http_filters
  envoy::extensions::filters::http::credential_injector::v3::CredentialInjector
      credential_injector_config;
  TestUtility::loadFromYaml(R"EOF(
  credential:
    name: envoy.http.injected_credentials.generic
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.injected_credentials.generic.v3.Generic
      credential:
        name: proxy_authorization
      header: Proxy-Authorization
)EOF",
                            credential_injector_config);
  HttpFilterProto filter_config;
  filter_config.set_name("envoy.filters.http.credential_injector");
  filter_config.mutable_typed_config()->PackFrom(credential_injector_config);
  addHttpUpstreamFilterToCluster(filter_config);
  addHttpUpstreamFilterToCluster(getCodecFilterConfig());

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy proxy_config;
    proxy_config.set_stat_prefix("tcp_stats");
    proxy_config.set_cluster("cluster_0");
    proxy_config.mutable_tunneling_config()->set_hostname("foo.lyft.com:80");

    auto* listener = bootstrap.mutable_static_resources()->add_listeners();
    listener->set_name("tcp_proxy");

    auto* socket_address = listener->mutable_address()->mutable_socket_address();
    socket_address->set_address(Network::Test::getLoopbackAddressString(version_));
    socket_address->set_port_value(0);

    auto* filter_chain = listener->add_filter_chains();
    auto* filter = filter_chain->add_filters();
    filter->mutable_typed_config()->PackFrom(proxy_config);
    filter->set_name("envoy.filters.network.tcp_proxy");

    auto* secret = bootstrap.mutable_static_resources()->add_secrets();
    secret->set_name("proxy_authorization");
    auto* generic = secret->mutable_generic_secret();
    generic->mutable_secret()->set_inline_string("Basic base64EncodedUsernamePassword");
  });
  initialize();

  setUpConnection(fake_upstream_connection_);
  sendBidiData(fake_upstream_connection_);
  EXPECT_EQ("Basic base64EncodedUsernamePassword",
            upstream_request_->headers()
                .get(Http::LowerCaseString("Proxy-Authorization"))[0]
                ->value()
                .getStringView());
  closeConnection(fake_upstream_connection_);
}

} // namespace
} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
