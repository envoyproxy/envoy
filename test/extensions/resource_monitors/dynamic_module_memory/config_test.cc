#include "envoy/extensions/resource_monitors/dynamic_module_memory/v3/dynamic_module_memory.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/resource_monitors/dynamic_module_memory/config.h"
#include "source/server/resource_monitor_config_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/options.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DynamicModuleMemory {
namespace {

class DynamicModuleMemoryMonitorFactoryTest : public testing::Test {
protected:
  void SetUp() override {
    factory_ = Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
        "envoy.resource_monitors.dynamic_module_memory");
    ASSERT_NE(nullptr, factory_);
  }

  absl::StatusOr<Server::ResourceMonitorPtr> createMonitor(const std::string& yaml) {
    envoy::extensions::resource_monitors::dynamic_module_memory::v3::DynamicModuleMemoryConfig
        config;
    TestUtility::loadFromYaml(yaml, config);
    Server::Configuration::ResourceMonitorFactoryContextImpl context(
        dispatcher_, options_, *api_, ProtobufMessage::getStrictValidationVisitor(), runtime_);
    return factory_->createResourceMonitor(config, context);
  }

  Server::Configuration::ResourceMonitorFactory* factory_ = nullptr;
  Event::MockDispatcher dispatcher_;
  Api::ApiPtr api_ = Api::createApiForTest();
  Server::MockOptions options_;
  testing::NiceMock<Runtime::MockLoader> runtime_;
};

TEST_F(DynamicModuleMemoryMonitorFactoryTest, CreateMonitor) {
  absl::StatusOr<Server::ResourceMonitorPtr> monitor = createMonitor(R"EOF(
module_name: my_module
max_heap_size_bytes: 1048576
)EOF");
  ASSERT_TRUE(monitor.ok());
  EXPECT_NE(nullptr, monitor.value());
}

TEST_F(DynamicModuleMemoryMonitorFactoryTest, RejectsEmptyModuleName) {
  EXPECT_THROW_WITH_REGEX(createMonitor("max_heap_size_bytes: 1048576").IgnoreError(),
                          ProtoValidationException, "ModuleName: value length must be at least 1");
}

TEST_F(DynamicModuleMemoryMonitorFactoryTest, RejectsZeroMaxHeapSize) {
  EXPECT_THROW_WITH_REGEX(createMonitor("module_name: my_module").IgnoreError(),
                          ProtoValidationException,
                          "MaxHeapSizeBytes: value must be greater than 0");
}

} // namespace
} // namespace DynamicModuleMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
