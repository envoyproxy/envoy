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
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/options.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CgroupMemory {

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

TEST(CgroupMemoryConfigTest, BasicTest) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->name(), "envoy.resource_monitors.cgroup_memory");
}

TEST(CgroupMemoryConfigTest, CreateMonitorViaFactory) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);

  NiceMock<Filesystem::MockInstance> mock_fs;
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillRepeatedly(Return(true));

  NiceMock<Api::MockApi> mock_api;
  EXPECT_CALL(mock_api, fileSystem()).WillRepeatedly(ReturnRef(mock_fs));

  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  Event::MockDispatcher dispatcher;
  Server::MockOptions options;
  testing::NiceMock<Runtime::MockLoader> runtime;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, mock_api, ProtobufMessage::getStrictValidationVisitor(), runtime);
  auto monitor_or_error = factory->createResourceMonitor(config, context);
  EXPECT_TRUE(monitor_or_error.ok());
  EXPECT_NE(monitor_or_error.value(), nullptr);
}

TEST(CgroupMemoryConfigTest, CreateMonitorIgnoresContext) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  config.set_max_memory_bytes(1234);

  NiceMock<Filesystem::MockInstance> mock_fs;
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getUsagePath())).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_fs, fileExists(CgroupPaths::V2::getLimitPath())).WillRepeatedly(Return(true));

  NiceMock<Api::MockApi> mock_api;
  EXPECT_CALL(mock_api, fileSystem()).WillRepeatedly(ReturnRef(mock_fs));

  Event::MockDispatcher dispatcher1;
  Server::MockOptions options1;
  testing::NiceMock<Runtime::MockLoader> runtime1;
  Server::Configuration::ResourceMonitorFactoryContextImpl context1(
      dispatcher1, options1, mock_api, ProtobufMessage::getStrictValidationVisitor(), runtime1);

  Event::MockDispatcher dispatcher2;
  Server::MockOptions options2;
  testing::NiceMock<Runtime::MockLoader> runtime2;
  Server::Configuration::ResourceMonitorFactoryContextImpl context2(
      dispatcher2, options2, mock_api, ProtobufMessage::getStrictValidationVisitor(), runtime2);

  auto monitor1_or_error = factory->createResourceMonitor(config, context1);
  auto monitor2_or_error = factory->createResourceMonitor(config, context2);

  EXPECT_TRUE(monitor1_or_error.ok());
  EXPECT_TRUE(monitor2_or_error.ok());
  EXPECT_NE(monitor1_or_error.value(), nullptr);
  EXPECT_NE(monitor2_or_error.value(), nullptr);
}

TEST(CgroupMemoryConfigTest, FactoryReturnsErrorWhenNoCgroupAvailable) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::ResourceMonitorFactory>::getFactory(
          "envoy.resource_monitors.cgroup_memory");
  ASSERT_NE(factory, nullptr);

  NiceMock<Filesystem::MockInstance> mock_fs;
  ON_CALL(mock_fs, fileExists).WillByDefault(Return(false));

  NiceMock<Api::MockApi> mock_api;
  EXPECT_CALL(mock_api, fileSystem()).WillRepeatedly(ReturnRef(mock_fs));

  envoy::extensions::resource_monitors::cgroup_memory::v3::CgroupMemoryConfig config;
  Event::MockDispatcher dispatcher;
  Server::MockOptions options;
  testing::NiceMock<Runtime::MockLoader> runtime;
  Server::Configuration::ResourceMonitorFactoryContextImpl context(
      dispatcher, options, mock_api, ProtobufMessage::getStrictValidationVisitor(), runtime);
  auto status = factory->createResourceMonitor(config, context).status();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(std::string(status.message()),
              ::testing::HasSubstr("No supported cgroup memory implementation found"));
}

} // namespace CgroupMemory
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
