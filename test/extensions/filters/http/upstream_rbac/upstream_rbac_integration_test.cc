#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/http/upstream_codec/v3/upstream_codec.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/rbac/matchers/upstream_ip_port/v3/upstream_ip_port_matcher.pb.h"

#include "source/common/runtime/runtime_features.h"

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

// With the correct-stats-prefix flag enabled (default), upstream RBAC filter stats in a cluster
// context are scoped under "cluster.<name>.rbac.*" and NOT double-prefixed.
TEST_P(UpstreamRbacIntegrationTest, ClusterContextStatsNotDoublePrefixed) {
  addUpstreamRbacFilter(loopbackCidr(), loopbackPrefixLen());
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("403", response->headers().getStatusValue());

  // The denied counter must exist under the cluster prefix exactly once.
  test_server_->waitForCounter("cluster.cluster_0.rbac.denied", testing::Eq(1));
  // It must not be double-prefixed.
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.cluster.cluster_0.rbac.denied"));

  cleanupUpstreamAndDownstream();
}

class UpstreamRbacStatsIntegrationTest : public UpstreamRbacIntegrationTest {
public:
  // Enforced rules with an empty rules_stat_prefix and shadow rules, both denying the loopback
  // upstream, with per rule stats.
  HttpFilterProto rbacFilter() {
    envoy::config::rbac::v3::Policy policy;
    envoy::extensions::rbac::matchers::upstream_ip_port::v3::UpstreamIpPortMatcher matcher;
    matcher.mutable_upstream_ip()->set_address_prefix(loopbackCidr());
    matcher.mutable_upstream_ip()->mutable_prefix_len()->set_value(loopbackPrefixLen());
    auto* matcher_ext = policy.add_permissions()->mutable_matcher();
    matcher_ext->set_name("envoy.rbac.matchers.upstream_ip_port");
    std::ignore = matcher_ext->mutable_typed_config()->PackFrom(matcher);
    policy.add_principals()->set_any(true);

    envoy::extensions::filters::http::rbac::v3::RBAC rbac;
    rbac.mutable_rules()->set_action(envoy::config::rbac::v3::RBAC::DENY);
    (*rbac.mutable_rules()->mutable_policies())["deny-upstream"] = policy;
    rbac.mutable_shadow_rules()->set_action(envoy::config::rbac::v3::RBAC::DENY);
    (*rbac.mutable_shadow_rules()->mutable_policies())["deny-upstream"] = policy;
    rbac.set_shadow_rules_stat_prefix("shadow_");
    rbac.set_track_per_rule_stats(true);

    HttpFilterProto rbac_filter;
    rbac_filter.set_name("envoy.filters.http.upstream_rbac");
    std::ignore = rbac_filter.mutable_typed_config()->PackFrom(rbac);
    return rbac_filter;
  }

  void addRouterUpstreamRbacFilter() {
    HttpFilterProto rbac_filter = rbacFilter();
    config_helper_.addConfigModifier(
        [rbac_filter](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* router_filter = hcm.mutable_http_filters(hcm.http_filters_size() - 1);
          envoy::extensions::filters::http::router::v3::Router router;
          ASSERT_TRUE(router_filter->typed_config().UnpackTo(&router));
          *router.add_upstream_http_filters() = rbac_filter;
          auto* codec_filter = router.add_upstream_http_filters();
          codec_filter->set_name("envoy.filters.http.upstream_codec");
          std::ignore = codec_filter->mutable_typed_config()->PackFrom(
              envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec());
          std::ignore = router_filter->mutable_typed_config()->PackFrom(router);
        });
  }

  void addClusterUpstreamRbacFilter() {
    HttpFilterProto rbac_filter = rbacFilter();
    config_helper_.addConfigModifier(
        [rbac_filter](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
          ConfigHelper::HttpProtocolOptions protocol_options =
              MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
                  (*cluster->mutable_typed_extension_protocol_options())
                      ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
          *protocol_options.add_http_filters() = rbac_filter;
          auto* codec_filter = protocol_options.add_http_filters();
          codec_filter->set_name("envoy.filters.http.upstream_codec");
          std::ignore = codec_filter->mutable_typed_config()->PackFrom(
              envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec());
          std::ignore = (*cluster->mutable_typed_extension_protocol_options())
                            ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
                                .PackFrom(protocol_options);
        });
  }

