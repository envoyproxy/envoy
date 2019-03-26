#include "test/common/config/delta_subscription_test_harness.h"

using testing::AnyNumber;
using testing::UnorderedElementsAre;

namespace Envoy {
namespace Config {
namespace {

class DeltaSubscriptionImplTest : public DeltaSubscriptionTestHarness, public testing::Test {};

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
  subscription_->onConfigUpdate(add1_2, {}, "debugversion1");
  subscription_->handleStreamEstablished();
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
  subscription_->onConfigUpdate(add1_3, remove2, "debugversion2");
  subscription_->handleStreamEstablished();
  cur_request = subscription_->internalRequestStateForTest();
  EXPECT_EQ("version1B", cur_request.initial_resource_versions().at("name1"));
  EXPECT_EQ(cur_request.initial_resource_versions().end(),
            cur_request.initial_resource_versions().find("name2"));
  EXPECT_EQ("version3A", cur_request.initial_resource_versions().at("name3"));

  // The next update removes 1 and 3. The map we send the server should be empty...
  Protobuf::RepeatedPtrField<std::string> remove1_3;
  *remove1_3.Add() = "name1";
  *remove1_3.Add() = "name3";
  subscription_->onConfigUpdate({}, remove1_3, "debugversion3");
  subscription_->handleStreamEstablished();
  cur_request = subscription_->internalRequestStateForTest();
  EXPECT_TRUE(cur_request.initial_resource_versions().empty());

  // ...but our own map should remember our interest. In particular, losing interest in all 3 should
  // cause their names to appear in the resource_names_unsubscribe field of a DeltaDiscoveryRequest.
  subscription_->resume(); // now we do want the request to actually get sendMessage()'d.
  EXPECT_CALL(async_stream_, sendMessage(_, _)).WillOnce([](const Protobuf::Message& msg, bool) {
    auto sent_request = static_cast<const envoy::api::v2::DeltaDiscoveryRequest*>(&msg);
    EXPECT_THAT(sent_request->resource_names_subscribe(), UnorderedElementsAre("name4"));
    EXPECT_THAT(sent_request->resource_names_unsubscribe(),
                UnorderedElementsAre("name1", "name2", "name3"));
  });
  subscription_->subscribe({"name4"}); // (implies "we no longer care about name1,2,3")
}

} // namespace
} // namespace Config
} // namespace Envoy
