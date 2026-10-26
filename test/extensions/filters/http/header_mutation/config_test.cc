#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/header_mutation/config.h"

#include "test/common/formatter/command_extension.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderMutation {
namespace {

using ::Envoy::StatusHelpers::HasStatusMessage;

TEST(FactoryTest, FactoryTest) {
  testing::NiceMock<Server::Configuration::MockFactoryContext> mock_factory_context;
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          "envoy.filters.http.header_mutation");
  ASSERT_NE(factory, nullptr);

  {
    const std::string config = R"EOF(
  mutations:
    request_mutations:
    - remove: "flag-header"
    - append:
        header:
          key: "flag-header"
          value: "%REQ(ANOTHER-FLAG-HEADER)%"
        append_action: APPEND_IF_EXISTS_OR_ADD
    query_parameter_mutations:
    - remove: "flag-query"
    - append:
        record:
          key: "flag-query"
          value: "%REQ(ANOTHER-FLAG-QUERY)%"
        action: APPEND_IF_EXISTS_OR_ADD
    response_mutations:
    - remove: "flag-header"
    - append:
        header:
          key: "flag-header"
          value: "%REQ(ANOTHER-FLAG-HEADER)%"
        append_action: APPEND_IF_EXISTS_OR_ADD
    request_trailers_mutations:
    - remove: "request-trailer"
    - append:
        header:
          key: "request-trailer"
          value: "value"
        append_action: "APPEND_IF_EXISTS_OR_ADD"
    response_trailers_mutations:
    - remove: "flag-trailer"
    - append:
        header:
          key: "flag-trailer"
          value: "hardcoded-value"
        append_action: "APPEND_IF_EXISTS_OR_ADD"
  )EOF";

    PerRouteProtoConfig per_route_proto_config;
    TestUtility::loadFromYaml(config, per_route_proto_config);
    ProtoConfig proto_config;
    TestUtility::loadFromYaml(config, proto_config);

    auto cb =
        factory->createFilterFactoryFromProto(proto_config, "test", mock_factory_context).value();
    Http::MockFilterChainFactoryCallbacks filter_callbacks;
    EXPECT_CALL(filter_callbacks, addStreamFilter(_));
    cb(filter_callbacks);

    Server::Configuration::ExtraFactoryContext extra_context{
        mock_factory_context.messageValidationVisitor(), "",
        makeOptRef<Init::Manager>(mock_factory_context.init_manager_)};
    EXPECT_NE(nullptr, factory
                           ->createHttpFilterRouteConfig(
                               per_route_proto_config, mock_factory_context.server_factory_context_,
                               extra_context)
                           .value());
  }

  {
    const std::string config = R"EOF(
  mutations:
    query_parameter_mutations:
    - remove: ""
  )EOF";

    ProtoConfig proto_config;
    TestUtility::loadFromYaml(config, proto_config);

    auto cb_or_error =
        factory->createFilterFactoryFromProto(proto_config, "test", mock_factory_context);
    EXPECT_THAT(cb_or_error, HasStatusMessage("One of 'append'/'remove' must be specified."));
  }

  {
    const std::string config = R"EOF(
  mutations:
    query_parameter_mutations:
    - remove: "another-key"
      append:
        record:
          key: "key"
          value: "value"
  )EOF";

    ProtoConfig proto_config;
    TestUtility::loadFromYaml(config, proto_config);

    auto cb_or_error =
        factory->createFilterFactoryFromProto(proto_config, "test", mock_factory_context);
    EXPECT_THAT(cb_or_error, HasStatusMessage("Only one of 'append'/'remove can be specified."));
  }

  {
    const std::string config = R"EOF(
  mutations:
    query_parameter_mutations:
    - append: {}
  )EOF";

    ProtoConfig proto_config;
    TestUtility::loadFromYaml(config, proto_config);

    auto cb_or_error =
        factory->createFilterFactoryFromProto(proto_config, "test", mock_factory_context);
    EXPECT_THAT(cb_or_error, HasStatusMessage("No record specified for append mutation."));
  }
  {
    const std::string config = R"EOF(
  mutations:
    query_parameter_mutations:
    - append:
        record:
          key: "key"
          value: 123
  )EOF";

    ProtoConfig proto_config;
    TestUtility::loadFromYaml(config, proto_config);

    auto cb_or_error =
        factory->createFilterFactoryFromProto(proto_config, "test", mock_factory_context);
    EXPECT_THAT(cb_or_error, HasStatusMessage("Only string value is allowed for record value."));
  }
  {
    const std::string config = R"EOF(
  mutations:
    query_parameter_mutations:
    - append:
        record:
          key: "key"
  )EOF";

    ProtoConfig proto_config;
    TestUtility::loadFromYaml(config, proto_config);

    auto cb_or_error =
        factory->createFilterFactoryFromProto(proto_config, "test", mock_factory_context);
    EXPECT_THAT(cb_or_error, HasStatusMessage("Only string value is allowed for record value."));
  }
}

