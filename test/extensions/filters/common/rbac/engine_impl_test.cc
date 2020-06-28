#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/config/rbac/v3/rbac.pb.validate.h"

#include "common/network/utility.h"

#include "extensions/filters/common/rbac/engine_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Const;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace {

void checkEngine(
    const RBAC::RoleBasedAccessControlEngineImpl& engine, bool expected,
    const Envoy::Network::Connection& connection = Envoy::Network::MockConnection(),
    const Envoy::Http::RequestHeaderMap& headers = Envoy::Http::TestRequestHeaderMapImpl(),
    const StreamInfo::StreamInfo& info = NiceMock<StreamInfo::MockStreamInfo>()) {
  EXPECT_EQ(expected, engine.allowed(connection, headers, info, nullptr));
}

TEST(RoleBasedAccessControlEngineImpl, Disabled) {
  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
  checkEngine(RBAC::RoleBasedAccessControlEngineImpl(rbac), false);

  rbac.set_action(envoy::config::rbac::v3::RBAC::DENY);
  checkEngine(RBAC::RoleBasedAccessControlEngineImpl(rbac), true);
}

// Test various invalid policies to validate the fix for
// https://github.com/envoyproxy/envoy/issues/8715.
TEST(RoleBasedAccessControlEngineImpl, InvalidConfig) {
  {
    envoy::config::rbac::v3::RBAC rbac;
    rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
    envoy::config::rbac::v3::Policy policy;
    (*rbac.mutable_policies())["foo"] = policy;

    EXPECT_THROW_WITH_REGEX(TestUtility::validate(rbac), EnvoyException,
                            "RBACValidationError\\.Policies.*PolicyValidationError\\.Permissions"
                            ".*value must contain at least")
  }

  {
    envoy::config::rbac::v3::RBAC rbac;
    rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
    envoy::config::rbac::v3::Policy policy;
    policy.add_permissions();
    (*rbac.mutable_policies())["foo"] = policy;

    EXPECT_THROW_WITH_REGEX(
        TestUtility::validate(rbac), EnvoyException,
        "RBACValidationError\\.Policies.*PolicyValidationError\\.Permissions.*rule.*is required");
  }

  {
    envoy::config::rbac::v3::RBAC rbac;
    rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
    envoy::config::rbac::v3::Policy policy;
    auto* permission = policy.add_permissions();
    auto* and_rules = permission->mutable_and_rules();
    and_rules->add_rules();
    (*rbac.mutable_policies())["foo"] = policy;

    EXPECT_THROW_WITH_REGEX(
        TestUtility::validate(rbac), EnvoyException,
        "RBACValidationError\\.Policies.*PolicyValidationError\\.Permissions"
        ".*PermissionValidationError\\.AndRules.*SetValidationError\\.Rules.*rule.*is required");
  }

  {
    envoy::config::rbac::v3::RBAC rbac;
    rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
    envoy::config::rbac::v3::Policy policy;
    auto* permission = policy.add_permissions();
    permission->set_any(true);
    (*rbac.mutable_policies())["foo"] = policy;

    EXPECT_THROW_WITH_REGEX(TestUtility::validate(rbac), EnvoyException,
                            "RBACValidationError\\.Policies.*PolicyValidationError\\.Principals"
                            ".*value must contain at least")
  }

  {
    envoy::config::rbac::v3::RBAC rbac;
    rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
    envoy::config::rbac::v3::Policy policy;
    auto* permission = policy.add_permissions();
    permission->set_any(true);
    policy.add_principals();
    (*rbac.mutable_policies())["foo"] = policy;

    EXPECT_THROW_WITH_REGEX(TestUtility::validate(rbac), EnvoyException,
                            "RBACValidationError\\.Policies.*PolicyValidationError\\.Principals"
                            ".*identifier.*is required");
  }

  {
    envoy::config::rbac::v3::RBAC rbac;
    rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
    envoy::config::rbac::v3::Policy policy;
    auto* permission = policy.add_permissions();
    permission->set_any(true);
    auto* principal = policy.add_principals();
    auto* and_ids = principal->mutable_and_ids();
    and_ids->add_ids();
    (*rbac.mutable_policies())["foo"] = policy;

    EXPECT_THROW_WITH_REGEX(
        TestUtility::validate(rbac), EnvoyException,
        "RBACValidationError\\.Policies.*PolicyValidationError\\.Principals"
        ".*PrincipalValidationError\\.AndIds.*SetValidationError\\.Ids.*identifier.*is required");
  }
}

TEST(RoleBasedAccessControlEngineImpl, AllowedAllowlist) {
  envoy::config::rbac::v3::Policy policy;
  policy.add_permissions()->set_destination_port(123);
  policy.add_principals()->set_any(true);

  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  EXPECT_CALL(Const(info), downstreamLocalAddress()).WillOnce(ReturnRef(addr));
  checkEngine(engine, true, conn, headers, info);

  addr = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 456, false);
  EXPECT_CALL(Const(info), downstreamLocalAddress()).WillOnce(ReturnRef(addr));
  checkEngine(engine, false, conn, headers, info);
}

