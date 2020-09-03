#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/config/delta_subscription_state.h"
#include "common/config/utility.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/local_info/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Throw;
using testing::UnorderedElementsAre;

namespace Envoy {
namespace Config {
namespace {

const char TypeUrl[] = "type.googleapis.com/envoy.api.v2.Cluster";

class DeltaSubscriptionStateTest : public testing::Test {
protected:
  DeltaSubscriptionStateTest() : state_(TypeUrl, callbacks_, local_info_) {
    state_.updateSubscriptionInterest({"name1", "name2", "name3"}, {});
    envoy::service::discovery::v3::DeltaDiscoveryRequest cur_request =
        state_.getNextRequestAckless();
    EXPECT_THAT(cur_request.resource_names_subscribe(),
                UnorderedElementsAre("name1", "name2", "name3"));
  }

  UpdateAck deliverDiscoveryResponse(
      const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>& added_resources,
      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
      const std::string& version_info, absl::optional<std::string> nonce = absl::nullopt,
      bool expect_config_update_call = true) {
    envoy::service::discovery::v3::DeltaDiscoveryResponse message;
    *message.mutable_resources() = added_resources;
    *message.mutable_removed_resources() = removed_resources;
    message.set_system_version_info(version_info);
    if (nonce.has_value()) {
      message.set_nonce(nonce.value());
    }
    EXPECT_CALL(callbacks_, onConfigUpdate(_, _, _)).Times(expect_config_update_call ? 1 : 0);
    return state_.handleResponse(message);
  }

  UpdateAck deliverBadDiscoveryResponse(
      const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>& added_resources,
      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
      const std::string& version_info, std::string nonce, std::string error_message) {
    envoy::service::discovery::v3::DeltaDiscoveryResponse message;
    *message.mutable_resources() = added_resources;
    *message.mutable_removed_resources() = removed_resources;
    message.set_system_version_info(version_info);
    message.set_nonce(nonce);
    EXPECT_CALL(callbacks_, onConfigUpdate(_, _, _)).WillOnce(Throw(EnvoyException(error_message)));
    return state_.handleResponse(message);
  }

