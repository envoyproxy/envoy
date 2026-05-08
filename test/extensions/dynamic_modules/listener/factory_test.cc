#include "envoy/extensions/filters/listener/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/extensions/filters/listener/dynamic_modules/factory.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/listener_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class DynamicModuleListenerFilterFactoryTest : public testing::Test {
public:
  void SetUp() override {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(
                                   "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
                               1);
  }

  NiceMock<MockListenerFactoryContext> context_;
  DynamicModuleListenerFilterConfigFactory factory_;
};

TEST_F(DynamicModuleListenerFilterFactoryTest, ValidConfig) {
  envoy::extensions::filters::listener::dynamic_modules::v3::DynamicModuleListenerFilter config;
  config.mutable_dynamic_module_config()->set_name("listener_no_op");
  config.set_filter_name("test_filter");

  auto result = factory_.createListenerFilterFactoryFromProto(config, nullptr, context_);
  // Result is a factory callback, which should not be null.
  EXPECT_NE(nullptr, result);
}

TEST_F(DynamicModuleListenerFilterFactoryTest, ValidConfigWithFilterConfig) {
  envoy::extensions::filters::listener::dynamic_modules::v3::DynamicModuleListenerFilter config;
  config.mutable_dynamic_module_config()->set_name("listener_no_op");
  config.set_filter_name("test_filter");
  config.mutable_filter_config()->PackFrom(ValueUtil::stringValue("test_config_value"));

  auto result = factory_.createListenerFilterFactoryFromProto(config, nullptr, context_);
  EXPECT_NE(nullptr, result);
}

TEST_F(DynamicModuleListenerFilterFactoryTest, InvalidModuleName) {
  envoy::extensions::filters::listener::dynamic_modules::v3::DynamicModuleListenerFilter config;
  config.mutable_dynamic_module_config()->set_name("nonexistent_module");
  config.set_filter_name("test_filter");

  EXPECT_THROW_WITH_REGEX(factory_.createListenerFilterFactoryFromProto(config, nullptr, context_),
                          EnvoyException, "Failed to load dynamic module");
}

TEST_F(DynamicModuleListenerFilterFactoryTest, MissingListenerFilterSymbols) {
  // Use the HTTP filter no_op module which lacks listener filter symbols.
  envoy::extensions::filters::listener::dynamic_modules::v3::DynamicModuleListenerFilter config;
  config.mutable_dynamic_module_config()->set_name("no_op");
  config.set_filter_name("test_filter");

  EXPECT_THROW_WITH_REGEX(factory_.createListenerFilterFactoryFromProto(config, nullptr, context_),
                          EnvoyException, "Failed to create filter config");
}

TEST_F(DynamicModuleListenerFilterFactoryTest, ConfigInitializationFailure) {
  // Use a module that returns nullptr from config_new.
  envoy::extensions::filters::listener::dynamic_modules::v3::DynamicModuleListenerFilter config;
  config.mutable_dynamic_module_config()->set_name("listener_config_new_fail");
  config.set_filter_name("test_filter");

  EXPECT_THROW_WITH_REGEX(factory_.createListenerFilterFactoryFromProto(config, nullptr, context_),
                          EnvoyException, "Failed to create filter config");
}

TEST_F(DynamicModuleListenerFilterFactoryTest, FactoryName) {
  EXPECT_EQ("envoy.filters.listener.dynamic_modules", factory_.name());
}

TEST_F(DynamicModuleListenerFilterFactoryTest, FilterFactoryCallbackAddsFilter) {
  envoy::extensions::filters::listener::dynamic_modules::v3::DynamicModuleListenerFilter config;
  config.mutable_dynamic_module_config()->set_name("listener_no_op");
  config.set_filter_name("test_filter");

  auto result = factory_.createListenerFilterFactoryFromProto(config, nullptr, context_);
  ASSERT_NE(nullptr, result);

  // Test that the filter factory callback correctly adds a filter.
  NiceMock<Network::MockListenerFilterManager> filter_manager;
  EXPECT_CALL(filter_manager, addAcceptFilter_(testing::_, testing::_));
  result(filter_manager);
}

TEST_F(DynamicModuleListenerFilterFactoryTest, DoNotCloseOption) {
  envoy::extensions::filters::listener::dynamic_modules::v3::DynamicModuleListenerFilter config;
  config.mutable_dynamic_module_config()->set_name("listener_no_op");
  config.mutable_dynamic_module_config()->set_do_not_close(true);
  config.set_filter_name("test_filter");

  auto result = factory_.createListenerFilterFactoryFromProto(config, nullptr, context_);
  EXPECT_NE(nullptr, result);
}

TEST_F(DynamicModuleListenerFilterFactoryTest, LoadGloballyOption) {
  envoy::extensions::filters::listener::dynamic_modules::v3::DynamicModuleListenerFilter config;
  config.mutable_dynamic_module_config()->set_name("listener_no_op");
  config.mutable_dynamic_module_config()->set_load_globally(true);
  config.set_filter_name("test_filter");

  auto result = factory_.createListenerFilterFactoryFromProto(config, nullptr, context_);
  EXPECT_NE(nullptr, result);
}

TEST_F(DynamicModuleListenerFilterFactoryTest, InvalidFilterConfigUnpackFailure) {
  envoy::extensions::filters::listener::dynamic_modules::v3::DynamicModuleListenerFilter config;
  config.mutable_dynamic_module_config()->set_name("listener_no_op");
  config.set_filter_name("test_filter");

  // Create an Any that claims to be a StringValue but contains invalid binary data.
  // This will cause UnpackTo<StringValue> to fail in anyToBytes.
  auto* any = config.mutable_filter_config();
  any->set_type_url("type.googleapis.com/google.protobuf.StringValue");
  any->set_value("invalid_binary_data_that_cannot_be_unpacked_as_string_value");

  EXPECT_THROW_WITH_REGEX(factory_.createListenerFilterFactoryFromProto(config, nullptr, context_),
                          EnvoyException, "Failed to parse filter config");
}

// Test that the legacy behavior registers the custom stat namespace when the runtime guard is
// enabled.
TEST_F(DynamicModuleListenerFilterFactoryTest, LegacyBehaviorWithRuntimeGuard) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix", "true"}});

  // Set up mock to expect the registerStatNamespace call.
  Stats::CustomStatNamespacesImpl custom_stat_namespaces;
  ON_CALL(context_.server_factory_context_.api_, customStatNamespaces())
      .WillByDefault(testing::ReturnRef(custom_stat_namespaces));

  envoy::extensions::filters::listener::dynamic_modules::v3::DynamicModuleListenerFilter config;
  config.mutable_dynamic_module_config()->set_name("listener_no_op");
  config.mutable_dynamic_module_config()->set_metrics_namespace("custom_namespace");
  config.set_filter_name("test_filter");

  auto result = factory_.createListenerFilterFactoryFromProto(config, nullptr, context_);
  EXPECT_NE(nullptr, result);

  // Verify the custom namespace was registered.
  EXPECT_TRUE(custom_stat_namespaces.registered("custom_namespace"));
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
