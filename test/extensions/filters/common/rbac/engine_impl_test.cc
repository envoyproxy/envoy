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

enum class LogResult { Yes, No, Undecided };

void checkEngine(
    RBAC::RoleBasedAccessControlEngineImpl& engine, bool expected, LogResult expected_log,
    StreamInfo::StreamInfo& info,
    const Envoy::Network::Connection& connection = Envoy::Network::MockConnection(),
    const Envoy::Http::RequestHeaderMap& headers = Envoy::Http::TestRequestHeaderMapImpl()) {

  bool engineRes = engine.handleAction(connection, headers, info, nullptr);
  EXPECT_EQ(expected, engineRes);

  if (expected_log != LogResult::Undecided) {
    auto filter_meta = info.dynamicMetadata().filter_metadata().at(
        RBAC::DynamicMetadataKeysSingleton::get().CommonNamespace);
    EXPECT_EQ(expected_log == LogResult::Yes,
              filter_meta.fields()
                  .at(RBAC::DynamicMetadataKeysSingleton::get().AccessLogKey)
                  .bool_value());
  } else {
    EXPECT_EQ(info.dynamicMetadata().filter_metadata().end(),
              info.dynamicMetadata().filter_metadata().find(
                  Filters::Common::RBAC::DynamicMetadataKeysSingleton::get().CommonNamespace));
  }
}

void checkEngine(
    RBAC::RoleBasedAccessControlEngineImpl& engine, bool expected, LogResult expected_log,
    const Envoy::Network::Connection& connection = Envoy::Network::MockConnection(),
    const Envoy::Http::RequestHeaderMap& headers = Envoy::Http::TestRequestHeaderMapImpl()) {

  NiceMock<StreamInfo::MockStreamInfo> empty_info;
  checkEngine(engine, expected, expected_log, empty_info, connection, headers);
}

void onMetadata(NiceMock<StreamInfo::MockStreamInfo>& info) {
  ON_CALL(info, setDynamicMetadata("envoy.common", _))
      .WillByDefault(Invoke([&info](const std::string&, const ProtobufWkt::Struct& obj) {
        (*info.metadata_.mutable_filter_metadata())["envoy.common"] = obj;
      }));
}

TEST(RoleBasedAccessControlEngineImpl, Disabled) {
  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
  RBAC::RoleBasedAccessControlEngineImpl engine_allow(rbac);
  checkEngine(engine_allow, false, LogResult::Undecided);

  rbac.set_action(envoy::config::rbac::v3::RBAC::DENY);
  RBAC::RoleBasedAccessControlEngineImpl engine_deny(rbac);
  checkEngine(engine_deny, true, LogResult::Undecided);
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
  checkEngine(engine, true, LogResult::Undecided, info, conn, headers);

  addr = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 456, false);
  EXPECT_CALL(Const(info), downstreamLocalAddress()).WillOnce(ReturnRef(addr));
  checkEngine(engine, false, LogResult::Undecided, info, conn, headers);
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
  checkEngine(engine, false, LogResult::Undecided, info, conn, headers);

  addr = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 456, false);
  EXPECT_CALL(Const(info), downstreamLocalAddress()).WillOnce(ReturnRef(addr));
  checkEngine(engine, true, LogResult::Undecided, info, conn, headers);
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
  checkEngine(engine, false, LogResult::Undecided);
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

  rbac.set_action(envoy::config::rbac::v3::RBAC::LOG);
  EXPECT_THROW_WITH_REGEX(RBAC::RoleBasedAccessControlEngineImpl engine_log(rbac), EnvoyException,
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
  checkEngine(engine, false, LogResult::Undecided);
}

TEST(RoleBasedAccessControlEngineImpl, EvaluationFailure) {
  envoy::config::rbac::v3::Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  policy.mutable_condition()->MergeFrom(
      TestUtility::parseYaml<google::api::expr::v1alpha1::Expr>(R"EOF(
    select_expr:
      operand:
        const_expr:
          string_value: request
      field: undefined
  )EOF"));

  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);
  checkEngine(engine, false, LogResult::Undecided);
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
  checkEngine(engine, false, LogResult::Undecided, Envoy::Network::MockConnection());
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

  checkEngine(engine, true, LogResult::Undecided, Envoy::Network::MockConnection(), headers);
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

  checkEngine(engine, true, LogResult::Undecided, info, Envoy::Network::MockConnection(), headers);
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
  EXPECT_CALL(Const(info), downstreamLocalAddress()).Times(1).WillRepeatedly(ReturnRef(addr));
  checkEngine(engine, false, LogResult::Undecided, info, conn, headers);
}

// Log tests
TEST(RoleBasedAccessControlEngineImpl, DisabledLog) {
  NiceMock<StreamInfo::MockStreamInfo> info;
  onMetadata(info);

  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::LOG);
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);
  checkEngine(engine, true, RBAC::LogResult::No, info);
}

TEST(RoleBasedAccessControlEngineImpl, LogIfMatched) {
  envoy::config::rbac::v3::Policy policy;
  policy.add_permissions()->set_destination_port(123);
  policy.add_principals()->set_any(true);

  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::LOG);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  onMetadata(info);

  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  EXPECT_CALL(Const(info), downstreamLocalAddress()).WillOnce(ReturnRef(addr));
  checkEngine(engine, true, RBAC::LogResult::Yes, info, conn, headers);

  addr = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 456, false);
  EXPECT_CALL(Const(info), downstreamLocalAddress()).WillOnce(ReturnRef(addr));
  checkEngine(engine, true, RBAC::LogResult::No, info, conn, headers);
}

} // namespace
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