  NiceMock<MockUntypedConfigUpdateCallbacks> callbacks_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  // We start out interested in three resources: name1, name2, and name3.
  DeltaSubscriptionState state_;
};

Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>
populateRepeatedResource(std::vector<std::pair<std::string, std::string>> items) {
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add_to;
  for (const auto& item : items) {
    auto* resource = add_to.Add();
    resource->set_name(item.first);
    resource->set_version(item.second);
  }
  return add_to;
}

// Basic gaining/losing interest in resources should lead to subscription updates.
TEST_F(DeltaSubscriptionStateTest, SubscribeAndUnsubscribe) {
  {
    state_.updateSubscriptionInterest({"name4"}, {"name1"});
    envoy::service::discovery::v3::DeltaDiscoveryRequest cur_request =
        state_.getNextRequestAckless();
    EXPECT_THAT(cur_request.resource_names_subscribe(), UnorderedElementsAre("name4"));
    EXPECT_THAT(cur_request.resource_names_unsubscribe(), UnorderedElementsAre("name1"));
  }
  {
    state_.updateSubscriptionInterest({"name1"}, {"name3", "name4"});
    envoy::service::discovery::v3::DeltaDiscoveryRequest cur_request =
        state_.getNextRequestAckless();
    EXPECT_THAT(cur_request.resource_names_subscribe(), UnorderedElementsAre("name1"));
    EXPECT_THAT(cur_request.resource_names_unsubscribe(), UnorderedElementsAre("name3", "name4"));
  }
}

// Delta xDS reliably queues up and sends all discovery requests, even in situations where it isn't
// strictly necessary. E.g.: if you subscribe but then unsubscribe to a given resource, all before a
// request was able to be sent, two requests will be sent. The following tests demonstrate this.
//
// If Envoy decided it wasn't interested in a resource and then (before a request was sent) decided
// it was again, for all we know, it dropped that resource in between and needs to retrieve it
// again. So, we *should* send a request "re-"subscribing. This means that the server needs to
// interpret the resource_names_subscribe field as "send these resources even if you think Envoy
// already has them".
TEST_F(DeltaSubscriptionStateTest, RemoveThenAdd) {
  state_.updateSubscriptionInterest({}, {"name3"});
  state_.updateSubscriptionInterest({"name3"}, {});
  envoy::service::discovery::v3::DeltaDiscoveryRequest cur_request = state_.getNextRequestAckless();
  EXPECT_THAT(cur_request.resource_names_subscribe(), UnorderedElementsAre("name3"));
  EXPECT_TRUE(cur_request.resource_names_unsubscribe().empty());
}

// Due to how our implementation provides the required behavior tested in RemoveThenAdd, the
// add-then-remove case *also* causes the resource to be referred to in the request (as an
// unsubscribe).
// Unlike the remove-then-add case, this one really is unnecessary, and ideally we would have
// the request simply not include any mention of the resource. Oh well.
// This test is just here to illustrate that this behavior exists, not to enforce that it
// should be like this. What *is* important: the server must happily and cleanly ignore
// "unsubscribe from [resource name I have never before referred to]" requests.
TEST_F(DeltaSubscriptionStateTest, AddThenRemove) {
  state_.updateSubscriptionInterest({"name4"}, {});
  state_.updateSubscriptionInterest({}, {"name4"});
  envoy::service::discovery::v3::DeltaDiscoveryRequest cur_request = state_.getNextRequestAckless();
  EXPECT_TRUE(cur_request.resource_names_subscribe().empty());
  EXPECT_THAT(cur_request.resource_names_unsubscribe(), UnorderedElementsAre("name4"));
}

// add/remove/add == add.
TEST_F(DeltaSubscriptionStateTest, AddRemoveAdd) {
  state_.updateSubscriptionInterest({"name4"}, {});
  state_.updateSubscriptionInterest({}, {"name4"});
  state_.updateSubscriptionInterest({"name4"}, {});
  envoy::service::discovery::v3::DeltaDiscoveryRequest cur_request = state_.getNextRequestAckless();
  EXPECT_THAT(cur_request.resource_names_subscribe(), UnorderedElementsAre("name4"));
  EXPECT_TRUE(cur_request.resource_names_unsubscribe().empty());
}

// remove/add/remove == remove.
TEST_F(DeltaSubscriptionStateTest, RemoveAddRemove) {
  state_.updateSubscriptionInterest({}, {"name3"});
  state_.updateSubscriptionInterest({"name3"}, {});
  state_.updateSubscriptionInterest({}, {"name3"});
  envoy::service::discovery::v3::DeltaDiscoveryRequest cur_request = state_.getNextRequestAckless();
  EXPECT_TRUE(cur_request.resource_names_subscribe().empty());
  EXPECT_THAT(cur_request.resource_names_unsubscribe(), UnorderedElementsAre("name3"));
}

// Starts with 1,2,3. 4 is added/removed/added. In those same updates, 1,2,3 are
// removed/added/removed. End result should be 4 added and 1,2,3 removed.
TEST_F(DeltaSubscriptionStateTest, BothAddAndRemove) {
  state_.updateSubscriptionInterest({"name4"}, {"name1", "name2", "name3"});
  state_.updateSubscriptionInterest({"name1", "name2", "name3"}, {"name4"});
  state_.updateSubscriptionInterest({"name4"}, {"name1", "name2", "name3"});
  envoy::service::discovery::v3::DeltaDiscoveryRequest cur_request = state_.getNextRequestAckless();
  EXPECT_THAT(cur_request.resource_names_subscribe(), UnorderedElementsAre("name4"));
  EXPECT_THAT(cur_request.resource_names_unsubscribe(),
              UnorderedElementsAre("name1", "name2", "name3"));
}

TEST_F(DeltaSubscriptionStateTest, CumulativeUpdates) {
  state_.updateSubscriptionInterest({"name4"}, {});
  state_.updateSubscriptionInterest({"name5"}, {});
  envoy::service::discovery::v3::DeltaDiscoveryRequest cur_request = state_.getNextRequestAckless();
  EXPECT_THAT(cur_request.resource_names_subscribe(), UnorderedElementsAre("name4", "name5"));
  EXPECT_TRUE(cur_request.resource_names_unsubscribe().empty());
}

// Verifies that a sequence of good and bad responses from the server all get the appropriate
// ACKs/NACKs from Envoy.
TEST_F(DeltaSubscriptionStateTest, AckGenerated) {
  // The xDS server's first response includes items for name1 and 2, but not 3.
  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource({{"name1", "version1A"}, {"name2", "version2A"}});
    UpdateAck ack = deliverDiscoveryResponse(added_resources, {}, "debug1", "nonce1");
    EXPECT_EQ("nonce1", ack.nonce_);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // The next response updates 1 and 2, and adds 3.
  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource(
            {{"name1", "version1B"}, {"name2", "version2B"}, {"name3", "version3A"}});
    UpdateAck ack = deliverDiscoveryResponse(added_resources, {}, "debug2", "nonce2");
    EXPECT_EQ("nonce2", ack.nonce_);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // The next response tries but fails to update all 3, and so should produce a NACK.
  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource(
            {{"name1", "version1C"}, {"name2", "version2C"}, {"name3", "version3B"}});
    UpdateAck ack = deliverBadDiscoveryResponse(added_resources, {}, "debug3", "nonce3", "oh no");
    EXPECT_EQ("nonce3", ack.nonce_);
    EXPECT_NE(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // The last response successfully updates all 3.
  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource(
            {{"name1", "version1D"}, {"name2", "version2D"}, {"name3", "version3C"}});
    UpdateAck ack = deliverDiscoveryResponse(added_resources, {}, "debug4", "nonce4");
    EXPECT_EQ("nonce4", ack.nonce_);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // Bad response error detail is truncated if it's too large.
  {
    const std::string very_large_error_message(1 << 20, 'A');
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources =
        populateRepeatedResource(
            {{"name1", "version1D"}, {"name2", "version2D"}, {"name3", "version3D"}});
    UpdateAck ack = deliverBadDiscoveryResponse(added_resources, {}, "debug5", "nonce5",
                                                very_large_error_message);
    EXPECT_EQ("nonce5", ack.nonce_);
    EXPECT_NE(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
    EXPECT_TRUE(absl::EndsWith(ack.error_detail_.message(), "AAAAAAA...(truncated)"));
    EXPECT_LT(ack.error_detail_.message().length(), very_large_error_message.length());
  }
}

// Tests population of the initial_resource_versions map in the first request of a new stream.
// Tests that
// 1) resources we have a version of are present in the map,
// 2) resources we are interested in but don't have are not present, and
// 3) resources we have lost interest in are not present.
TEST_F(DeltaSubscriptionStateTest, ResourceGoneLeadsToBlankInitialVersion) {
  {
    // The xDS server's first update includes items for name1 and 2, but not 3.
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add1_2 =
        populateRepeatedResource({{"name1", "version1A"}, {"name2", "version2A"}});
    deliverDiscoveryResponse(add1_2, {}, "debugversion1");
    state_.markStreamFresh(); // simulate a stream reconnection
    envoy::service::discovery::v3::DeltaDiscoveryRequest cur_request =
        state_.getNextRequestAckless();
    EXPECT_EQ("version1A", cur_request.initial_resource_versions().at("name1"));
    EXPECT_EQ("version2A", cur_request.initial_resource_versions().at("name2"));
    EXPECT_EQ(cur_request.initial_resource_versions().end(),
              cur_request.initial_resource_versions().find("name3"));
  }

  {
    // The next update updates 1, removes 2, and adds 3. The map should then have 1 and 3.
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add1_3 =
        populateRepeatedResource({{"name1", "version1B"}, {"name3", "version3A"}});
    Protobuf::RepeatedPtrField<std::string> remove2;
    *remove2.Add() = "name2";
    deliverDiscoveryResponse(add1_3, remove2, "debugversion2");
    state_.markStreamFresh(); // simulate a stream reconnection
    envoy::service::discovery::v3::DeltaDiscoveryRequest cur_request =
        state_.getNextRequestAckless();
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
    deliverDiscoveryResponse({}, remove1_3, "debugversion3");
    state_.markStreamFresh(); // simulate a stream reconnection
    envoy::service::discovery::v3::DeltaDiscoveryRequest cur_request =
        state_.getNextRequestAckless();
    EXPECT_TRUE(cur_request.initial_resource_versions().empty());
  }

  {
    // ...but our own map should remember our interest. In particular, losing interest in a
    // resource should cause its name to appear in the next request's resource_names_unsubscribe.
    state_.updateSubscriptionInterest({"name4"}, {"name1", "name2"});
    envoy::service::discovery::v3::DeltaDiscoveryRequest cur_request =
        state_.getNextRequestAckless();
    EXPECT_THAT(cur_request.resource_names_subscribe(), UnorderedElementsAre("name4"));
    EXPECT_THAT(cur_request.resource_names_unsubscribe(), UnorderedElementsAre("name1", "name2"));
  }
}

// Upon a reconnection, the server is supposed to assume a blank slate for the Envoy's state
// (hence the need for initial_resource_versions). The resource_names_subscribe of the first
// message must therefore be every resource the Envoy is interested in.
//
// resource_names_unsubscribe, on the other hand, is always blank in the first request - even if,
// in between the last request of the last stream and the first request of the new stream, Envoy
// lost interest in a resource. The unsubscription implicitly takes effect by simply saying
// nothing about the resource in the newly reconnected stream.
TEST_F(DeltaSubscriptionStateTest, SubscribeAndUnsubscribeAfterReconnect) {
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add1_2 =
      populateRepeatedResource({{"name1", "version1A"}, {"name2", "version2A"}});
  deliverDiscoveryResponse(add1_2, {}, "debugversion1");

  state_.updateSubscriptionInterest({"name4"}, {"name1"});
  state_.markStreamFresh(); // simulate a stream reconnection
  envoy::service::discovery::v3::DeltaDiscoveryRequest cur_request = state_.getNextRequestAckless();
  // Regarding the resource_names_subscribe field:
  // name1: do not include: we lost interest.
  // name2: yes do include: we're interested and we have a version of it.
  // name3: yes do include: even though we don't have a version of it, we are interested.
  // name4: yes do include: we are newly interested. (If this wasn't a stream reconnect, only
  // name4
  //                        would belong in this subscribe field).
  EXPECT_THAT(cur_request.resource_names_subscribe(),
              UnorderedElementsAre("name2", "name3", "name4"));
  EXPECT_TRUE(cur_request.resource_names_unsubscribe().empty());
}

// initial_resource_versions should not be present on messages after the first in a stream.
TEST_F(DeltaSubscriptionStateTest, InitialVersionMapFirstMessageOnly) {
  // First, verify that the first message of a new stream sends initial versions.
  {
    // The xDS server's first update gives us all three resources.
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add_all =
        populateRepeatedResource(
            {{"name1", "version1A"}, {"name2", "version2A"}, {"name3", "version3A"}});
    deliverDiscoveryResponse(add_all, {}, "debugversion1");
    state_.markStreamFresh(); // simulate a stream reconnection
    envoy::service::discovery::v3::DeltaDiscoveryRequest cur_request =
        state_.getNextRequestAckless();
    EXPECT_EQ("version1A", cur_request.initial_resource_versions().at("name1"));
    EXPECT_EQ("version2A", cur_request.initial_resource_versions().at("name2"));
    EXPECT_EQ("version3A", cur_request.initial_resource_versions().at("name3"));
  }
  // Then, after updating the resources but not reconnecting the stream, verify that initial
  // versions are not sent.
  {
    state_.updateSubscriptionInterest({"name4"}, {});
    // The xDS server updates our resources, and gives us our newly requested one too.
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> add_all =
        populateRepeatedResource({{"name1", "version1B"},
                                  {"name2", "version2B"},
                                  {"name3", "version3B"},
                                  {"name4", "version4A"}});
    deliverDiscoveryResponse(add_all, {}, "debugversion2");
    envoy::service::discovery::v3::DeltaDiscoveryRequest cur_request =
        state_.getNextRequestAckless();
    EXPECT_TRUE(cur_request.initial_resource_versions().empty());
  }
}

TEST_F(DeltaSubscriptionStateTest, CheckUpdatePending) {
  // Note that the test fixture ctor causes the first request to be "sent", so we start in the
  // middle of a stream, with our initially interested resources having been requested already.
  EXPECT_FALSE(state_.subscriptionUpdatePending());
  state_.updateSubscriptionInterest({}, {}); // no change
  EXPECT_FALSE(state_.subscriptionUpdatePending());
  state_.markStreamFresh();
  EXPECT_TRUE(state_.subscriptionUpdatePending());  // no change, BUT fresh stream
  state_.updateSubscriptionInterest({}, {"name3"}); // one removed
  EXPECT_TRUE(state_.subscriptionUpdatePending());
  state_.updateSubscriptionInterest({"name3"}, {}); // one added
  EXPECT_TRUE(state_.subscriptionUpdatePending());
}

// The next three tests test that duplicate resource names (whether additions or removals) cause
// DeltaSubscriptionState to reject the update without even trying to hand it to the consuming
// API's onConfigUpdate().
TEST_F(DeltaSubscriptionStateTest, DuplicatedAdd) {
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> additions =
      populateRepeatedResource({{"name1", "version1A"}, {"name1", "sdfsdfsdfds"}});
  UpdateAck ack = deliverDiscoveryResponse(additions, {}, "debugversion1", absl::nullopt, false);
  EXPECT_EQ("duplicate name name1 found among added/updated resources",
            ack.error_detail_.message());
}

TEST_F(DeltaSubscriptionStateTest, DuplicatedRemove) {
  Protobuf::RepeatedPtrField<std::string> removals;
  *removals.Add() = "name1";
  *removals.Add() = "name1";
  UpdateAck ack = deliverDiscoveryResponse({}, removals, "debugversion1", absl::nullopt, false);
  EXPECT_EQ("duplicate name name1 found in the union of added+removed resources",
            ack.error_detail_.message());
}

TEST_F(DeltaSubscriptionStateTest, AddedAndRemoved) {
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> additions =
      populateRepeatedResource({{"name1", "version1A"}});
  Protobuf::RepeatedPtrField<std::string> removals;
  *removals.Add() = "name1";
  UpdateAck ack =
      deliverDiscoveryResponse(additions, removals, "debugversion1", absl::nullopt, false);
  EXPECT_EQ("duplicate name name1 found in the union of added+removed resources",
            ack.error_detail_.message());
}

} // namespace
} // namespace Config
} // namespace Envoy
