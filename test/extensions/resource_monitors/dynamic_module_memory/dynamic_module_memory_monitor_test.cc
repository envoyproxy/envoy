#include <optional>

#include "envoy/extensions/resource_monitors/dynamic_module_memory/v3/dynamic_module_memory.pb.h"

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/resource_monitors/dynamic_module_memory/dynamic_module_memory_monitor.h"

#include "test/extensions/dynamic_modules/util.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace DynamicModuleMemory {
namespace {

using HeapSampleVisitor = absl::FunctionRef<void(absl::Span<void* const>, uint64_t)>;

class ResourcePressure : public Server::ResourceUpdateCallbacks {
public:
  void onSuccess(const Server::ResourceUsage& usage) override {
    pressure_ = usage.resource_pressure_;
  }

  void onFailure(const absl::Status& error) override { error_ = error; }

  std::optional<double> pressure_;
  std::optional<absl::Status> error_;
};

envoy::extensions::resource_monitors::dynamic_module_memory::v3::DynamicModuleMemoryConfig
makeConfig() {
  envoy::extensions::resource_monitors::dynamic_module_memory::v3::DynamicModuleMemoryConfig config;
  config.set_module_name("monitored");
  config.set_max_heap_size_bytes(1000);
  return config;
}

TEST(DynamicModuleMemoryMonitorTest, NoPressureWhileModuleIsNotLoaded) {
  DynamicModuleMemoryMonitor monitor(
      makeConfig(),
      DynamicModules::ModuleMemoryReader([](HeapSampleVisitor) { return absl::OkStatus(); }));
  ResourcePressure resource;
  monitor.updateResourceUsage(resource);
  EXPECT_EQ(0, resource.pressure_);
  EXPECT_FALSE(resource.error_.has_value());
}

#if defined(__linux__)
class LoadedModuleTest : public testing::Test {
protected:
  void SetUp() override {
    absl::StatusOr<DynamicModules::DynamicModulePtr> module = DynamicModules::newDynamicModule(
        DynamicModules::testSharedObjectPath("memory_allocator", "c"), false, false, "monitored");
    ASSERT_TRUE(module.ok()) << module.status();
    module_ = std::move(module.value());
    // A return address points past its call instruction, so one byte into a function stands for a
    // frame of that function.
    module_frame_ = reinterpret_cast<char*>(
                        module_
                            ->getFunctionPointer<decltype(&envoy_dynamic_module_on_program_init)>(
                                "envoy_dynamic_module_on_program_init")
                            .value()) +
                    1;
  }

  DynamicModules::DynamicModulePtr module_;
  void* module_frame_;
};

TEST_F(LoadedModuleTest, ReportsHeapMemoryOfModule) {
  DynamicModuleMemoryMonitor monitor(
      makeConfig(), DynamicModules::ModuleMemoryReader([this](HeapSampleVisitor visitor) {
        void* const stack[] = {module_frame_};
        visitor(stack, 250);
        return absl::OkStatus();
      }));
  ResourcePressure resource;
  monitor.updateResourceUsage(resource);
  EXPECT_EQ(0.25, resource.pressure_);
  EXPECT_FALSE(resource.error_.has_value());
}

TEST_F(LoadedModuleTest, ReportsFailureToReadHeapMemory) {
  DynamicModuleMemoryMonitor monitor(makeConfig(),
                                     DynamicModules::ModuleMemoryReader([](HeapSampleVisitor) {
                                       return absl::UnimplementedError("no heap profile");
                                     }));
  ResourcePressure resource;
  monitor.updateResourceUsage(resource);
  EXPECT_FALSE(resource.pressure_.has_value());
  EXPECT_EQ(absl::UnimplementedError("no heap profile"), resource.error_);
}
#endif

} // namespace
} // namespace DynamicModuleMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
