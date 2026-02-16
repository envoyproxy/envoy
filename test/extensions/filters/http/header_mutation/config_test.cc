#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/filters/http/header_mutation/config.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderMutation {
namespace {

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

    EXPECT_NE(nullptr, factory
                           ->createRouteSpecificFilterConfig(
                               per_route_proto_config, mock_factory_context.server_factory_context_,
                               mock_factory_context.messageValidationVisitor())
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
    EXPECT_FALSE(cb_or_error.status().ok());
    EXPECT_EQ("One of 'append'/'remove' must be specified.", cb_or_error.status().message());
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
    EXPECT_FALSE(cb_or_error.status().ok());
    EXPECT_EQ("Only one of 'append'/'remove can be specified.", cb_or_error.status().message());
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
    EXPECT_FALSE(cb_or_error.status().ok());
    EXPECT_EQ("No record specified for append mutation.", cb_or_error.status().message());
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
    EXPECT_FALSE(cb_or_error.status().ok());
    EXPECT_EQ("Only string value is allowed for record value.", cb_or_error.status().message());
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
    EXPECT_FALSE(cb_or_error.status().ok());
    EXPECT_EQ("Only string value is allowed for record value.", cb_or_error.status().message());
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

  auto cb = factory->createFilterFactoryFromProtoWithServerContext(proto_config, "test",
                                                                   mock_server_context);
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));
  cb(filter_callbacks);
}

} // namespace
} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
