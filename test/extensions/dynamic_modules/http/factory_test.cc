#include "source/extensions/filters/http/dynamic_modules/factory.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

TEST(DynamicModuleConfigFactory, Overrides) {
  Envoy::Server::Configuration::DynamicModuleConfigFactory factory;
  EXPECT_EQ(factory.name(), "envoy.extensions.filters.http.dynamic_modules");
  auto empty_config = factory.createEmptyConfigProto();
  EXPECT_NE(empty_config, nullptr);
}

TEST(DynamicModuleConfigFactory, LoadOK) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter config;
  const std::string yaml = R"EOF(
dynamic_module_config:
    name: no_op
    do_not_close: true
    load_globally: false
filter_name: foo
filter_config:
    "@type": "type.googleapis.com/google.protobuf.StringValue"
    value: "bar"
)EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  Api::ApiPtr api = Api::createApiForTest();
  EXPECT_CALL(context.server_factory_context_, api()).WillRepeatedly(testing::ReturnRef(*api));
  ON_CALL(context.server_factory_context_.options_, concurrency())
      .WillByDefault(testing::Return(1));

  Envoy::Server::Configuration::DynamicModuleConfigFactory factory;
  auto result = factory.createFilterFactoryFromProto(proto_config, "", context);
  EXPECT_TRUE(result.ok());
  auto factory_cb = result.value();
  NiceMock<Http::MockFilterChainFactoryCallbacks> callbacks;

  NiceMock<Event::MockDispatcher> dispatcher{"worker_0"};
  ON_CALL(callbacks, dispatcher()).WillByDefault(ReturnRef(dispatcher));

  EXPECT_CALL(callbacks, addStreamFilter(testing::_));
  factory_cb(callbacks);
}

TEST(DynamicModuleConfigFactory, LoadOKBasedOnServerContext) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter config;
  const std::string yaml = R"EOF(
dynamic_module_config:
    name: no_op
    do_not_close: true
    load_globally: false
filter_name: foo
filter_config:
    "@type": "type.googleapis.com/google.protobuf.StringValue"
    value: "bar"
)EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  Api::ApiPtr api = Api::createApiForTest();
  EXPECT_CALL(context.server_factory_context_, api()).WillRepeatedly(testing::ReturnRef(*api));
  ON_CALL(context.server_factory_context_.options_, concurrency())
      .WillByDefault(testing::Return(1));

  Envoy::Server::Configuration::DynamicModuleConfigFactory factory;
  auto factory_cb = factory.createFilterFactoryFromProtoWithServerContext(
      proto_config, "", context.server_factory_context_);
  NiceMock<Http::MockFilterChainFactoryCallbacks> callbacks;

  NiceMock<Event::MockDispatcher> dispatcher{"worker_0"};
  ON_CALL(callbacks, dispatcher()).WillByDefault(ReturnRef(dispatcher));

  EXPECT_CALL(callbacks, addStreamFilter(testing::_));
  factory_cb(callbacks);
}

TEST(DynamicModuleConfigFactory, LoadEmpty) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter config;
  const std::string yaml = R"EOF(
dynamic_module_config:
    name: no_op
    do_not_close: true
    load_globally: true
filter_name: foo
)EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  Api::ApiPtr api = Api::createApiForTest();
  EXPECT_CALL(context.server_factory_context_, api()).WillRepeatedly(testing::ReturnRef(*api));
  ON_CALL(context.server_factory_context_.options_, concurrency())
      .WillByDefault(testing::Return(1));

  Envoy::Server::Configuration::DynamicModuleConfigFactory factory;
  auto result = factory.createFilterFactoryFromProto(proto_config, "", context);
  EXPECT_TRUE(result.ok());
  auto factory_cb = result.value();
  NiceMock<Http::MockFilterChainFactoryCallbacks> callbacks;

  NiceMock<Event::MockDispatcher> dispatcher{"worker_0"};
  ON_CALL(callbacks, dispatcher()).WillByDefault(ReturnRef(dispatcher));

  EXPECT_CALL(callbacks, addStreamFilter(testing::_));
  factory_cb(callbacks);
}

TEST(DynamicModuleConfigFactory, LoadBytes) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter config;
  const std::string yaml = R"EOF(
dynamic_module_config:
    name: no_op
    do_not_close: true
    load_globally: true
filter_name: foo
filter_config:
    "@type": "type.googleapis.com/google.protobuf.BytesValue"
    value: "YmFy" # echo -n "bar" | base64
)EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  Api::ApiPtr api = Api::createApiForTest();
  EXPECT_CALL(context.server_factory_context_, api()).WillRepeatedly(testing::ReturnRef(*api));
  ON_CALL(context.server_factory_context_.options_, concurrency())
      .WillByDefault(testing::Return(1));

  Envoy::Server::Configuration::DynamicModuleConfigFactory factory;
  auto result = factory.createFilterFactoryFromProto(proto_config, "", context);
  EXPECT_TRUE(result.ok());
  auto factory_cb = result.value();
  NiceMock<Http::MockFilterChainFactoryCallbacks> callbacks;

  NiceMock<Event::MockDispatcher> dispatcher{"worker_0"};
  ON_CALL(callbacks, dispatcher()).WillByDefault(ReturnRef(dispatcher));

  EXPECT_CALL(callbacks, addStreamFilter(testing::_));
  factory_cb(callbacks);
}

