#include "envoy/extensions/resource_monitors/fixed_heap/v3/fixed_heap.pb.h"
#include "envoy/extensions/resource_monitors/fixed_heap/v3/fixed_heap.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/resource_monitors/fixed_heap/config.h"
#include "source/server/resource_monitor_config_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/options.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace FixedHeapMonitor {
namespace {

TEST(FixedHeapMonitorFactoryTest, CreateMonitor) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.fixed_heap");
  EXPECT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig config;
  config.set_max_heap_size_bytes(std::numeric_limits<uint64_t>::max());
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  auto monitor = factory->createResourceMonitor(config, context);
  EXPECT_NE(monitor, nullptr);
}

TEST(FixedHeapMonitorFactoryTest, CreateMonitorWithStartupOverride) {
  const std::string env_var = "ENVOY_FIXED_HEAP_TEST_MAX_BYTES";
  TestEnvironment::setEnvVar(env_var, "2000", 1);

  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.fixed_heap");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig config;
  config.mutable_max_heap_size_bytes_source()->set_environment_variable(env_var);
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  auto monitor = factory->createResourceMonitor(config, context);
  EXPECT_NE(monitor, nullptr);

  TestEnvironment::unsetEnvVar(env_var);
}

TEST(FixedHeapMonitorFactoryTest, RejectNeitherSet) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.fixed_heap");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig config;
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  EXPECT_THROW_WITH_REGEX(factory->createResourceMonitor(config, context), EnvoyException,
                          "exactly one of max_heap_size_bytes or max_heap_size_bytes_source must "
                          "be set");
}

TEST(FixedHeapMonitorFactoryTest, RejectBothSet) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.fixed_heap");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig config;
  config.set_max_heap_size_bytes(1000);
  config.mutable_max_heap_size_bytes_source()->set_inline_string("2000");
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  EXPECT_THROW_WITH_REGEX(factory->createResourceMonitor(config, context), EnvoyException,
                          "exactly one of max_heap_size_bytes or max_heap_size_bytes_source must "
                          "be set");
}

TEST(FixedHeapMonitorFactoryTest, RejectDataSourceInvalidInteger) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.fixed_heap");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig config;
  config.mutable_max_heap_size_bytes_source()->set_inline_string("not_a_number");
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  EXPECT_THROW_WITH_REGEX(factory->createResourceMonitor(config, context), EnvoyException,
                          "fixed_heap max_heap_size_bytes_source must be a positive integer");
}

TEST(FixedHeapMonitorFactoryTest, RejectDataSourceZero) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.fixed_heap");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig config;
  config.mutable_max_heap_size_bytes_source()->set_inline_string("0");
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  EXPECT_THROW_WITH_REGEX(factory->createResourceMonitor(config, context), EnvoyException,
                          "max heap size must be greater than 0");
}

TEST(FixedHeapMonitorFactoryTest, RejectDataSourceReadFailure) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.fixed_heap");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig config;
  config.mutable_max_heap_size_bytes_source()->set_environment_variable(
      "ENVOY_FIXED_HEAP_NONEXISTENT_VAR_12345");
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  EXPECT_THROW(factory->createResourceMonitor(config, context), EnvoyException);
}

} // namespace
} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
