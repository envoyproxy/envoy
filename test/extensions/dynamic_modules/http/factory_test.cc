#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.h"

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

  // No config-load failure stats should be emitted on the happy path. The failure counters live on
  // the server scope (so they survive a rejected listener config), not the listener scope.
  auto& server_scope = context.server_factory_context_.scope();
  EXPECT_EQ(0U, failureCounter(server_scope, "module_load_error", "foo"));
  EXPECT_EQ(0U, failureCounter(server_scope, "config_init_error", "foo"));
  EXPECT_EQ(0U, failureCounter(server_scope, "remote_fetch_error", "foo"));
  EXPECT_EQ(0U, failureCounter(server_scope, "per_route_config_error", "foo"));
}

TEST(DynamicModuleConfigFactory, LoadOKPerRoute) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

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

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilterPerRoute proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  Api::ApiPtr api = Api::createApiForTest();
  EXPECT_CALL(context.server_factory_context_, api()).WillRepeatedly(testing::ReturnRef(*api));
  ON_CALL(context.server_factory_context_.options_, concurrency())
      .WillByDefault(testing::Return(1));

  Envoy::Server::Configuration::DynamicModuleConfigFactory factory;
  auto result = factory.createRouteSpecificFilterConfig(
      proto_config, context.server_factory_context_, context.messageValidationVisitor());
}

// The per-route path honors the ``module.local.filename`` data source. With only ``local.filename``
// set (no ``name``), reaching per-route symbol resolution rather than a "Failed to load dynamic
// module" error proves the local file was loaded; the no_op C module simply lacks the per-route
// symbols, so config creation then fails.
TEST(DynamicModuleConfigFactory, PerRouteLocalFileIsLoaded) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
dynamic_module_config:
    module:
      local:
        filename: "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c/libno_op.so"
    do_not_close: true
filter_name: foo
filter_config:
    "@type": "type.googleapis.com/google.protobuf.StringValue"
    value: "bar"
)EOF");

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilterPerRoute proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  Api::ApiPtr api = Api::createApiForTest();
  EXPECT_CALL(context.server_factory_context_, api()).WillRepeatedly(testing::ReturnRef(*api));
  ON_CALL(context.server_factory_context_.options_, concurrency())
      .WillByDefault(testing::Return(1));

  Envoy::Server::Configuration::DynamicModuleConfigFactory factory;
  auto result = factory.createRouteSpecificFilterConfig(
      proto_config, context.server_factory_context_, context.messageValidationVisitor());
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("envoy_dynamic_module_on_http_filter_per_route_config_new"));
}

TEST(DynamicModuleConfigFactory, LoadOKPerRouteWithStruct) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

  // ``google.protobuf.Struct`` per-route configs must be serialized to a JSON string instead of
  // being forwarded as raw protobuf binary, matching the behavior of the regular filter config.
  const std::string yaml = R"EOF(
dynamic_module_config:
    name: no_op
    do_not_close: true
    load_globally: false
filter_name: foo
filter_config:
    "@type": "type.googleapis.com/google.protobuf.Struct"
    value:
        key: value
)EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilterPerRoute proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  Api::ApiPtr api = Api::createApiForTest();
  EXPECT_CALL(context.server_factory_context_, api()).WillRepeatedly(testing::ReturnRef(*api));
  ON_CALL(context.server_factory_context_.options_, concurrency())
      .WillByDefault(testing::Return(1));

  Envoy::Server::Configuration::DynamicModuleConfigFactory factory;
  auto result = factory.createRouteSpecificFilterConfig(
      proto_config, context.server_factory_context_, context.messageValidationVisitor());
}

TEST(DynamicModuleConfigFactory, DEPRECATED_FEATURE_TEST(LoadOKPerRouteWithLegacyName)) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

  const std::string yaml = R"EOF(
dynamic_module_config:
    name: no_op
    do_not_close: true
    load_globally: false
per_route_config_name: foo
filter_config:
    "@type": "type.googleapis.com/google.protobuf.StringValue"
    value: "bar"
)EOF";

  envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilterPerRoute proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  Api::ApiPtr api = Api::createApiForTest();
  EXPECT_CALL(context.server_factory_context_, api()).WillRepeatedly(testing::ReturnRef(*api));
  ON_CALL(context.server_factory_context_.options_, concurrency())
      .WillByDefault(testing::Return(1));

  Envoy::Server::Configuration::DynamicModuleConfigFactory factory;
  auto result = factory.createRouteSpecificFilterConfig(
      proto_config, context.server_factory_context_, context.messageValidationVisitor());
}

TEST(DynamicModuleConfigFactory, LoadOKNoOptionalABI) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

  const std::string yaml = R"EOF(
