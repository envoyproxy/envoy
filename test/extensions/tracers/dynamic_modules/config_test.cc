#include "envoy/extensions/tracers/dynamic_modules/v3/dynamic_modules.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/stats/custom_stat_namespaces_impl.h"
#include "source/extensions/tracers/dynamic_modules/config.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace DynamicModules {
namespace {

class DynamicModuleTracerFactoryTest : public ::testing::Test {
public:
  DynamicModuleTracerFactoryTest() {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(
                                   "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
                               1);
  }

  NiceMock<Server::Configuration::MockTracerFactoryContext> context_;
};

TEST_F(DynamicModuleTracerFactoryTest, FactoryName) {
  DynamicModuleTracerFactory factory;
  EXPECT_EQ(factory.name(), "envoy.tracers.dynamic_modules");
}

TEST_F(DynamicModuleTracerFactoryTest, CreateEmptyConfigProto) {
  DynamicModuleTracerFactory factory;
  auto proto = factory.createEmptyConfigProto();
  EXPECT_NE(proto, nullptr);
}

TEST_F(DynamicModuleTracerFactoryTest, CreateTracerDriverSuccess) {
  DynamicModuleTracerFactory factory;

  ::envoy::extensions::tracers::dynamic_modules::v3::DynamicModuleTracer proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("tracer_no_op");
  proto_config.set_tracer_name("test_tracer");

  auto driver = factory.createTracerDriver(proto_config, context_);
  EXPECT_NE(driver, nullptr);
}

TEST_F(DynamicModuleTracerFactoryTest, CreateTracerDriverModuleNotFound) {
  DynamicModuleTracerFactory factory;

  ::envoy::extensions::tracers::dynamic_modules::v3::DynamicModuleTracer proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("nonexistent_module");
  proto_config.set_tracer_name("test_tracer");

  EXPECT_THROW_WITH_REGEX(factory.createTracerDriver(proto_config, context_), EnvoyException,
                          "Failed to load dynamic module");
}

TEST_F(DynamicModuleTracerFactoryTest, CreateTracerDriverMissingSymbols) {
  DynamicModuleTracerFactory factory;

  ::envoy::extensions::tracers::dynamic_modules::v3::DynamicModuleTracer proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("no_op");
  proto_config.set_tracer_name("test_tracer");

  EXPECT_THROW_WITH_REGEX(factory.createTracerDriver(proto_config, context_), EnvoyException,
                          "Failed to create tracer config");
}

TEST_F(DynamicModuleTracerFactoryTest, CreateTracerDriverConfigInitFails) {
  DynamicModuleTracerFactory factory;

  ::envoy::extensions::tracers::dynamic_modules::v3::DynamicModuleTracer proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("tracer_config_fail");
  proto_config.set_tracer_name("test_tracer");

  EXPECT_THROW_WITH_REGEX(factory.createTracerDriver(proto_config, context_), EnvoyException,
                          "Failed to create tracer config");
}

TEST_F(DynamicModuleTracerFactoryTest, CreateTracerDriverWithTracerConfig) {
  DynamicModuleTracerFactory factory;

  ::envoy::extensions::tracers::dynamic_modules::v3::DynamicModuleTracer proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("tracer_no_op");
  proto_config.set_tracer_name("test_tracer");

  Protobuf::StringValue string_value;
  string_value.set_value("test_config_payload");
  proto_config.mutable_tracer_config()->PackFrom(string_value);

  auto driver = factory.createTracerDriver(proto_config, context_);
  EXPECT_NE(driver, nullptr);
}

TEST_F(DynamicModuleTracerFactoryTest, CreateTracerDriverWithInvalidTracerConfig) {
  DynamicModuleTracerFactory factory;

  ::envoy::extensions::tracers::dynamic_modules::v3::DynamicModuleTracer proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("tracer_no_op");
  proto_config.set_tracer_name("test_tracer");

  auto* any = proto_config.mutable_tracer_config();
  any->set_type_url("type.googleapis.com/google.protobuf.StringValue");
  any->set_value("invalid_binary_data_that_cannot_be_unpacked_as_string_value");

  EXPECT_THROW_WITH_REGEX(factory.createTracerDriver(proto_config, context_), EnvoyException,
                          "Failed to parse tracer config");
}

TEST_F(DynamicModuleTracerFactoryTest, CreateTracerDriverWithCustomMetricsNamespace) {
  DynamicModuleTracerFactory factory;

  ::envoy::extensions::tracers::dynamic_modules::v3::DynamicModuleTracer proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("tracer_no_op");
  proto_config.mutable_dynamic_module_config()->set_metrics_namespace("custom_ns");
  proto_config.set_tracer_name("test_tracer");

  auto driver = factory.createTracerDriver(proto_config, context_);
  EXPECT_NE(driver, nullptr);
}

TEST_F(DynamicModuleTracerFactoryTest, RegisterStatNamespaceWithRuntimeGuard) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.dynamic_modules_strip_custom_stat_prefix", "true"}});

  Stats::CustomStatNamespacesImpl custom_stat_namespaces;
  ON_CALL(context_.server_factory_context_.api_, customStatNamespaces())
      .WillByDefault(testing::ReturnRef(custom_stat_namespaces));

  DynamicModuleTracerFactory factory;

  ::envoy::extensions::tracers::dynamic_modules::v3::DynamicModuleTracer proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("tracer_no_op");
  proto_config.mutable_dynamic_module_config()->set_metrics_namespace("my_custom_namespace");
  proto_config.set_tracer_name("test_tracer");

  auto driver = factory.createTracerDriver(proto_config, context_);
  EXPECT_NE(driver, nullptr);

  EXPECT_TRUE(custom_stat_namespaces.registered("my_custom_namespace"));
}

} // namespace
} // namespace DynamicModules
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
