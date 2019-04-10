#include "test/common/config/delta_subscription_test_harness.h"

using testing::AnyNumber;
using testing::UnorderedElementsAre;

namespace Envoy {
namespace Config {
namespace {

class DeltaSubscriptionImplTest : public DeltaSubscriptionTestHarness, public testing::Test {
protected:
  void deliverDiscoveryResponse(
      const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
      const std::string& version_info) {
    auto message = std::make_unique<envoy::api::v2::DeltaDiscoveryResponse>();
    *message->mutable_resources() = added_resources;
    *message->mutable_removed_resources() = removed_resources;
    message->set_system_version_info(version_info);
    subscription_->onDiscoveryResponse(std::move(message));
  }
};

TEST_F(DeltaSubscriptionImplTest, ResourceGoneLeadsToBlankInitialVersion) {
  // Envoy is interested in three resources: name1, name2, and name3.
  startSubscription({"name1", "name2", "name3"});

  // Ignore these for now, although at the very end there is one we will care about.
  EXPECT_CALL(async_stream_, sendMessage(_, _)).Times(AnyNumber());

  // Semi-hack: we don't want the requests to actually get sent, since it would clear out the
  // request_ that we want to inspect. pause() does the trick!
  subscription_->pause();

  // The xDS server's first update includes items for name1 and 2, but not 3.
  Protobuf::RepeatedPtrField<envoy::api::v2::Resource> add1_2;
  auto* resource = add1_2.Add();
  resource->set_name("name1");
  resource->set_version("version1A");
  resource = add1_2.Add();
  resource->set_name("name2");
  resource->set_version("version2A");
  deliverDiscoveryResponse(add1_2, {}, "debugversion1");
  subscription_->onStreamEstablished();
  envoy::api::v2::DeltaDiscoveryRequest cur_request = subscription_->internalRequestStateForTest();
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
  deliverDiscoveryResponse(add1_3, remove2, "debugversion2");
  subscription_->onStreamEstablished();
  cur_request = subscription_->internalRequestStateForTest();
  EXPECT_EQ("version1B", cur_request.initial_resource_versions().at("name1"));
  EXPECT_EQ(cur_request.initial_resource_versions().end(),
            cur_request.initial_resource_versions().find("name2"));
  EXPECT_EQ("version3A", cur_request.initial_resource_versions().at("name3"));

  // The next update removes 1 and 3. The map we send the server should be empty...
  Protobuf::RepeatedPtrField<std::string> remove1_3;
  *remove1_3.Add() = "name1";
  *remove1_3.Add() = "name3";
  deliverDiscoveryResponse({}, remove1_3, "debugversion3");
  subscription_->onStreamEstablished();
  cur_request = subscription_->internalRequestStateForTest();
  EXPECT_TRUE(cur_request.initial_resource_versions().empty());

  // ...but our own map should remember our interest. In particular, losing interest in all 3 should
  // cause their names to appear in the resource_names_unsubscribe field of a DeltaDiscoveryRequest.
  subscription_->resume(); // we do want the final subscribe() to do a sendMessage().
  expectSendMessage({"name4"}, {"name1", "name2", "name3"}, Grpc::Status::GrpcStatus::Ok, "");
  subscription_->updateResources({"name4"}); // (implies "we no longer care about name1,2,3")
}

// If Envoy decided it wasn't interested in a resource and then (before a request was sent) decided
// it was again, for all we know, it dropped that resource in between and needs to retrieve it
// again. So, we *should* send a request "re-"subscribing. This means that the server needs to
// interpret the resource_names_subscribe field as "send these resources even if you think Envoy
// already has them".
TEST_F(DeltaSubscriptionImplTest, RemoveThenAdd) {
  startSubscription({"name1", "name2", "name3"});
  subscription_->pause(); // Pause because we're testing multiple updates in between request sends.
  subscription_->updateResources({"name1", "name2"});
  subscription_->updateResources({"name1", "name2", "name3"});
  expectSendMessage({"name3"}, {}, Grpc::Status::GrpcStatus::Ok, "");
  subscription_->resume();
}

// Due to the need for the behavior tested by LoseThenGainSent, gain-then-losing interest in
// resource X before the DeltaDiscoveryRequest is sent causes that request to "unsubscribe" from X.
// Ideally we would have the request simply not include any mention of X. Oh well. This test is just
// here to illustrate that this behavior exists, not to enforce that it should be like this. What
// *is* important: the server must happily and cleanly ignore "unsubscribe from [resource name I
// have never before referred to]" requests.
TEST_F(DeltaSubscriptionImplTest, AddThenRemove) {
  startSubscription({"name1", "name2", "name3"});
  subscription_->pause(); // Pause because we're testing multiple updates in between request sends.
  subscription_->updateResources({"name1", "name2", "name3", "name4"});
  subscription_->updateResources({"name1", "name2", "name3"});
  expectSendMessage({}, {"name4"}, Grpc::Status::GrpcStatus::Ok, "");
  subscription_->resume();
}

// add/remove/add == add.
TEST_F(DeltaSubscriptionImplTest, AddRemoveAdd) {
  startSubscription({"name1", "name2", "name3"});
  subscription_->pause();
  subscription_->updateResources({"name1", "name2", "name3", "name4"});
  subscription_->updateResources({"name1", "name2", "name3"});
  subscription_->updateResources({"name1", "name2", "name3", "name4"});
  expectSendMessage({"name4"}, {}, Grpc::Status::GrpcStatus::Ok, "");
  subscription_->resume();
}

// remove/add/remove == remove.
TEST_F(DeltaSubscriptionImplTest, RemoveAddRemove) {
  startSubscription({"name1", "name2", "name3"});
  subscription_->pause();
  subscription_->updateResources({"name1", "name2"});
  subscription_->updateResources({"name1", "name2", "name3"});
  subscription_->updateResources({"name1", "name2"});
  expectSendMessage({}, {"name3"}, Grpc::Status::GrpcStatus::Ok, "");
  subscription_->resume();
}

// Starts with 1,2,3. 4 is added/removed/added. In those same updates, 1,2,3 are
// removed/added/removed. End result should be 4 added and 1,2,3 removed.
TEST_F(DeltaSubscriptionImplTest, BothAddAndRemove) {
  startSubscription({"name1", "name2", "name3"});
  subscription_->pause();
  subscription_->updateResources({"name4"});
  subscription_->updateResources({"name1", "name2", "name3"});
  subscription_->updateResources({"name4"});
  expectSendMessage({"name4"}, {"name1", "name2", "name3"}, Grpc::Status::GrpcStatus::Ok, "");
  subscription_->resume();
}

// If one update fails to send a request (pausing, rate limit, no stream are all identical for this
// purpose), and then another update comes along and also fails, when the request is finally sent,
// both should be present. (A previous version of the code would have had 1->12 generate a diff of
// 2, then 12->123 generate a diff of 3, which would replace 2).
TEST_F(DeltaSubscriptionImplTest, CumulativeUpdates) {
  startSubscription({"name1"});
  subscription_->pause();
  subscription_->updateResources({"name1", "name2"});
  subscription_->updateResources({"name1", "name2", "name3"});
  expectSendMessage({"name2", "name3"}, {}, Grpc::Status::GrpcStatus::Ok, "");
  subscription_->resume();
}

} // namespace
} // namespace Config
} // namespace Envoy
