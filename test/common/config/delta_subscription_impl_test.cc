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

  subscription.start({"name1", "name2"}, callbacks);

  Protobuf::RepeatedPtrField<envoy::api::v2::Resource> added;
  auto* resource = added.Add();
  resource->set_name("name1");
  resource->set_version("version1A");
  resource = added.Add();
  resource->set_name("name2");
  resource->set_version("version2A");
  subscription.onConfigUpdate(added, {}, "debugversion1");
  subscription.handleStreamEstablished();
  envoy::api::v2::DeltaDiscoveryRequest cur_request = subscription.stateOfRequest();
  EXPECT_EQ("version1A", cur_request.initial_resource_versions().at("name1"));
  EXPECT_EQ("version2A", cur_request.initial_resource_versions().at("name2"));

  Protobuf::RepeatedPtrField<std::string> removed1;
  *removed1.Add() = "name1";
  subscription.onConfigUpdate({}, removed1, "debugversion2");
  subscription.handleStreamEstablished();
  cur_request = subscription.stateOfRequest();
  EXPECT_EQ(cur_request.initial_resource_versions().end(),
            cur_request.initial_resource_versions().find("name1"));
  EXPECT_EQ("version2A", cur_request.initial_resource_versions().at("name2"));

  Protobuf::RepeatedPtrField<std::string> removed2;
  *removed2.Add() = "name2";
  subscription.onConfigUpdate({}, removed2, "debugversion3");
  subscription.handleStreamEstablished();
  cur_request = subscription.stateOfRequest();
  EXPECT_EQ(cur_request.initial_resource_versions().end(),
            cur_request.initial_resource_versions().find("name1"));
  EXPECT_EQ(cur_request.initial_resource_versions().end(),
            cur_request.initial_resource_versions().find("name2"));
}

} // namespace
} // namespace Config
} // namespace Envoy
