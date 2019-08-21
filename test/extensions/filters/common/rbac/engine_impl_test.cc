#include "common/network/utility.h"

#include "extensions/filters/common/rbac/engine_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Const;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace {

void checkEngine(const RBAC::RoleBasedAccessControlEngineImpl& engine, bool expected,
                 const Envoy::Network::Connection& connection = Envoy::Network::MockConnection(),
                 const Envoy::Http::HeaderMap& headers = Envoy::Http::HeaderMapImpl(),
                 const envoy::api::v2::core::Metadata& metadata = envoy::api::v2::core::Metadata(),
                 std::string* policy_id = nullptr) {
  NiceMock<StreamInfo::MockStreamInfo> info;
  EXPECT_CALL(Const(info), dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
  EXPECT_EQ(expected, engine.allowed(connection, headers, info, policy_id));
}

TEST(RoleBasedAccessControlEngineImpl, Disabled) {
  envoy::config::rbac::v2::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v2::RBAC_Action::RBAC_Action_ALLOW);
  checkEngine(RBAC::RoleBasedAccessControlEngineImpl(rbac), false);

  rbac.set_action(envoy::config::rbac::v2::RBAC_Action::RBAC_Action_DENY);
  checkEngine(RBAC::RoleBasedAccessControlEngineImpl(rbac), true);
}

TEST(RoleBasedAccessControlEngineImpl, AllowedWhitelist) {
  envoy::config::rbac::v2::Policy policy;
  policy.add_permissions()->set_destination_port(123);
  policy.add_principals()->set_any(true);

  envoy::config::rbac::v2::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v2::RBAC_Action::RBAC_Action_ALLOW);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);

  Envoy::Network::MockConnection conn;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  EXPECT_CALL(conn, localAddress()).WillOnce(ReturnRef(addr));
  checkEngine(engine, true, conn);

  addr = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 456, false);
  EXPECT_CALL(conn, localAddress()).WillOnce(ReturnRef(addr));
  checkEngine(engine, false, conn);
}

TEST(RoleBasedAccessControlEngineImpl, DeniedBlacklist) {
  envoy::config::rbac::v2::Policy policy;
  policy.add_permissions()->set_destination_port(123);
  policy.add_principals()->set_any(true);

  envoy::config::rbac::v2::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v2::RBAC_Action::RBAC_Action_DENY);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);

  Envoy::Network::MockConnection conn;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  EXPECT_CALL(conn, localAddress()).WillOnce(ReturnRef(addr));
  checkEngine(engine, false, conn);

  addr = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 456, false);
  EXPECT_CALL(conn, localAddress()).WillOnce(ReturnRef(addr));
  checkEngine(engine, true, conn);
}

TEST(RoleBasedAccessControlEngineImpl, BasicCondition) {
  envoy::config::rbac::v2::Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  policy.mutable_condition()->MergeFrom(
      TestUtility::parseYaml<google::api::expr::v1alpha1::Expr>(R"EOF(
    const_expr:
      bool_value: false
  )EOF"));

  envoy::config::rbac::v2::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v2::RBAC_Action::RBAC_Action_ALLOW);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);
  checkEngine(engine, false);
}

TEST(RoleBasedAccessControlEngineImpl, MalformedCondition) {
  envoy::config::rbac::v2::Policy policy;
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

  envoy::config::rbac::v2::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v2::RBAC_Action::RBAC_Action_ALLOW);
  (*rbac.mutable_policies())["foo"] = policy;

  EXPECT_THROW_WITH_REGEX(RBAC::RoleBasedAccessControlEngineImpl engine(rbac), EnvoyException,
                          "failed to create an expression: .*");
}

TEST(RoleBasedAccessControlEngineImpl, MistypedCondition) {
  envoy::config::rbac::v2::Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  policy.mutable_condition()->MergeFrom(
      TestUtility::parseYaml<google::api::expr::v1alpha1::Expr>(R"EOF(
    const_expr:
      int64_value: 13
  )EOF"));

  envoy::config::rbac::v2::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v2::RBAC_Action::RBAC_Action_ALLOW);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);
  checkEngine(engine, false);
}

TEST(RoleBasedAccessControlEngineImpl, ErrorCondition) {
  envoy::config::rbac::v2::Policy policy;
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

  envoy::config::rbac::v2::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v2::RBAC_Action::RBAC_Action_ALLOW);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);
  checkEngine(engine, false, Envoy::Network::MockConnection());
}

TEST(RoleBasedAccessControlEngineImpl, HeaderCondition) {
  envoy::config::rbac::v2::Policy policy;
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

  envoy::config::rbac::v2::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v2::RBAC_Action::RBAC_Action_ALLOW);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);

  Envoy::Http::HeaderMapImpl headers;
  Envoy::Http::LowerCaseString key("foo");
  std::string value = "bar";
  headers.setReference(key, value);

  checkEngine(engine, true, Envoy::Network::MockConnection(), headers);
}

TEST(RoleBasedAccessControlEngineImpl, MetadataCondition) {
  envoy::config::rbac::v2::Policy policy;
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

  envoy::config::rbac::v2::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v2::RBAC_Action::RBAC_Action_ALLOW);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);

  Envoy::Http::HeaderMapImpl headers;

  auto label = MessageUtil::keyValueStruct("label", "prod");
  envoy::api::v2::core::Metadata metadata;
  metadata.mutable_filter_metadata()->insert(
      Protobuf::MapPair<std::string, ProtobufWkt::Struct>("other", label));

  checkEngine(engine, true, Envoy::Network::MockConnection(), headers, metadata);
}

TEST(RoleBasedAccessControlEngineImpl, ConjunctiveCondition) {
  envoy::config::rbac::v2::Policy policy;
  policy.add_permissions()->set_destination_port(123);
  policy.add_principals()->set_any(true);
  policy.mutable_condition()->MergeFrom(
      TestUtility::parseYaml<google::api::expr::v1alpha1::Expr>(R"EOF(
    const_expr:
      bool_value: false
  )EOF"));

  envoy::config::rbac::v2::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v2::RBAC_Action::RBAC_Action_ALLOW);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac);

  Envoy::Network::MockConnection conn;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  EXPECT_CALL(conn, localAddress()).WillOnce(ReturnRef(addr));
  checkEngine(engine, false, conn);
}

} // namespace
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
