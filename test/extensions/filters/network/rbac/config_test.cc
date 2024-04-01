#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/network/rbac/v3/rbac.pb.validate.h"

#include "source/extensions/filters/network/rbac/config.h"
#include "source/extensions/filters/network/rbac/rbac_filter.h"

#include "test/mocks/server/factory_context.h"

#include "fmt/printf.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xds/type/matcher/v3/matcher.pb.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBACFilter {
namespace {

const std::string header = R"EOF(
{ "header": {"name": "key", "exact_match": "value"} }
)EOF";

constexpr absl::string_view header_key = "key";
constexpr absl::string_view header_value = "value";

} // namespace

class RoleBasedAccessControlNetworkFilterConfigFactoryTest : public testing::Test {
public:
  void validateRule(const std::string& policy_json) {
    checkRule(fmt::sprintf(policy_json, header));
  }

  void validateMatcher(const std::string& matcher_yaml) { checkMatcher(matcher_yaml); }

private:
  void checkRule(const std::string& policy_json) {
    envoy::config::rbac::v3::Policy policy_proto{};
    TestUtility::loadFromJson(policy_json, policy_proto);

    envoy::extensions::filters::network::rbac::v3::RBAC config{};
    config.set_stat_prefix("test");
    (*config.mutable_rules()->mutable_policies())["foo"] = policy_proto;

    NiceMock<Server::Configuration::MockFactoryContext> context;
    RoleBasedAccessControlNetworkFilterConfigFactory factory;
    EXPECT_THROW(factory.createFilterFactoryFromProto(config, context).IgnoreError(),
                 Envoy::EnvoyException);

    config.clear_rules();
    (*config.mutable_shadow_rules()->mutable_policies())["foo"] = policy_proto;
    EXPECT_THROW(factory.createFilterFactoryFromProto(config, context).IgnoreError(),
                 Envoy::EnvoyException);
  }

  void checkMatcher(const std::string& matcher_yaml) {
    xds::type::matcher::v3::Matcher matcher_proto{};
    TestUtility::loadFromYaml(matcher_yaml, matcher_proto);

    envoy::extensions::filters::network::rbac::v3::RBAC config{};
    config.set_stat_prefix("test");
    *config.mutable_matcher() = matcher_proto;

    Stats::IsolatedStoreImpl store;
    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    EXPECT_THROW(
        std::make_shared<RoleBasedAccessControlFilterConfig>(
            config, *store.rootScope(), context, ProtobufMessage::getStrictValidationVisitor()),
        Envoy::EnvoyException);

    config.clear_matcher();
    *config.mutable_shadow_matcher() = matcher_proto;
    EXPECT_THROW(
        std::make_shared<RoleBasedAccessControlFilterConfig>(
            config, *store.rootScope(), context, ProtobufMessage::getStrictValidationVisitor()),
        Envoy::EnvoyException);
  }
};

TEST_F(RoleBasedAccessControlNetworkFilterConfigFactoryTest, ValidProto) {
  envoy::config::rbac::v3::Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  envoy::extensions::filters::network::rbac::v3::RBAC config;
  config.set_stat_prefix("stats");
  (*config.mutable_rules()->mutable_policies())["foo"] = policy;

  NiceMock<Server::Configuration::MockFactoryContext> context;
  RoleBasedAccessControlNetworkFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context).value();
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST_F(RoleBasedAccessControlNetworkFilterConfigFactoryTest, ValidMatcherProto) {
  envoy::config::rbac::v3::Action action;
  action.set_name("foo");
  action.set_action(envoy::config::rbac::v3::RBAC::ALLOW);

  xds::type::matcher::v3::Matcher matcher;
  auto matcher_action = matcher.mutable_on_no_match()->mutable_action();
  matcher_action->set_name("action");
  matcher_action->mutable_typed_config()->PackFrom(action);
  envoy::extensions::filters::network::rbac::v3::RBAC config;
  config.set_stat_prefix("stats");
  *config.mutable_matcher() = matcher;

  NiceMock<Server::Configuration::MockFactoryContext> context;
  RoleBasedAccessControlNetworkFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context).value();
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST_F(RoleBasedAccessControlNetworkFilterConfigFactoryTest, EmptyProto) {
  RoleBasedAccessControlNetworkFilterConfigFactory factory;
  EXPECT_NE(nullptr, dynamic_cast<envoy::extensions::filters::network::rbac::v3::RBAC*>(
                         factory.createEmptyConfigProto().get()));
}

TEST_F(RoleBasedAccessControlNetworkFilterConfigFactoryTest, InvalidPermission) {
  validateRule(R"EOF(
{
  "permissions": [ { "any": true }, { "and_rules": { "rules": [ { "any": true }, %s ] } } ],
  "principals": [ { "any": true } ]
}
)EOF");

  validateRule(R"EOF(
{
  "permissions": [ { "any": true }, { "or_rules": { "rules": [ { "any": true }, %s ] } } ],
  "principals": [ { "any": true } ]
}
)EOF");

  validateRule(R"EOF(
{
  "permissions": [ { "any": true }, { "not_rule": %s } ],
  "principals": [ { "any": true } ]
}
)EOF");

  validateRule(R"EOF(
{
  "permissions": [ { "any": true }, %s ],
  "principals": [ { "any": true } ]
}
)EOF");
}

TEST_F(RoleBasedAccessControlNetworkFilterConfigFactoryTest, InvalidPrincipal) {
  validateRule(R"EOF(
{
  "principals": [ { "any": true }, { "and_ids": { "ids": [ { "any": true }, %s ] } } ],
  "permissions": [ { "any": true } ]
}
)EOF");

  validateRule(R"EOF(
{
  "principals": [ { "any": true }, { "or_ids": { "ids": [ { "any": true }, %s ] } } ],
  "permissions": [ { "any": true } ]
}
)EOF");

  validateRule(R"EOF(
{
  "principals": [ { "any": true }, { "not_id": %s } ],
  "permissions": [ { "any": true } ]
}
)EOF");

  validateRule(R"EOF(
{
  "principals": [ { "any": true }, %s ],
  "permissions": [ { "any": true } ]
}
)EOF");
}

TEST_F(RoleBasedAccessControlNetworkFilterConfigFactoryTest, InvalidMatcher) {
  validateMatcher(fmt::format(R"EOF(
matcher_tree:
  input:
    name: source-ip
    typed_config:
      '@type': type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
      header_name: {}
  exact_match_map:
    map:
      "{}":
        action:
          name: action
          typed_config:
            '@type': type.googleapis.com/envoy.config.rbac.v3.Action
            name: deny
)EOF",
                              header_key, header_value));

  validateMatcher(fmt::format(R"EOF(
matcher_tree:
  input:
    name: source-ip
    typed_config:
      '@type': type.googleapis.com/envoy.type.matcher.v3.HttpResponseHeaderMatchInput
      header_name: {}
  exact_match_map:
    map:
      "{}":
        action:
          name: action
          typed_config:
            '@type': type.googleapis.com/envoy.config.rbac.v3.Action
            name: deny
)EOF",
                              header_key, header_value));
}

} // namespace RBACFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
