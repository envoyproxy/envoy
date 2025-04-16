#include "envoy/registry/registry.h"
#include "envoy/server/resource_monitor_config.h"

#include "source/extensions/resource_monitors/cgroup_memory/config.h"
#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_monitor.h"
#include "source/extensions/resource_monitors/cgroup_memory/cgroup_memory_stats_reader.h"
#include "source/server/resource_monitor_config_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/options.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

using testing::Return;

class MockCgroupMemoryStatsReader : public CgroupMemoryStatsReader {
public:
  MOCK_METHOD(uint64_t, getMemoryUsage, (), (override));
  MOCK_METHOD(uint64_t, getMemoryLimit, (), (override));
  MOCK_METHOD(std::string, getMemoryUsagePath, (), (const, override));
  MOCK_METHOD(std::string, getMemoryLimitPath, (), (const, override));
};

TEST(CgroupMemoryMonitorFactoryTest, BasicTest) {
  auto* factory = Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
      "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->name(), "envoy.resource_monitors.cgroup_memory");
}

TEST(CgroupMemoryMonitorFactoryTest, CreateMonitorDefault) {
  auto* factory = Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
      "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);

  // Set up mock stats reader factory
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

TEST(CgroupMemoryMonitorFactoryTest, CreateMonitorWithLimit) {
  auto* factory = Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
      "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);

  // Set up mock stats reader
  auto mock_reader = std::make_unique<MockCgroupMemoryStatsReader>();
  EXPECT_CALL(*mock_reader, getMemoryUsage()).WillRepeatedly(Return(500));
  EXPECT_CALL(*mock_reader, getMemoryLimit()).WillRepeatedly(Return(2000));

  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.set_max_memory_bytes(1234);  // Set explicit memory limit

  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());
  auto monitor = std::make_unique<CgroupMemoryMonitor>(config, std::move(mock_reader));
  EXPECT_NE(monitor, nullptr);
}

TEST(CgroupMemoryMonitorFactoryTest, InvalidConfig) {
  auto* factory = Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
      "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.set_max_memory_bytes(0);  // Invalid value

  Event::MockDispatcher dispatcher;
  Api::ApiPtr api = Api::createApiForTest();
  Server::MockOptions options;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, *api, ProtobufMessage::getStrictValidationVisitor());

  EXPECT_THROW_WITH_MESSAGE(
      factory->createResourceMonitor(config, context),
      ProtoValidationException,
      "max_memory_bytes: 0\n: Proto constraint validation failed "
      "(CgroupMemoryConfigValidationError.MaxMemoryBytes: value must be greater than 0)");
}

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