TEST(FactoryTest, UpstreamFactoryTest) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::UpstreamHttpFilterConfigFactory>::getFactory(
          "envoy.filters.http.header_mutation");
  ASSERT_NE(factory, nullptr);
}

TEST(FactoryTest, QueryParameterMutationsTest) {

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;

  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          "envoy.filters.http.header_mutation");
  ASSERT_NE(factory, nullptr);
}

TEST(FactoryTest, FactoryTestWithServerContext) {
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> mock_server_context;
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          "envoy.filters.http.header_mutation");
  ASSERT_NE(factory, nullptr);

  const std::string config = R"EOF(
  mutations:
    request_mutations:
    - remove: "flag-header"
    - append:
        header:
          key: "flag-header"
          value: "%REQ(ANOTHER-FLAG-HEADER)%"
        append_action: APPEND_IF_EXISTS_OR_ADD
  )EOF";

  ProtoConfig proto_config;
  TestUtility::loadFromYaml(config, proto_config);

  // The typed createHttpFilterFactoryFromProto overload is only visible on the concrete factory
  // type (the base NamedHttpFilterConfigFactory pointer exposes only the Protobuf::Message
  // overload, which routes through the legacy path).
  HeaderMutationFactoryConfig header_mutation_factory;
  Server::Configuration::ExtraFactoryContext extra_context{
      mock_server_context.messageValidationVisitor(), "test"};
  auto cb = header_mutation_factory
                .createHttpFilterFactoryFromProto(proto_config, mock_server_context, extra_context)
                .value();
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));
  cb(filter_callbacks);
}

class ContextRecordingCommandFactory : public Formatter::TestCommandFactory {
public:
  Formatter::CommandParserPtr
  createCommandParserFromProto(const Protobuf::Message& message,
                               Server::Configuration::GenericFactoryContext& context) override {
    init_manager_ = &context.initManager();
    scope_ = &context.scope();
    return Formatter::TestCommandFactory::createCommandParserFromProto(message, context);
  }

  Init::Manager* init_manager_{};
  Stats::Scope* scope_{};
};

TEST(FactoryTest, CustomFormatterUsesFactoryContextTest) {
  ContextRecordingCommandFactory formatter_factory;
  Registry::InjectFactory<Formatter::CommandParserFactory> register_factory(formatter_factory);
  testing::NiceMock<Server::Configuration::MockFactoryContext> mock_factory_context;
  HeaderMutationFactoryConfig factory;

  const std::string config = R"EOF(
  mutations:
    request_mutations:
    - append:
        header:
          key: "flag-header"
          value: "%COMMAND_EXTENSION()%"
        append_action: APPEND_IF_EXISTS_OR_ADD
    formatters:
    - name: envoy.formatter.TestFormatter
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
  )EOF";

  ProtoConfig proto_config;
  TestUtility::loadFromYaml(config, proto_config);
  EXPECT_OK(factory.createFilterFactoryFromProto(proto_config, "test", mock_factory_context));
  EXPECT_EQ(&mock_factory_context.init_manager_, formatter_factory.init_manager_);
  EXPECT_EQ(&mock_factory_context.scope_, formatter_factory.scope_);

  PerRouteProtoConfig per_route_proto_config;
  TestUtility::loadFromYaml(config, per_route_proto_config);
  testing::StrictMock<Init::MockManager> route_init_manager;
  Server::Configuration::ExtraFactoryContext extra_context{
      mock_factory_context.messageValidationVisitor(), "",
      makeOptRef<Init::Manager>(route_init_manager)};
  EXPECT_OK(factory.createHttpFilterRouteConfig(
      per_route_proto_config, mock_factory_context.server_factory_context_, extra_context));
  EXPECT_EQ(&route_init_manager, formatter_factory.init_manager_);
}

