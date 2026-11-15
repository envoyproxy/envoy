#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/common/rbac/utility.h"
#include "source/extensions/filters/http/rbac/rbac_filter.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace {

TEST(ResponseDetail, ResponseDetail) {
  EXPECT_EQ(RBAC::responseDetail("abdfxy"), "rbac_access_denied_matched_policy[abdfxy]");
  EXPECT_EQ(RBAC::responseDetail("ab df  xy"), "rbac_access_denied_matched_policy[ab_df__xy]");
  EXPECT_EQ(RBAC::responseDetail("a \t\f\v\n\ry"), "rbac_access_denied_matched_policy[a______y]");
}

TEST(CreateEngine, IgnoreRules) {
  envoy::extensions::filters::http::rbac::v3::RBAC config;
  std::string yaml = R"EOF(
rules:
  action: ALLOW
matcher:
  on_no_match:
    action:
      name: action
      typed_config:
        "@type": type.googleapis.com/envoy.config.rbac.v3.Action
        name: none
        action: ALLOW
)EOF";
  TestUtility::loadFromYaml(yaml, config);

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  HttpFilters::RBACFilter::ActionValidationVisitor validation_visitor;
  EXPECT_LOG_CONTAINS("warn", "RBAC rules are ignored when matcher is configured",
                      createEngine(config, factory_context,
                                   ProtobufMessage::getStrictValidationVisitor(),
                                   validation_visitor));
}

TEST(CreateShadowEngine, IgnoreShadowRules) {
  envoy::extensions::filters::http::rbac::v3::RBAC config;
  std::string yaml = R"EOF(
shadow_rules:
  action: ALLOW
shadow_matcher:
  on_no_match:
    action:
      name: action
      typed_config:
        "@type": type.googleapis.com/envoy.config.rbac.v3.Action
        name: none
        action: ALLOW
)EOF";
  TestUtility::loadFromYaml(yaml, config);

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  HttpFilters::RBACFilter::ActionValidationVisitor validation_visitor;
  EXPECT_LOG_CONTAINS("warn", "RBAC shadow rules are ignored when shadow matcher is configured",
                      createShadowEngine(config, factory_context,
                                         ProtobufMessage::getStrictValidationVisitor(),
                                         validation_visitor));
}

void expectCounter(const Stats::Counter& counter, const std::string& name,
                   const std::string& tag_extracted_name,
                   const std::vector<std::pair<std::string, std::string>>& expected_tags) {
  EXPECT_EQ(name, counter.name());
  EXPECT_EQ(tag_extracted_name, counter.tagExtractedName());
  std::vector<std::pair<std::string, std::string>> tags;
  for (const Stats::Tag& tag : counter.tags()) {
    tags.emplace_back(tag.name_, tag.value_);
  }
  EXPECT_EQ(expected_tags, tags);
}

TEST(GenerateStats, NetworkFilterPrefixIsTagged) {
  Stats::IsolatedStoreImpl store;
  auto stats = generateStats("tcp.", "rules_", "", false, *store.rootScope());

  expectCounter(stats.allowed_, "tcp.rbac.rules_.allowed", "rbac.allowed",
                {{"envoy.rbac_prefix", "tcp"}, {"envoy.rbac_rules_prefix", "rules_"}});
  expectCounter(stats.shadow_allowed_, "tcp.rbac.shadow_allowed", "rbac.shadow_allowed",
                {{"envoy.rbac_prefix", "tcp"}});

  stats.incPolicyAllowed("foo");
  expectCounter(*TestUtility::findCounter(store, "tcp.rbac.rules_.policy.foo.allowed"),
                "tcp.rbac.rules_.policy.foo.allowed", "rbac.policy.allowed",
                {{"envoy.rbac_prefix", "tcp"},
                 {"envoy.rbac_rules_prefix", "rules_"},
                 {"envoy.rbac_policy_name", "foo"}});
}

TEST(GenerateStats, HttpFilterEmptyPrefix) {
  Stats::IsolatedStoreImpl store;
  auto stats = generateStats("", "", "shadow_", true, *store.rootScope());

  expectCounter(stats.allowed_, "rbac.allowed", "rbac.allowed", {});
  expectCounter(stats.shadow_allowed_, "rbac.shadow_.shadow_allowed", "rbac.shadow_allowed",
                {{"envoy.rbac_http_prefix", "shadow_"}});

  stats.incPolicyShadowAllowed("foo");
  expectCounter(*TestUtility::findCounter(store, "rbac.shadow_.policy.foo.shadow_allowed"),
                "rbac.shadow_.policy.foo.shadow_allowed", "rbac.policy.shadow_allowed",
                {{"envoy.rbac_http_prefix", "shadow_"}, {"envoy.rbac_policy_name", "foo"}});
}

TEST(GenerateStats, HttpFilterFlatPrefixIsPartOfBaseName) {
  Stats::IsolatedStoreImpl store;
  auto stats = generateStats("http.foo.", "rules_", "", true, *store.rootScope());

  // The flat parent prefix is not tagged but kept in the tag-extracted name.
  expectCounter(stats.denied_, "http.foo.rbac.rules_.denied", "http.foo.rbac.denied",
                {{"envoy.rbac_http_prefix", "rules_"}});
  expectCounter(stats.shadow_denied_, "http.foo.rbac.shadow_denied", "http.foo.rbac.shadow_denied",
                {});

  stats.incPolicyDenied("bar");
  expectCounter(*TestUtility::findCounter(store, "http.foo.rbac.rules_.policy.bar.denied"),
                "http.foo.rbac.rules_.policy.bar.denied", "http.foo.rbac.policy.denied",
                {{"envoy.rbac_http_prefix", "rules_"}, {"envoy.rbac_policy_name", "bar"}});
}

} // namespace
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
