#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/config/rbac/v3/rbac.pb.validate.h"
#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/common/rbac/engine_impl.h"
#include "source/extensions/filters/http/rbac/rbac_filter.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Const;
using testing::ReturnPointee;
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

void checkMatcherEngine(
    RBAC::RoleBasedAccessControlMatcherEngineImpl& engine, bool expected, LogResult expected_log,
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

void checkMatcherEngine(
    RBAC::RoleBasedAccessControlMatcherEngineImpl& engine, bool expected, LogResult expected_log,
    const Envoy::Network::Connection& connection,
    const Envoy::Http::RequestHeaderMap& headers = Envoy::Http::TestRequestHeaderMapImpl()) {
  NiceMock<StreamInfo::MockStreamInfo> empty_info;
  checkMatcherEngine(engine, expected, expected_log, empty_info, connection, headers);
}

void onMetadata(NiceMock<StreamInfo::MockStreamInfo>& info) {
  ON_CALL(info, setDynamicMetadata("envoy.common", _))
      .WillByDefault(Invoke([&info](const std::string&, const ProtobufWkt::Struct& obj) {
        (*info.metadata_.mutable_filter_metadata())["envoy.common"] = obj;
      }));
}

TEST(RoleBasedAccessControlEngineImpl, Disabled) {
  Server::Configuration::MockServerFactoryContext factory_context;
  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
  RBAC::RoleBasedAccessControlEngineImpl engine_allow(
      rbac, ProtobufMessage::getStrictValidationVisitor(), factory_context);
  checkEngine(engine_allow, false, LogResult::Undecided);

  rbac.set_action(envoy::config::rbac::v3::RBAC::DENY);
  RBAC::RoleBasedAccessControlEngineImpl engine_deny(
      rbac, ProtobufMessage::getStrictValidationVisitor(), factory_context);
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
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  envoy::config::rbac::v3::Policy policy;
  policy.add_permissions()->set_destination_port(123);
  policy.add_principals()->set_any(true);

  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac, ProtobufMessage::getStrictValidationVisitor(),
                                                factory_context);

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 123, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);
  checkEngine(engine, true, LogResult::Undecided, info, conn, headers);

  addr = Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 456, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);
  checkEngine(engine, false, LogResult::Undecided, info, conn, headers);
}

TEST(RoleBasedAccessControlEngineImpl, DeniedDenylist) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  envoy::config::rbac::v3::Policy policy;
  policy.add_permissions()->set_destination_port(123);
  policy.add_principals()->set_any(true);

  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::DENY);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac, ProtobufMessage::getStrictValidationVisitor(),
                                                factory_context);

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 123, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);
  checkEngine(engine, false, LogResult::Undecided, info, conn, headers);

  addr = Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 456, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);
  checkEngine(engine, true, LogResult::Undecided, info, conn, headers);
}

TEST(RoleBasedAccessControlEngineImpl, BasicCondition) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
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
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac, ProtobufMessage::getStrictValidationVisitor(),
                                                factory_context);
  checkEngine(engine, false, LogResult::Undecided);
}

TEST(RoleBasedAccessControlEngineImpl, MalformedCondition) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
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

  EXPECT_THROW_WITH_REGEX(RBAC::RoleBasedAccessControlEngineImpl engine(
                              rbac, ProtobufMessage::getStrictValidationVisitor(), factory_context),
                          EnvoyException, "failed to create an expression: .*");

  rbac.set_action(envoy::config::rbac::v3::RBAC::LOG);
  EXPECT_THROW_WITH_REGEX(RBAC::RoleBasedAccessControlEngineImpl engine_log(
                              rbac, ProtobufMessage::getStrictValidationVisitor(), factory_context),
                          EnvoyException, "failed to create an expression: .*");
}

TEST(RoleBasedAccessControlEngineImpl, MistypedCondition) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
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
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac, ProtobufMessage::getStrictValidationVisitor(),
                                                factory_context);
  checkEngine(engine, false, LogResult::Undecided);
}

TEST(RoleBasedAccessControlEngineImpl, EvaluationFailure) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
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
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac, ProtobufMessage::getStrictValidationVisitor(),
                                                factory_context);
  checkEngine(engine, false, LogResult::Undecided);
}

TEST(RoleBasedAccessControlEngineImpl, ErrorCondition) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
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
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac, ProtobufMessage::getStrictValidationVisitor(),
                                                factory_context);
  checkEngine(engine, false, LogResult::Undecided, Envoy::Network::MockConnection());
}

