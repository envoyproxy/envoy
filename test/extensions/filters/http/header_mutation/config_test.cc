#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
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

class InitManagerRecordingCommandFactory : public Formatter::TestCommandFactory {
public:
  Formatter::CommandParserPtr
  createCommandParserFromProto(const Protobuf::Message& config,
                               Server::Configuration::GenericFactoryContext& context) override {
    init_manager_ = &context.initManager();
    init_manager_->add(init_target_);
    return Formatter::TestCommandFactory::createCommandParserFromProto(config, context);
  }

  Init::Manager* initManager() const { return init_manager_; }

private:
  Init::Manager* init_manager_{};
  Init::ExpectableTargetImpl init_target_{"formatter"};
};

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

    const std::string empty_stats_prefix;
    Server::Configuration::ExtraFactoryContext extra_context{
        mock_factory_context.messageValidationVisitor(), empty_stats_prefix,
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

TEST(FactoryTest, UnknownFormatterReturnsErrorAtFilterAndRouteScopes) {
  const std::string config = R"EOF(
  mutations:
    formatters:
    - name: envoy.formatter.TestFormatterUnknown
      typed_config:
        "@type": type.googleapis.com/google.protobuf.Any
  )EOF";

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  HeaderMutationFactoryConfig factory;
  ProtoConfig proto_config;
  TestUtility::loadFromYaml(config, proto_config);
  EXPECT_THAT(factory.createFilterFactoryFromProto(proto_config, "test", context),
              HasStatusMessage("Formatter not found: envoy.formatter.TestFormatterUnknown"));

  PerRouteProtoConfig per_route_proto_config;
  TestUtility::loadFromYaml(config, per_route_proto_config);
  const std::string empty_stats_prefix;
  Server::Configuration::ExtraFactoryContext extra_context{
      context.messageValidationVisitor(), empty_stats_prefix,
      makeOptRef<Init::Manager>(context.init_manager_)};
  EXPECT_THAT(factory.createHttpFilterRouteConfig(per_route_proto_config,
                                                  context.server_factory_context_, extra_context),
              HasStatusMessage("Formatter not found: envoy.formatter.TestFormatterUnknown"));
}

TEST(FactoryTest, FormatterConfigurationIsNotInheritedByRoute) {
  const std::string filter_config = R"EOF(
  mutations:
    request_mutations:
    - append:
        header:
          key: "test-header"
          value: "%COMMAND_EXTENSION()%"
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
          key: "test-header"
          value: "%COMMAND_EXTENSION()%"
  )EOF";

  Formatter::TestCommandFactory command_factory;
  Registry::InjectFactory<Formatter::CommandParserFactory> command_register(command_factory);
  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  HeaderMutationFactoryConfig factory;

  ProtoConfig proto_config;
  TestUtility::loadFromYaml(filter_config, proto_config);
  auto filter_factory = factory.createFilterFactoryFromProto(proto_config, "test", context);
  ASSERT_TRUE(filter_factory.ok()) << filter_factory.status();

  PerRouteProtoConfig per_route_proto_config;
  TestUtility::loadFromYaml(route_config, per_route_proto_config);
  const std::string empty_stats_prefix;
  Server::Configuration::ExtraFactoryContext extra_context{
      context.messageValidationVisitor(), empty_stats_prefix,
      makeOptRef<Init::Manager>(context.init_manager_)};
  EXPECT_THAT(factory.createHttpFilterRouteConfig(per_route_proto_config,
                                                  context.server_factory_context_, extra_context),
              HasStatusMessage("Not supported field in StreamInfo: COMMAND_EXTENSION"));
}

TEST(FactoryTest, FilterFormatterUsesFilterInitManager) {
  const std::string config = R"EOF(
  mutations:
    request_mutations:
    - append:
        header:
          key: "test-header"
          value: "%COMMAND_EXTENSION()%"
    formatters:
    - name: envoy.formatter.TestFormatter
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
  )EOF";

  InitManagerRecordingCommandFactory command_factory;
  Registry::InjectFactory<Formatter::CommandParserFactory> command_register(command_factory);
  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context.init_manager_, add(_));

  ProtoConfig proto_config;
  TestUtility::loadFromYaml(config, proto_config);
  HeaderMutationFactoryConfig factory;
  auto filter_config = factory.createFilterFactoryFromProto(proto_config, "test", context);
  EXPECT_TRUE(filter_config.ok()) << filter_config.status();
  EXPECT_EQ(command_factory.initManager(), &context.init_manager_);
}

TEST(FactoryTest, RouteFormatterUsesRouteInitManager) {
  const std::string config = R"EOF(
  mutations:
    request_mutations:
    - append:
        header:
          key: "test-header"
          value: "%COMMAND_EXTENSION()%"
    formatters:
    - name: envoy.formatter.TestFormatter
      typed_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
  )EOF";

  InitManagerRecordingCommandFactory command_factory;
  Registry::InjectFactory<Formatter::CommandParserFactory> command_register(command_factory);
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  testing::StrictMock<Init::MockManager> route_init_manager;
  EXPECT_CALL(route_init_manager, add(_));

  PerRouteProtoConfig proto_config;
  TestUtility::loadFromYaml(config, proto_config);
  const std::string empty_stats_prefix;
  Server::Configuration::ExtraFactoryContext extra_context{
      context.messageValidationVisitor(), empty_stats_prefix,
      makeOptRef<Init::Manager>(route_init_manager)};

  HeaderMutationFactoryConfig factory;
  auto route_config = factory.createHttpFilterRouteConfig(proto_config, context, extra_context);
  EXPECT_TRUE(route_config.ok()) << route_config.status();
  EXPECT_EQ(command_factory.initManager(), &route_init_manager);
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

} // namespace
} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
