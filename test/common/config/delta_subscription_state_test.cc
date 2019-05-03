#include "common/config/delta_subscription_state.h"
#include "common/config/utility.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/local_info/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::UnorderedElementsAre;

namespace Envoy {
namespace Config {
namespace {

const char TypeUrl[] = "type.googleapis.com/envoy.api.v2.Cluster";

void deliverDiscoveryResponse(
    DeltaSubscriptionState& state,
    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& version_info) {
  envoy::api::v2::DeltaDiscoveryResponse message;
  *message.mutable_resources() = added_resources;
  *message.mutable_removed_resources() = removed_resources;
  message.set_system_version_info(version_info);
  state.handleResponse(message);
}

TEST(DeltaSubscriptionImplTest, ResourceGoneLeadsToBlankInitialVersion) {
  NiceMock<MockSubscriptionCallbacks<envoy::api::v2::Cluster>> callbacks;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  Stats::IsolatedStoreImpl store;
  SubscriptionStats stats = Utility::generateStats(store);
  // We start out interested in three resources: name1, name2, and name3.
  DeltaSubscriptionState state(TypeUrl, {"name1", "name2", "name3"}, callbacks, local_info,
                               std::chrono::milliseconds(0U), dispatcher, stats);
  {
    envoy::api::v2::DeltaDiscoveryRequest cur_request = state.getNextRequestAckless();
    EXPECT_THAT(cur_request.resource_names_subscribe(),
                UnorderedElementsAre("name1", "name2", "name3"));
  }

  {
    // The xDS server's first update includes items for name1 and 2, but not 3.
    Protobuf::RepeatedPtrField<envoy::api::v2::Resource> add1_2;
    auto* resource = add1_2.Add();
    resource->set_name("name1");
    resource->set_version("version1A");
    resource = add1_2.Add();
    resource->set_name("name2");
    resource->set_version("version2A");
    deliverDiscoveryResponse(state, add1_2, {}, "debugversion1");
    state.markStreamFresh(); // simulate a stream reconnection
    envoy::api::v2::DeltaDiscoveryRequest cur_request = state.getNextRequestAckless();
    EXPECT_EQ("version1A", cur_request.initial_resource_versions().at("name1"));
    EXPECT_EQ("version2A", cur_request.initial_resource_versions().at("name2"));
    EXPECT_EQ(cur_request.initial_resource_versions().end(),
              cur_request.initial_resource_versions().find("name3"));
  }

  {
    // The next update updates 1, removes 2, and adds 3. The map should then have 1 and 3.
    Protobuf::RepeatedPtrField<envoy::api::v2::Resource> add1_3;
    auto* resource = add1_3.Add();
    resource->set_name("name1");
    resource->set_version("version1B");
    resource = add1_3.Add();
    resource->set_name("name3");
    resource->set_version("version3A");
    Protobuf::RepeatedPtrField<std::string> remove2;
    *remove2.Add() = "name2";
    deliverDiscoveryResponse(state, add1_3, remove2, "debugversion2");
    state.markStreamFresh(); // simulate a stream reconnection
    envoy::api::v2::DeltaDiscoveryRequest cur_request = state.getNextRequestAckless();
    EXPECT_EQ("version1B", cur_request.initial_resource_versions().at("name1"));
    EXPECT_EQ(cur_request.initial_resource_versions().end(),
              cur_request.initial_resource_versions().find("name2"));
    EXPECT_EQ("version3A", cur_request.initial_resource_versions().at("name3"));
  }

  {
    // The next update removes 1 and 3. The map we send the server should be empty...
    Protobuf::RepeatedPtrField<std::string> remove1_3;
    *remove1_3.Add() = "name1";
    *remove1_3.Add() = "name3";
    deliverDiscoveryResponse(state, {}, remove1_3, "debugversion3");
    state.markStreamFresh(); // simulate a stream reconnection
    envoy::api::v2::DeltaDiscoveryRequest cur_request = state.getNextRequestAckless();
    EXPECT_TRUE(cur_request.initial_resource_versions().empty());
  }

  {
    // ...but our own map should remember our interest. In particular, losing interest in all 3
    // should cause their names to appear in the resource_names_unsubscribe field of a
    // DeltaDiscoveryRequest.
    state.updateResourceInterest({"name4"}); // note the lack of 1, 2, and 3
    state.markStreamFresh();                 // simulate a stream reconnection
    envoy::api::v2::DeltaDiscoveryRequest cur_request = state.getNextRequestAckless();
    EXPECT_THAT(cur_request.resource_names_subscribe(), UnorderedElementsAre("name4"));
    EXPECT_THAT(cur_request.resource_names_unsubscribe(),
                UnorderedElementsAre("name1", "name2", "name3"));
  }
}

// Checks that resources that were supposed to be present in the map last reconnect, but should not
// be present for this reconnect, are in fact not there.
TEST(DeltaSubscriptionImplTest, InitialVersionMapCleared) {}

} // namespace
} // namespace Config
} // namespace Envoy