TEST(RoleBasedAccessControlEngineImpl, HeaderCondition) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
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
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac, ProtobufMessage::getStrictValidationVisitor(),
                                                factory_context);

  Envoy::Http::TestRequestHeaderMapImpl headers;
  Envoy::Http::LowerCaseString key("foo");
  std::string value = "bar";
  headers.setReference(key, value);

  checkEngine(engine, true, LogResult::Undecided, Envoy::Network::MockConnection(), headers);
}

TEST(RoleBasedAccessControlEngineImpl, MetadataCondition) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
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
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac, ProtobufMessage::getStrictValidationVisitor(),
                                                factory_context);

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
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
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
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac, ProtobufMessage::getStrictValidationVisitor(),
                                                factory_context);

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 123, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);
  checkEngine(engine, false, LogResult::Undecided, info, conn, headers);
}

TEST(RoleBasedAccessControlMatcherEngineImpl, Disabled) {
  xds::type::matcher::v3::Matcher matcher;

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  HttpFilters::RBACFilter::ActionValidationVisitor validation_visitor;
  RBAC::RoleBasedAccessControlMatcherEngineImpl engine(matcher, factory_context,
                                                       validation_visitor);

  NiceMock<Envoy::Network::MockConnection> conn;
  checkMatcherEngine(engine, false, LogResult::Undecided, conn);
}

TEST(RoleBasedAccessControlMatcherEngineImpl, AllowedAllowlist) {
  envoy::extensions::matching::common_inputs::network::v3::DestinationPortInput input;
  envoy::config::rbac::v3::Action allow_action;
  allow_action.set_name("allow");
  allow_action.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
  envoy::config::rbac::v3::Action deny_action;
  deny_action.set_name("deny");
  deny_action.set_action(envoy::config::rbac::v3::RBAC::DENY);

  xds::type::matcher::v3::Matcher matcher;
  auto matcher_matcher = matcher.mutable_matcher_list()->mutable_matchers()->Add();
  auto matcher_action = matcher_matcher->mutable_on_match()->mutable_action();
  matcher_action->set_name("action");
  matcher_action->mutable_typed_config()->PackFrom(allow_action);
  auto matcher_predicate = matcher_matcher->mutable_predicate()->mutable_single_predicate();
  auto matcher_input = matcher_predicate->mutable_input();
  matcher_input->set_name("envoy.matching.inputs.destination_port");
  matcher_input->mutable_typed_config()->PackFrom(input);
  matcher_predicate->mutable_value_match()->set_exact("123");

  auto matcher_on_no_match_action = matcher.mutable_on_no_match()->mutable_action();
  matcher_on_no_match_action->set_name("action");
  matcher_on_no_match_action->mutable_typed_config()->PackFrom(deny_action);

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  HttpFilters::RBACFilter::ActionValidationVisitor validation_visitor;
  RBAC::RoleBasedAccessControlMatcherEngineImpl engine(matcher, factory_context,
                                                       validation_visitor);

  NiceMock<Envoy::Network::MockConnection> conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  EXPECT_CALL(conn, streamInfo()).WillRepeatedly(ReturnRef(stream_info));

  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 123, false);
  stream_info.downstream_connection_info_provider_->setLocalAddress(addr);

  EXPECT_CALL(stream_info, downstreamAddressProvider())
      .WillRepeatedly(ReturnRef(stream_info.downstreamAddressProvider()));

  checkMatcherEngine(engine, true, LogResult::Undecided, stream_info, conn, headers);

  addr = Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 456, false);
  stream_info.downstream_connection_info_provider_->setLocalAddress(addr);
  checkMatcherEngine(engine, false, LogResult::Undecided, stream_info, conn, headers);
}

