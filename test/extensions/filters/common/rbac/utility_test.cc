#include "source/extensions/filters/common/rbac/utility.h"
#include "source/extensions/filters/http/rbac/rbac_filter.h"

#include "test/mocks/server/factory_context.h"
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

} // namespace
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
