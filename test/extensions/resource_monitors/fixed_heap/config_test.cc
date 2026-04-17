#include "envoy/extensions/resource_monitors/fixed_heap/v3/fixed_heap.pb.h"
#include "envoy/extensions/resource_monitors/fixed_heap/v3/fixed_heap.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/resource_monitors/fixed_heap/config.h"
#include "source/server/resource_monitor_config_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/options.h"

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
  testing::NiceMock<Runtime::MockLoader> runtime;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor(), runtime);
  auto monitor = factory->createResourceMonitor(config, context);
  EXPECT_NE(monitor, nullptr);
}

TEST(FixedHeapMonitorFactoryTest, CreateMonitorWithRuntimeOverride) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.fixed_heap");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig config;
  config.mutable_max_heap_size_bytes_runtime()->set_default_value(2000);
  config.mutable_max_heap_size_bytes_runtime()->set_runtime_key("fixed_heap.max_bytes");
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  testing::NiceMock<Runtime::MockLoader> runtime;
  EXPECT_CALL(runtime, snapshot()).WillRepeatedly(testing::ReturnRef(runtime.snapshot_));
  EXPECT_CALL(runtime.snapshot_, getInteger("fixed_heap.max_bytes", 2000))
      .WillRepeatedly(testing::Return(2000));
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor(), runtime);
  auto monitor = factory->createResourceMonitor(config, context);
  EXPECT_NE(monitor, nullptr);
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
  testing::NiceMock<Runtime::MockLoader> runtime;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor(), runtime);
  EXPECT_THROW_WITH_REGEX(factory->createResourceMonitor(config, context), EnvoyException,
                          "max heap size must be greater than 0");
}

TEST(FixedHeapMonitorFactoryTest, RejectBothSet) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.fixed_heap");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig config;
  config.set_max_heap_size_bytes(1000);
  config.mutable_max_heap_size_bytes_runtime()->set_default_value(2000);
  config.mutable_max_heap_size_bytes_runtime()->set_runtime_key("fixed_heap.max_bytes");
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  testing::NiceMock<Runtime::MockLoader> runtime;
  EXPECT_CALL(runtime, snapshot()).WillRepeatedly(testing::ReturnRef(runtime.snapshot_));
  EXPECT_CALL(runtime.snapshot_, getInteger("fixed_heap.max_bytes", 2000))
      .WillRepeatedly(testing::Return(2000));
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor(), runtime);
  EXPECT_THROW_WITH_REGEX(factory->createResourceMonitor(config, context), EnvoyException,
                          "exactly one of max_heap_size_bytes or max_heap_size_bytes_runtime must "
                          "be set");
}

TEST(FixedHeapMonitorFactoryTest, RejectRuntimeDefaultZero) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.fixed_heap");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::fixed_heap::v3::FixedHeapConfig config;
  config.mutable_max_heap_size_bytes_runtime()->set_default_value(0);
  config.mutable_max_heap_size_bytes_runtime()->set_runtime_key("fixed_heap.max_bytes");
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  testing::NiceMock<Runtime::MockLoader> runtime;
  EXPECT_CALL(runtime, snapshot()).WillRepeatedly(testing::ReturnRef(runtime.snapshot_));
  EXPECT_CALL(runtime.snapshot_, getInteger("fixed_heap.max_bytes", 0))
      .WillRepeatedly(testing::Return(0));
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor(), runtime);
  EXPECT_THROW_WITH_REGEX(factory->createResourceMonitor(config, context), EnvoyException,
                          "max heap size must be greater than 0");
}

} // namespace
} // namespace FixedHeapMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