TEST(RoleBasedAccessControlMatcherEngineImpl, DeniedDenylist) {
  envoy::extensions::matching::common_inputs::network::v3::DestinationPortInput input;
  envoy::config::rbac::v3::Action allow_action;
  allow_action.set_name("allow");
  allow_action.set_action(envoy::config::rbac::v3::RBAC::ALLOW);
  envoy::config::rbac::v3::Action deny_action;
  deny_action.set_name("deny");
  deny_action.set_action(envoy::config::rbac::v3::RBAC::DENY);

  xds::type::matcher::v3::Matcher matcher;
  auto matcher_matcher = matcher.mutable_matcher_list()->mutable_matchers()->Add();
  auto matcher_action = matcher_matcher->mutable_on_match()->mutable_action();
  matcher_action->set_name("action");
  matcher_action->mutable_typed_config()->PackFrom(deny_action);
  auto matcher_predicate = matcher_matcher->mutable_predicate()->mutable_single_predicate();
  auto matcher_input = matcher_predicate->mutable_input();
  matcher_input->set_name("envoy.matching.inputs.destination_port");
  matcher_input->mutable_typed_config()->PackFrom(input);
  matcher_predicate->mutable_value_match()->set_exact("123");

  auto matcher_on_no_match_action = matcher.mutable_on_no_match()->mutable_action();
  matcher_on_no_match_action->set_name("action");
  matcher_on_no_match_action->mutable_typed_config()->PackFrom(allow_action);

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  HttpFilters::RBACFilter::ActionValidationVisitor validation_visitor;
  RBAC::RoleBasedAccessControlMatcherEngineImpl engine(matcher, factory_context,
                                                       validation_visitor);

  NiceMock<Envoy::Network::MockConnection> conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;

  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 123, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);
  EXPECT_CALL(info, downstreamAddressProvider())
      .WillRepeatedly(ReturnPointee(info.downstream_connection_info_provider_));
  checkMatcherEngine(engine, false, LogResult::Undecided, info, conn, headers);

  addr = Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 456, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);
  checkMatcherEngine(engine, true, LogResult::Undecided, info, conn, headers);
}

// Log tests
TEST(RoleBasedAccessControlEngineImpl, DisabledLog) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  NiceMock<StreamInfo::MockStreamInfo> info;
  onMetadata(info);

  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::LOG);
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac, ProtobufMessage::getStrictValidationVisitor(),
                                                factory_context);
  checkEngine(engine, true, RBAC::LogResult::No, info);
}

TEST(RoleBasedAccessControlEngineImpl, LogIfMatched) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  envoy::config::rbac::v3::Policy policy;
  policy.add_permissions()->set_destination_port(123);
  policy.add_principals()->set_any(true);

  envoy::config::rbac::v3::RBAC rbac;
  rbac.set_action(envoy::config::rbac::v3::RBAC::LOG);
  (*rbac.mutable_policies())["foo"] = policy;
  RBAC::RoleBasedAccessControlEngineImpl engine(rbac, ProtobufMessage::getStrictValidationVisitor(),
                                                factory_context);

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  onMetadata(info);

  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 123, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);
  checkEngine(engine, true, RBAC::LogResult::Yes, info, conn, headers);

  addr = Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 456, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);
  checkEngine(engine, true, RBAC::LogResult::No, info, conn, headers);
}

TEST(RoleBasedAccessControlMatcherEngineImpl, LogIfMatched) {
  envoy::extensions::matching::common_inputs::network::v3::DestinationPortInput input;
  envoy::config::rbac::v3::Action log_action;
  log_action.set_name("log");
  log_action.set_action(envoy::config::rbac::v3::RBAC::LOG);
  envoy::config::rbac::v3::Action allow_action;
  allow_action.set_name("allow");
  allow_action.set_action(envoy::config::rbac::v3::RBAC::ALLOW);

  xds::type::matcher::v3::Matcher matcher;
  auto matcher_matcher = matcher.mutable_matcher_list()->mutable_matchers()->Add();
  auto matcher_action = matcher_matcher->mutable_on_match()->mutable_action();
  matcher_action->set_name("action");
  matcher_action->mutable_typed_config()->PackFrom(log_action);
  auto matcher_predicate = matcher_matcher->mutable_predicate()->mutable_single_predicate();
  auto matcher_input = matcher_predicate->mutable_input();
  matcher_input->set_name("envoy.matching.inputs.destination_port");
  matcher_input->mutable_typed_config()->PackFrom(input);
  matcher_predicate->mutable_value_match()->set_exact("123");

  auto matcher_on_no_match_action = matcher.mutable_on_no_match()->mutable_action();
  matcher_on_no_match_action->set_name("action");
  matcher_on_no_match_action->mutable_typed_config()->PackFrom(allow_action);

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  HttpFilters::RBACFilter::ActionValidationVisitor validation_visitor;
  RBAC::RoleBasedAccessControlMatcherEngineImpl engine(matcher, factory_context,
                                                       validation_visitor);

  NiceMock<Envoy::Network::MockConnection> conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  onMetadata(info);

  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 123, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);
  EXPECT_CALL(info, downstreamAddressProvider())
      .WillRepeatedly(ReturnPointee(info.downstream_connection_info_provider_));
  EXPECT_CALL(conn, streamInfo()).WillRepeatedly(ReturnRef(info));
  checkMatcherEngine(engine, true, RBAC::LogResult::Yes, info, conn, headers);

  addr = Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 456, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);
  checkMatcherEngine(engine, true, RBAC::LogResult::No, info, conn, headers);
}

} // namespace
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