TEST(FactoryTest, UnknownFormatterTest) {
  const std::string config = R"EOF(
  mutations:
    formatters:
    - name: envoy.formatter.TestFormatterUnknown
      typed_config:
        "@type": type.googleapis.com/google.protobuf.Any
  )EOF";

  testing::NiceMock<Server::Configuration::MockFactoryContext> mock_factory_context;
  HeaderMutationFactoryConfig factory;

  ProtoConfig proto_config;
  TestUtility::loadFromYaml(config, proto_config);
  EXPECT_THAT(factory.createFilterFactoryFromProto(proto_config, "test", mock_factory_context),
              HasStatusMessage("Formatter not found: envoy.formatter.TestFormatterUnknown"));

  PerRouteProtoConfig per_route_proto_config;
  TestUtility::loadFromYaml(config, per_route_proto_config);
  Server::Configuration::ExtraFactoryContext extra_context{
      mock_factory_context.messageValidationVisitor(), "",
      makeOptRef<Init::Manager>(mock_factory_context.init_manager_)};
  EXPECT_THAT(factory.createHttpFilterRouteConfig(per_route_proto_config,
                                                  mock_factory_context.server_factory_context_,
                                                  extra_context),
              HasStatusMessage("Formatter not found: envoy.formatter.TestFormatterUnknown"));
}

TEST(FactoryTest, FormattersNotInheritedByRouteTest) {
  Formatter::TestCommandFactory formatter_factory;
  Registry::InjectFactory<Formatter::CommandParserFactory> register_factory(formatter_factory);
  testing::NiceMock<Server::Configuration::MockFactoryContext> mock_factory_context;
  HeaderMutationFactoryConfig factory;

  const std::string filter_config = R"EOF(
  mutations:
    request_mutations:
    - append:
        header:
          key: "flag-header"
          value: "%COMMAND_EXTENSION()%"
        append_action: APPEND_IF_EXISTS_OR_ADD
    formatters:
    - name: envoy.formatter.TestFormatter
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
  )EOF";
  const std::string route_config = R"EOF(
  mutations:
    request_mutations:
    - append:
        header:
          key: "flag-header"
          value: "%COMMAND_EXTENSION()%"
        append_action: APPEND_IF_EXISTS_OR_ADD
  )EOF";

  ProtoConfig proto_config;
  TestUtility::loadFromYaml(filter_config, proto_config);
  EXPECT_OK(factory.createFilterFactoryFromProto(proto_config, "test", mock_factory_context));

  PerRouteProtoConfig per_route_proto_config;
  TestUtility::loadFromYaml(route_config, per_route_proto_config);
  Server::Configuration::ExtraFactoryContext extra_context{
      mock_factory_context.messageValidationVisitor(), "",
      makeOptRef<Init::Manager>(mock_factory_context.init_manager_)};
  EXPECT_THAT(factory.createHttpFilterRouteConfig(per_route_proto_config,
                                                  mock_factory_context.server_factory_context_,
                                                  extra_context),
              HasStatusMessage("Not supported field in StreamInfo: COMMAND_EXTENSION"));
}

} // namespace
} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
