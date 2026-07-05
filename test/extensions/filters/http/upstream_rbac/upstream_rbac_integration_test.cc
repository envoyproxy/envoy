#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/http/upstream_codec/v3/upstream_codec.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/rbac/matchers/upstream_ip_port/v3/upstream_ip_port_matcher.pb.h"

#include "test/config/utility.h"
#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

using HttpFilterProto =
    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter;

class UpstreamRbacIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public HttpIntegrationTest {
public:
  UpstreamRbacIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    skip_tag_extraction_rule_check_ = true;
  }

  // Adds an upstream RBAC filter (followed by the upstream codec filter) to the cluster, denying
  // requests whose selected upstream IP falls within the provided CIDR.
  void addUpstreamRbacFilter(const std::string& address_prefix, uint32_t prefix_len) {
    envoy::extensions::filters::http::rbac::v3::RBAC rbac;
    rbac.mutable_rules()->set_action(envoy::config::rbac::v3::RBAC::DENY);

    envoy::config::rbac::v3::Policy policy;
    envoy::extensions::rbac::matchers::upstream_ip_port::v3::UpstreamIpPortMatcher matcher;
    matcher.mutable_upstream_ip()->set_address_prefix(address_prefix);
    matcher.mutable_upstream_ip()->mutable_prefix_len()->set_value(prefix_len);
    auto* matcher_ext = policy.add_permissions()->mutable_matcher();
    matcher_ext->set_name("envoy.rbac.matchers.upstream_ip_port");
    std::ignore = matcher_ext->mutable_typed_config()->PackFrom(matcher);
    policy.add_principals()->set_any(true);
    (*rbac.mutable_rules()->mutable_policies())["deny-upstream"] = policy;

    HttpFilterProto rbac_filter;
    rbac_filter.set_name("envoy.filters.http.upstream_rbac");
    std::ignore = rbac_filter.mutable_typed_config()->PackFrom(rbac);

    HttpFilterProto codec_filter;
    codec_filter.set_name("envoy.filters.http.upstream_codec");
    std::ignore = codec_filter.mutable_typed_config()->PackFrom(
        envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec());

    config_helper_.addConfigModifier(
        [rbac_filter, codec_filter](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
          ConfigHelper::HttpProtocolOptions protocol_options =
              MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
                  (*cluster->mutable_typed_extension_protocol_options())
                      ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
          *protocol_options.add_http_filters() = rbac_filter;
          *protocol_options.add_http_filters() = codec_filter;
          std::ignore = (*cluster->mutable_typed_extension_protocol_options())
                            ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
                                .PackFrom(protocol_options);
        });
  }

  std::string loopbackCidr() {
    return version_ == Network::Address::IpVersion::v4 ? "127.0.0.1" : "::1";
  }

  uint32_t loopbackPrefixLen() { return version_ == Network::Address::IpVersion::v4 ? 8 : 128; }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, UpstreamRbacIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// A request to an upstream whose IP matches the deny rule is rejected with a 403 and no upstream
// connection is established.
TEST_P(UpstreamRbacIntegrationTest, DeniesMatchingUpstreamIp) {
  addUpstreamRbacFilter(loopbackCidr(), loopbackPrefixLen());
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("403", response->headers().getStatusValue());
  // No upstream connection should have been established since the request was denied during host
  // selection, before the connection was initiated.
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_total")->value());

  cleanupUpstreamAndDownstream();
}

// A request to an upstream whose IP does not match the deny rule is forwarded normally.
TEST_P(UpstreamRbacIntegrationTest, AllowsNonMatchingUpstreamIp) {
  // Deny a CIDR that the loopback upstream address will not fall into.
  addUpstreamRbacFilter("10.255.255.255", 32);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace Envoy