dynamic_module_config:
    name: no_op_no_optional_abi
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

TEST(DynamicModuleConfigFactory, LoadStruct) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

  const std::string yaml = R"EOF(
dynamic_module_config:
    name: no_op
    do_not_close: true
    load_globally: true
filter_name: foo
filter_config:
    "@type": "type.googleapis.com/google.protobuf.Struct"
    value:
        key: value
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

  // A module that cannot be loaded at all bumps the module_load_error counter, tagged with the
  // filter name. The counter is on the server scope, which outlives the rejected listener config.
  EXPECT_EQ(1U,
            failureCounter(context.server_factory_context_.scope(), "module_load_error", "foo"));

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

  // The module loads fine in each of these cases (dlopen + program_init succeed); the failure is in
  // resolving the HTTP filter ABI symbols during config initialization, so config_init_error is
  // bumped once per case while module_load_error stays at its earlier value. All cases share the
  // same filter name "foo", so they accumulate on the one config_init_error{filter="foo"} series.
  EXPECT_EQ(test_cases.size(),
            failureCounter(context.server_factory_context_.scope(), "config_init_error", "foo"));
  EXPECT_EQ(1U,
            failureCounter(context.server_factory_context_.scope(), "module_load_error", "foo"));

  // A module that loads and exposes every symbol but whose on_http_filter_config_new returns null
  // also bumps config_init_error.
  {
    NiceMock<Server::Configuration::MockFactoryContext> init_fail_context;
    const std::string yaml = R"EOF(
dynamic_module_config:
    name: http_filter_config_new_fail
filter_name: foo
filter_config:
    "@type": "type.googleapis.com/google.protobuf.StringValue"
    value: "bar"
)EOF";
    envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
    TestUtility::loadFromYamlAndValidate(yaml, proto_config);

    auto result = factory.createFilterFactoryFromProto(proto_config, "", init_fail_context);
    EXPECT_FALSE(result.ok());
    EXPECT_THAT(result.status().message(),
                testing::HasSubstr("Failed to initialize dynamic module"));
    auto& server_scope = init_fail_context.server_factory_context_.scope();
    EXPECT_EQ(1U, failureCounter(server_scope, "config_init_error", "foo"));
    EXPECT_EQ(0U, failureCounter(server_scope, "module_load_error", "foo"));
  }

  auto symbol_err = [](const std::string& symbol) {
    return fmt::format("Failed to resolve symbol {}", symbol);
  };

  // Test case for per-route config when module fails to load entirely.
  {
    const std::string yaml = R"EOF(
dynamic_module_config:
    name: non-existent-module
filter_name: foo
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
    EXPECT_EQ(1U, failureCounter(context.scope(), "per_route_config_error", "foo"));
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
filter_name: foo
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
    // A fresh context is used per iteration, so the per-route failure counter is exactly one.
    EXPECT_EQ(1U, failureCounter(context.scope(), "per_route_config_error", "foo"));
  }
}

TEST(DynamicModuleConfigFactory, MalformedFilterConfig) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);

  Envoy::Server::Configuration::DynamicModuleConfigFactory factory;

  // A filter_config Any that claims to be a StringValue but whose bytes are not valid wire format
  // (a truncated length-delimited field) makes knownAnyToBytes fail after the module itself has
  // loaded successfully. This must be set programmatically because a filter_config parsed from YAML
  // is always valid.
  auto set_malformed = [](auto& proto_config) {
    proto_config.mutable_dynamic_module_config()->set_name("no_op");
    proto_config.set_filter_name("foo");
    auto* any = proto_config.mutable_filter_config();
    any->set_type_url("type.googleapis.com/google.protobuf.StringValue");
    any->set_value("\x0a");
  };

  // createFilterFactory path -> config_init_error.
  {
    NiceMock<Server::Configuration::MockFactoryContext> context;
    envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter proto_config;
    set_malformed(proto_config);

    auto result = factory.createFilterFactoryFromProto(proto_config, "", context);
    EXPECT_FALSE(result.ok());
    auto& server_scope = context.server_factory_context_.scope();
    EXPECT_EQ(1U, failureCounter(server_scope, "config_init_error", "foo"));
    EXPECT_EQ(0U, failureCounter(server_scope, "module_load_error", "foo"));
  }

  // createRouteSpecificFilterConfig path -> per_route_config_error.
  {
    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilterPerRoute proto_config;
    set_malformed(proto_config);

    auto result = factory.createRouteSpecificFilterConfig(
        proto_config, context, ProtobufMessage::getNullValidationVisitor());
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(1U, failureCounter(context.scope(), "per_route_config_error", "foo"));
  }
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
