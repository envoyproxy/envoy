#include <memory>

#include "common/config/delta_subscription_impl.h"
#include "common/config/utility.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Config {
namespace {

// When the xDS server informs Envoy that a resource is gone, that resource should be removed from
// the initial versions map that Envoy sends upon stream reconnect. A resource name we are
// interested in but have not gotten any version of should also not be in that map.
TEST(DeltaSubscriptionImplTest, ResourceGoneLeadsToBlankInitialVersion) {
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockRandomGenerator> random;
  Envoy::Config::RateLimitSettings rate_limit_settings;
  Stats::IsolatedStoreImpl stats_store;
  SubscriptionStats stats = Utility::generateStats(stats_store);
  NiceMock<Config::MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> callbacks;
  envoy::api::v2::core::Node node;
  node.set_id("fo0");
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  EXPECT_CALL(local_info, node()).WillRepeatedly(testing::ReturnRef(node));

  DeltaSubscriptionImpl<envoy::api::v2::ClusterLoadAssignment> subscription(
      local_info, std::make_unique<NiceMock<Grpc::MockAsyncClient>>(), dispatcher,
      *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.api.v2.EndpointDiscoveryService.StreamEndpoints"),
      random, stats_store, rate_limit_settings, stats, std::chrono::milliseconds(123));

  // Envoy is interested in three resources: name1, name2, and name3.
  subscription.start({"name1", "name2", "name3"}, callbacks);

  // The xDS server's first update includes items for name1 and 2, but not 3.
  Protobuf::RepeatedPtrField<envoy::api::v2::Resource> add1_2;
  auto* resource = add1_2.Add();
  resource->set_name("name1");
  resource->set_version("version1A");
  resource = add1_2.Add();
  resource->set_name("name2");
  resource->set_version("version2A");
  subscription.onConfigUpdate(add1_2, {}, "debugversion1");
  subscription.handleStreamEstablished();
  envoy::api::v2::DeltaDiscoveryRequest cur_request = subscription.internalRequestStateForTest();
  EXPECT_EQ("version1A", cur_request.initial_resource_versions().at("name1"));
  EXPECT_EQ("version2A", cur_request.initial_resource_versions().at("name2"));
  EXPECT_EQ(cur_request.initial_resource_versions().end(),
            cur_request.initial_resource_versions().find("name3"));

  // The next update updates 1, removes 2, and adds 3. The map should then have 1 and 3.
  Protobuf::RepeatedPtrField<envoy::api::v2::Resource> add1_3;
  resource = add1_3.Add();
  resource->set_name("name1");
  resource->set_version("version1B");
  resource = add1_3.Add();
  resource->set_name("name3");
  resource->set_version("version3A");
  Protobuf::RepeatedPtrField<std::string> remove2;
  *remove2.Add() = "name2";
  subscription.onConfigUpdate(add1_3, remove2, "debugversion2");
  subscription.handleStreamEstablished();
  cur_request = subscription.internalRequestStateForTest();
  EXPECT_EQ("version1B", cur_request.initial_resource_versions().at("name1"));
  EXPECT_EQ(cur_request.initial_resource_versions().end(),
            cur_request.initial_resource_versions().find("name2"));
  EXPECT_EQ("version3A", cur_request.initial_resource_versions().at("name3"));

  // The next update removes 1 and 3. The map should be empty.
  Protobuf::RepeatedPtrField<std::string> remove1_3;
  *remove1_3.Add() = "name1";
  *remove1_3.Add() = "name3";
  subscription.onConfigUpdate({}, remove1_3, "debugversion3");
  subscription.handleStreamEstablished();
  cur_request = subscription.internalRequestStateForTest();
  EXPECT_TRUE(cur_request.initial_resource_versions().empty());
}

} // namespace
} // namespace Config
} // namespace Envoy