TEST(DynamicModuleConfigFactory, LoadError) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

  // Non existent module.
  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter config;
  const std::string yaml = R"EOF(
dynamic_module_config:
    name: something-not-exist
filter_name: foo
filter_config:
    "@type": "type.googleapis.com/google.protobuf.StringValue"
    value: "bar"
)EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  Envoy::Server::Configuration::DynamicModuleConfigFactory factory;
  auto result = factory.createFilterFactoryFromProto(proto_config, "", context);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Failed to load dynamic module:"));

  // Test cases for missing symbols.
  std::vector<std::pair<std::string, std::string>> test_cases = {
      {"no_http_config_new", "envoy_dynamic_module_on_http_filter_config_new"},
      {"no_http_config_destroy", "envoy_dynamic_module_on_http_filter_config_destroy"},
      {"no_http_filter_new", "envoy_dynamic_module_on_http_filter_new"},
      {"no_http_filter_request_headers", "envoy_dynamic_module_on_http_filter_request_headers"},
      {"no_http_filter_request_body", "envoy_dynamic_module_on_http_filter_request_body"},
      {"no_http_filter_request_trailers", "envoy_dynamic_module_on_http_filter_request_trailers"},
      {"no_http_filter_response_headers", "envoy_dynamic_module_on_http_filter_response_headers"},
      {"no_http_filter_response_body", "envoy_dynamic_module_on_http_filter_response_body"},
      {"no_http_filter_response_trailers", "envoy_dynamic_module_on_http_filter_response_trailers"},
      {"no_http_filter_stream_complete", "envoy_dynamic_module_on_http_filter_stream_complete"},
      {"no_http_filter_destroy", "envoy_dynamic_module_on_http_filter_destroy"},
  };

  for (const auto& test_case : test_cases) {
    const std::string& module_name = test_case.first;
    const std::string& missing_symbol_name = test_case.second;

    const std::string yaml = fmt::format(R"EOF(
dynamic_module_config:
    name: {}
filter_name: foo
filter_config:
    "@type": "type.googleapis.com/google.protobuf.StringValue"
    value: "bar"
)EOF",
                                         module_name);
    envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
    TestUtility::loadFromYamlAndValidate(yaml, proto_config);

    auto result = factory.createFilterFactoryFromProto(proto_config, "", context);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_THAT(
        result.status().message(),
        testing::HasSubstr(fmt::format("Failed to resolve symbol {}", missing_symbol_name)));
  }

  auto symbol_err = [](const std::string& symbol) {
    return fmt::format("Failed to resolve symbol {}", symbol);
  };

  // Test case for per-route config when module fails to load entirely.
  {
    const std::string yaml = R"EOF(
dynamic_module_config:
    name: non-existent-module
per_route_config_name: foo
filter_config:
    "@type": "type.googleapis.com/google.protobuf.StringValue"
    value: "bar"
)EOF";
    envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilterPerRoute proto_config;
    TestUtility::loadFromYamlAndValidate(yaml, proto_config);
    NiceMock<Server::Configuration::MockServerFactoryContext> context;

    auto result = factory.createRouteSpecificFilterConfig(
        proto_config, context, ProtobufMessage::getNullValidationVisitor());
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_THAT(result.status().message(), testing::HasSubstr("Failed to load dynamic module:"));
  }

  std::vector<std::pair<std::string, std::string>> per_route_test_cases = {
      {"no_http_filter_per_route_config_new",
       symbol_err("envoy_dynamic_module_on_http_filter_per_route_config_new")},
      {"no_http_filter_per_route_config_destroy",
       symbol_err("envoy_dynamic_module_on_http_filter_per_route_config_destroy")},
      {"http_filter_per_route_config_new_fail", "Failed to initialize per-route dynamic module"},
  };

  for (const auto& test_case : per_route_test_cases) {
    const std::string& module_name = test_case.first;
    const std::string& expected_error = test_case.second;

    const std::string yaml = fmt::format(R"EOF(
dynamic_module_config:
    name: {}
per_route_config_name: foo
filter_config:
    "@type": "type.googleapis.com/google.protobuf.StringValue"
    value: "bar"
)EOF",
                                         module_name);
    envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilterPerRoute proto_config;
    TestUtility::loadFromYamlAndValidate(yaml, proto_config);
    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    ON_CALL(context.options_, concurrency()).WillByDefault(testing::Return(1));

    auto result = factory.createRouteSpecificFilterConfig(
        proto_config, context, ProtobufMessage::getNullValidationVisitor());
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_THAT(result.status().message(), testing::HasSubstr(expected_error));
  }
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
