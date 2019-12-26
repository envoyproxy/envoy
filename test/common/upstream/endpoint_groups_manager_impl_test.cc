#include "envoy/config/endpoint/v3/endpoint.pb.h"

#include "common/upstream/endpoint_groups_manager_impl.h"

#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

TEST(EndpointGroupsManagerImplTest, Basic) {
  EndpointGroupsManagerImpl manager;

  auto monitor = std::make_shared<MockEndpointGroupMonitor>();
  const std::string group_name("test1");
  EXPECT_FALSE(manager.findMonitor(monitor, group_name));
  manager.addMonitor(monitor, group_name);
  EXPECT_TRUE(manager.findMonitor(monitor, group_name));

  envoy::config::endpoint::v3::EndpointGroup group_data;
  group_data.set_name(group_name);
  const std::string version("1.0");
  EXPECT_CALL(*monitor, update(_, _))
      .WillOnce(Invoke([&](const envoy::config::endpoint::v3::EndpointGroup& group,
                           absl::string_view version_info) -> void {
        EXPECT_EQ(group_data.name(), group.name());
        EXPECT_STREQ(version.c_str(), version_info.data());
      }))
      .WillOnce(Invoke([&](const envoy::config::endpoint::v3::EndpointGroup& group,
                           absl::string_view version_info) -> void {
        EXPECT_EQ(group_data.name(), group.name());
        EXPECT_TRUE(group.endpoints().empty());
        EXPECT_STREQ(version.c_str(), version_info.data());
      }));
  manager.addOrUpdateEndpointGroup(group_data, version);

  // Clear the endpoint group resource.
  manager.clearEndpointGroup(group_name, version);
  // The monitor does not deleted.
  EXPECT_TRUE(manager.findMonitor(monitor, group_name));

  // Remove the endpoint group resource.
  manager.removeEndpointGroup(group_name);
  // The monitor does not exist because the resource has been deleted.
  EXPECT_FALSE(manager.findMonitor(monitor, group_name));
}

TEST(EndpointGroupsManagerImplTest, MultiEndpointGroupResource) {
  EndpointGroupsManagerImpl manager;

  const std::string test1_group("test1");
  auto test1_monitor = std::make_shared<MockEndpointGroupMonitor>();
  EXPECT_FALSE(manager.findMonitor(test1_monitor, test1_group));
  manager.addMonitor(test1_monitor, test1_group);
  EXPECT_TRUE(manager.findMonitor(test1_monitor, test1_group));

  const std::string test2_group("test2");
  auto test2_monitor = std::make_shared<MockEndpointGroupMonitor>();
  EXPECT_FALSE(manager.findMonitor(test2_monitor, test2_group));
  manager.addMonitor(test2_monitor, test2_group);
  EXPECT_TRUE(manager.findMonitor(test2_monitor, test2_group));

  const std::string test3_group("test3");
  auto test3_monitor = std::make_shared<MockEndpointGroupMonitor>();
  EXPECT_FALSE(manager.findMonitor(test3_monitor, test3_group));
  manager.addMonitor(test3_monitor, test3_group);
  EXPECT_TRUE(manager.findMonitor(test3_monitor, test3_group));

  // Listen on resource test2.
  auto test_monitor = std::make_shared<MockEndpointGroupMonitor>();
  manager.addMonitor(test_monitor, test2_group);
  EXPECT_TRUE(manager.findMonitor(test_monitor, test2_group));

  const std::string version("1.0");
  envoy::config::endpoint::v3::EndpointGroup test_group_data;
  test_group_data.set_name(test1_group);
  EXPECT_CALL(*test1_monitor, update(_, _)).Times(1);
  EXPECT_CALL(*test2_monitor, update(_, _)).Times(0);
  EXPECT_CALL(*test3_monitor, update(_, _)).Times(0);
  manager.addOrUpdateEndpointGroup(test_group_data, version);

  test_group_data.set_name(test2_group);
  EXPECT_CALL(*test2_monitor, update(_, _)).Times(1);
  EXPECT_CALL(*test_monitor, update(_, _)).Times(1);
  EXPECT_CALL(*test1_monitor, update(_, _)).Times(0);
  EXPECT_CALL(*test3_monitor, update(_, _)).Times(0);
  manager.addOrUpdateEndpointGroup(test_group_data, version);

  // Remove the test3_monitor.
  test_group_data.set_name(test3_group);
  manager.removeMonitor(test3_monitor, test3_group);
  EXPECT_CALL(*test3_monitor, update(_, _)).Times(0);
  manager.addOrUpdateEndpointGroup(test_group_data, version);

  // Remove the test2_monitor.
  test_group_data.set_name(test2_group);
  manager.removeMonitor(test2_monitor, test2_group);
  EXPECT_CALL(*test2_monitor, update(_, _)).Times(0);
  EXPECT_CALL(*test_monitor, update(_, _)).Times(1);
  manager.addOrUpdateEndpointGroup(test_group_data, version);

  // Remove the endpoint group resource.
  manager.removeEndpointGroup(test1_group);
  // The monitor does not exist because the resource has been deleted.
  EXPECT_FALSE(manager.findMonitor(test1_monitor, test1_group));

  manager.removeEndpointGroup(test2_group);
  EXPECT_FALSE(manager.findMonitor(test2_monitor, test2_group));
  EXPECT_FALSE(manager.findMonitor(test3_monitor, test3_group));
}

} // namespace
} // namespace Upstream
} // namespace Envoy