  // The legacy tag extraction mode in IPv4 and the explicit tags mode in IPv6. The mode is read
  // during server initialization, so it must be set directly rather than with
  // addRuntimeOverride().
  bool useExplicitTags() {
    const bool explicit_tags = version_ == Network::Address::IpVersion::v6;
    Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.enable_stats_explicit_tags",
                                  explicit_tags);
    return explicit_tags;
  }

  void sendDeniedRequest() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("403", response->headers().getStatusValue());
    cleanupUpstreamAndDownstream();
  }

  void expectStat(const std::string& name, const std::string& tag_extracted_name,
                  std::vector<std::pair<std::string, std::string>> expected_tags) {
    test_server_->waitForCounter(name, testing::Eq(1));
    const Stats::CounterSharedPtr counter = test_server_->counter(name);
    ASSERT_NE(counter, nullptr) << "no counter named '" << name << "'";
    EXPECT_EQ(counter->tagExtractedName(), tag_extracted_name) << " for stat '" << name << "'";
    std::vector<std::pair<std::string, std::string>> actual_tags;
    for (const Stats::Tag& tag : counter->tags()) {
      actual_tags.emplace_back(tag.name_, tag.value_);
    }
    std::sort(actual_tags.begin(), actual_tags.end());
    std::sort(expected_tags.begin(), expected_tags.end());
    EXPECT_EQ(actual_tags, expected_tags) << " for stat '" << name << "'";
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, UpstreamRbacStatsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(UpstreamRbacStatsIntegrationTest, RouterUpstreamFilterStatsAreIdenticalInBothModes) {
  useExplicitTags();
  addRouterUpstreamRbacFilter();
  initialize();
  sendDeniedRequest();

  const std::pair<std::string, std::string> hcm_tag{"envoy.http_conn_manager_prefix",
                                                    "config_test"};
  expectStat("http.config_test.rbac.denied", "http.rbac.denied", {hcm_tag});
  expectStat("http.config_test.rbac.policy.deny-upstream.denied", "http.rbac.policy.denied",
             {hcm_tag, {"envoy.rbac_policy_name", "deny-upstream"}});
  expectStat("http.config_test.rbac.shadow_.shadow_denied", "http.rbac.shadow_denied",
             {hcm_tag, {"envoy.rbac_http_prefix", "shadow_"}});
  expectStat("http.config_test.rbac.shadow_.policy.deny-upstream.shadow_denied",
             "http.rbac.policy.shadow_denied",
             {hcm_tag,
              {"envoy.rbac_http_prefix", "shadow_"},
              {"envoy.rbac_policy_name", "deny-upstream"}});
}

TEST_P(UpstreamRbacStatsIntegrationTest, ClusterUpstreamFilterStatsInBothModes) {
  const bool explicit_tags = useExplicitTags();
  addClusterUpstreamRbacFilter();
  initialize();
  sendDeniedRequest();

  const std::pair<std::string, std::string> cluster_tag{"envoy.cluster_name", "cluster_0"};
  expectStat("cluster.cluster_0.rbac.denied", "cluster.rbac.denied", {cluster_tag});
  expectStat("cluster.cluster_0.rbac.policy.deny-upstream.denied", "cluster.rbac.policy.denied",
             {cluster_tag, {"envoy.rbac_policy_name", "deny-upstream"}});
  // No legacy regex extracts the rules prefix under a cluster, the explicit tags do.
  // This is expected behavior changes when explicit tags are used.
  if (explicit_tags) {
    expectStat("cluster.cluster_0.rbac.shadow_.shadow_denied", "cluster.rbac.shadow_denied",
               {cluster_tag, {"envoy.rbac_http_prefix", "shadow_"}});
    expectStat("cluster.cluster_0.rbac.shadow_.policy.deny-upstream.shadow_denied",
               "cluster.rbac.policy.shadow_denied",
               {cluster_tag,
                {"envoy.rbac_http_prefix", "shadow_"},
                {"envoy.rbac_policy_name", "deny-upstream"}});
  } else {
    expectStat("cluster.cluster_0.rbac.shadow_.shadow_denied", "cluster.rbac.shadow_.shadow_denied",
               {cluster_tag});
    expectStat("cluster.cluster_0.rbac.shadow_.policy.deny-upstream.shadow_denied",
               "cluster.rbac.shadow_.policy.shadow_denied",
               {cluster_tag, {"envoy.rbac_policy_name", "deny-upstream"}});
  }
}

} // namespace
} // namespace Envoy