TEST(RoleBasedAccessControlEngineImpl, DeniedDenylist) {
  envoy::config::rbac::v3::Policy policy;
  policy.add_permissions()->set_destination_port(123);
  policy.add_principals()->set_any(true);

  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::DENY);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  EXPECT_CALL(Const(info), downstreamLocalAddress()).WillOnce(ReturnRef(addr));
  checkEngine(engine, false, conn, headers, info);

  addr = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 456, false);
  EXPECT_CALL(Const(info), downstreamLocalAddress()).WillOnce(ReturnRef(addr));
  checkEngine(engine, true, conn, headers, info);
}

TEST(RoleBasedAccessControlEngineImpl, BasicCondition) {
  envoy::config::rbac::v3::Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  policy.mutable_condition()->MergeFrom(
      TestUtility::parseYaml<google::api::expr::v1alpha1::Expr>(R"EOF(
    const_expr:
      bool_value: false
  )EOF"));

  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);
  checkEngine(engine, false);
}

TEST(RoleBasedAccessControlEngineImpl, MalformedCondition) {
  envoy::config::rbac::v3::Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  policy.mutable_condition()->MergeFrom(
      TestUtility::parseYaml<google::api::expr::v1alpha1::Expr>(R"EOF(
    call_expr:
      function: undefined_extent
      args:
      - const_expr:
          bool_value: false
  )EOF"));

  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
  (*rbac.mutable_policies())["foo"] = policy;

  EXPECT_THROW_WITH_REGEX(RBAC::RoleBasedAccessControlEngineImpl engine(rbac), EnvoyException,
                          "failed to create an expression: .*");
}

TEST(RoleBasedAccessControlEngineImpl, MistypedCondition) {
  envoy::config::rbac::v3::Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  policy.mutable_condition()->MergeFrom(
      TestUtility::parseYaml<google::api::expr::v1alpha1::Expr>(R"EOF(
    const_expr:
      int64_value: 13
  )EOF"));

  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);
  checkEngine(engine, false);
}

TEST(RoleBasedAccessControlEngineImpl, ErrorCondition) {
  envoy::config::rbac::v3::Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  policy.mutable_condition()->MergeFrom(
      TestUtility::parseYaml<google::api::expr::v1alpha1::Expr>(R"EOF(
    call_expr:
      function: _[_]
      args:
      - select_expr:
          operand:
            ident_expr:
              name: request
          field: undefined
      - const_expr:
          string_value: foo
  )EOF"));

  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);
  checkEngine(engine, false, Envoy::Network::MockConnection());
}

TEST(RoleBasedAccessControlEngineImpl, HeaderCondition) {
  envoy::config::rbac::v3::Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  policy.mutable_condition()->MergeFrom(
      TestUtility::parseYaml<google::api::expr::v1alpha1::Expr>(R"EOF(
    call_expr:
      function: _==_
      args:
      - call_expr:
          function: _[_]
          args:
          - select_expr:
              operand:
                ident_expr:
                  name: request
              field: headers
          - const_expr:
              string_value: foo
      - const_expr:
          string_value: bar
  )EOF"));

  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);

  Envoy::Http::TestRequestHeaderMapImpl headers;
  Envoy::Http::LowerCaseString key("foo");
  std::string value = "bar";
  headers.setReference(key, value);

  checkEngine(engine, true, Envoy::Network::MockConnection(), headers);
}

TEST(RoleBasedAccessControlEngineImpl, MetadataCondition) {
  envoy::config::rbac::v3::Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  policy.mutable_condition()->MergeFrom(
      TestUtility::parseYaml<google::api::expr::v1alpha1::Expr>(R"EOF(
    call_expr:
      function: _==_
      args:
      - call_expr:
          function: _[_]
          args:
          - call_expr:
              function: _[_]
              args:
              - select_expr:
                  operand:
                    ident_expr:
                      name: metadata
                  field: filter_metadata
              - const_expr:
                  string_value: other
          - const_expr:
              string_value: label
      - const_expr:
          string_value: prod
  )EOF"));

  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);

  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;

  auto label = MessageUtil::keyValueStruct("label", "prod");
  envoy::config::core::v3::Metadata metadata;
  metadata.mutable_filter_metadata()->insert(
      Protobuf::MapPair<std::string, ProtobufWkt::Struct>("other", label));
  EXPECT_CALL(Const(info), dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));

  checkEngine(engine, true, Envoy::Network::MockConnection(), headers, info);
}

TEST(RoleBasedAccessControlEngineImpl, ConjunctiveCondition) {
  envoy::config::rbac::v3::Policy policy;
  policy.add_permissions()->set_destination_port(123);
  policy.add_principals()->set_any(true);
  policy.mutable_condition()->MergeFrom(
      TestUtility::parseYaml<google::api::expr::v1alpha1::Expr>(R"EOF(
    const_expr:
      bool_value: false
  )EOF"));

  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  EXPECT_CALL(Const(info), downstreamLocalAddress()).WillOnce(ReturnRef(addr));
  checkEngine(engine, false, conn, headers, info);
}

} // namespace
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
