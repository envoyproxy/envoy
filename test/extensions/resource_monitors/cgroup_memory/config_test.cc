#include "envoy/registry/registry.h"
#include "envoy/server/resource_monitor_config.h"

#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_monitor.h"
#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_stats_reader.h"
#include "source/extensions/resource_monitors/cgroup_memory/config.h"
#include "source/server/resource_monitor_config_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/options.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

using testing::NiceMock;
using testing::Return;

class MockCgroupMemoryStatsReader : public CgroupMemoryStatsReader {
public:
  MOCK_METHOD(uint64_t, getMemoryUsage, (), (override));
  MOCK_METHOD(uint64_t, getMemoryLimit, (), (override));
  MOCK_METHOD(std::string, getMemoryUsagePath, (), (const, override));
  MOCK_METHOD(std::string, getMemoryLimitPath, (), (const, override));
};

class MockFileSystem : public FileSystem {
public:
  MOCK_METHOD(bool, exists, (const std::string&), (const, override));
};

// Basic test to verify factory registration
TEST(CgroupMemoryMonitorFactoryTest, BasicTest) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->name(), "envoy.resource_monitors.cgroup_memory");
}

// Test creating a monitor with default values
TEST(CgroupMemoryMonitorFactoryTest, CreateMonitorDefault) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);

  auto mock_reader = std::make_unique<MockCgroupMemoryStatsReader>();
  EXPECT_CALL(*mock_reader, getMemoryUsage()).WillRepeatedly(Return(100));
  EXPECT_CALL(*mock_reader, getMemoryLimit()).WillRepeatedly(Return(1000));

  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, std::move(mock_reader));
  EXPECT_NE(monitor, nullptr);
}

// Test creating a monitor with a specific configured memory limit
TEST(CgroupMemoryMonitorFactoryTest, CreateMonitorWithLimit) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);

  auto mock_reader = std::make_unique<MockCgroupMemoryStatsReader>();
  EXPECT_CALL(*mock_reader, getMemoryUsage()).WillRepeatedly(Return(500));
  EXPECT_CALL(*mock_reader, getMemoryLimit()).WillRepeatedly(Return(2000));

  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.set_max_memory_bytes(1234); // Set explicit memory limit

  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, std::move(mock_reader));
  EXPECT_NE(monitor, nullptr);
}

// Test creating a monitor with an invalid memory limit
TEST(CgroupMemoryMonitorFactoryTest, InvalidConfig) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.set_max_memory_bytes(0);

  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());

  EXPECT_THROW_WITH_MESSAGE(
      factory->createResourceMonitor(config, context), ProtoValidationException,
      "max_memory_bytes: 0\n: Proto constraint validation failed "
      "(CgroupMemoryConfigValidationError.MaxMemoryBytes: value must be greater than 0)");
}

// Test that factory creates a monitor successfully with ignored context
TEST(CgroupMemoryMonitorFactoryTest, CreateMonitorIgnoresContext) {
  auto mock_file_system = std::make_unique<NiceMock<MockFileSystem>>();
  const auto* mock_ptr = mock_file_system.get();
  const FileSystem* original = &FileSystem::instance();
  FileSystem::setInstance(mock_ptr);

  // Mock filesystem to indicate cgroup v2 is available
  ON_CALL(*mock_file_system, exists(_)).WillByDefault(Return(false));
  EXPECT_CALL(*mock_file_system, exists(CgroupPaths::V2::getUsagePath()))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_file_system, exists(CgroupPaths::V2::getLimitPath()))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_file_system, exists("/sys/fs/cgroup/memory")).Times(0);

  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.set_max_memory_bytes(1234);

  Event::MockDispatcher dispatcher1;
  Api::ApiPtr api1 = Api::createApiForTest();
  Server::MockOptions options1;
  Server::Configuration::ResourceMonitorFactoryContextImpl context1(
      dispatcher1, options1, *api1, ProtobufMessage::getStrictValidationVisitor());

  Event::MockDispatcher dispatcher2;
  Api::ApiPtr api2 = Api::createApiForTest();
  Server::MockOptions options2;
  Server::Configuration::ResourceMonitorFactoryContextImpl context2(
      dispatcher2, options2, *api2, ProtobufMessage::getStrictValidationVisitor());

  auto monitor1 = factory->createResourceMonitor(config, context1);
  auto monitor2 = factory->createResourceMonitor(config, context2);

  EXPECT_NE(monitor1, nullptr);
  EXPECT_NE(monitor2, nullptr);

  FileSystem::setInstance(original);
}

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
