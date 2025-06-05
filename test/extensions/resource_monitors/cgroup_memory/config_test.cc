#include "envoy/registry/registry.h"
#include "envoy/server/resource_monitor_config.h"

#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_monitor.h"
#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_paths.h"
#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_stats_reader.h"
#include "source/extensions/resource_monitors/cgroup_memory/config.h"
#include "source/server/resource_monitor_config_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/server/options.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

using testing::NiceMock;
using testing::Return;
using testing::StartsWith;

// Basic test to verify factory registration
TEST(CgroupMemoryConfigTest, BasicTest) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->name(), "envoy.resource_monitors.cgroup_memory");
}

// Test creating a monitor with default values
TEST(CgroupMemoryConfigTest, CreateMonitorDefault) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);

  NiceMock<Filesystem::MockInstance> mock_fs;

  // Mock the filesystem to indicate that cgroup v2 paths exist
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillRepeatedly(Return(true));

  // Mock the file reads to return memory usage and limit
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
      .WillRepeatedly(Return(absl::StatusOr<std::string>("100")));
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getLimitPath()))
      .WillRepeatedly(Return(absl::StatusOr<std::string>("1000")));

  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, mock_fs);
  EXPECT_NE(monitor, nullptr);
}

// Test creating a monitor with a specific configured memory limit
TEST(CgroupMemoryConfigTest, CreateMonitorWithLimit) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);

  NiceMock<Filesystem::MockInstance> mock_fs;

  // Mock the filesystem to indicate that cgroup v2 paths exist
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillRepeatedly(Return(true));

  // Mock the file reads to return memory usage and limit
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
      .WillRepeatedly(Return(absl::StatusOr<std::string>("500")));
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getLimitPath()))
      .WillRepeatedly(Return(absl::StatusOr<std::string>("2000")));

  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.set_max_memory_bytes(1234);

  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, mock_fs);
  EXPECT_NE(monitor, nullptr);
}

// Test creating a monitor with a zero memory limit (now allowed)
TEST(CgroupMemoryConfigTest, ZeroMemoryLimit) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);

  NiceMock<Filesystem::MockInstance> mock_fs;

  // Mock the filesystem to indicate that cgroup v2 paths exist
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillRepeatedly(Return(true));

  // Mock the file reads to return memory usage and limit
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
      .WillRepeatedly(Return(absl::StatusOr<std::string>("500")));
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getLimitPath()))
      .WillRepeatedly(Return(absl::StatusOr<std::string>("2000")));

  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.set_max_memory_bytes(0);

  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, mock_fs);
  EXPECT_NE(monitor, nullptr);
}

// Test that factory creates a monitor successfully with ignored context
TEST(CgroupMemoryConfigTest, CreateMonitorIgnoresContext) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.set_max_memory_bytes(1234);

  // Create mock filesystem
  NiceMock<Filesystem::MockInstance> mock_fs;

  // Mock the filesystem to indicate that cgroup v2 paths exist
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillRepeatedly(Return(true));

  // Mock the file reads to return memory usage and limit
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getUsagePath()))
      .WillRepeatedly(Return(absl::StatusOr<std::string>("500")));
  EXPECT_CALL(mock_fs, fileReadToEnd(CgroupPaths::V2::getLimitPath()))
      .WillRepeatedly(Return(absl::StatusOr<std::string>("2000")));

  // Create mock API with the mock filesystem
  NiceMock<Api::MockApi> mock_api;
  EXPECT_CALL(mock_api, fileSystem()).WillRepeatedly(ReturnRef(mock_fs));

  // Create contexts with the mock API
  Event::MockDispatcher dispatcher1;
  Server::MockOptions options1;
  Server::Configuration::ResourceMonitorFactoryContextImpl context1(
      dispatcher1, options1, mock_api, ProtobufMessage::getStrictValidationVisitor());

  Event::MockDispatcher dispatcher2;
  Server::MockOptions options2;
  Server::Configuration::ResourceMonitorFactoryContextImpl context2(
      dispatcher2, options2, mock_api, ProtobufMessage::getStrictValidationVisitor());

  auto monitor1 = factory->createResourceMonitor(config, context1);
  auto monitor2 = factory->createResourceMonitor(config, context2);

  EXPECT_NE(monitor1, nullptr);
  EXPECT_NE(monitor2, nullptr);
}

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
