#include "common/config/resource_name.h"
#include "common/config/sotw_subscription_state.h"
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

const std::string TypeUrl = Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(
    envoy::config::core::v3::ApiVersion::V3);

class SotwSubscriptionStateTest : public testing::Test {
protected:
  SotwSubscriptionStateTest()
      : state_(TypeUrl, callbacks_, std::chrono::milliseconds(0U), dispatcher_) {
    state_.updateSubscriptionInterest({"name1", "name2", "name3"}, {});
    auto cur_request = getNextDiscoveryRequestAckless();
    EXPECT_THAT(cur_request->resource_names(), UnorderedElementsAre("name1", "name2", "name3"));
  }

  std::unique_ptr<envoy::api::v2::DiscoveryRequest> getNextDiscoveryRequestAckless() {
    auto* ptr = static_cast<envoy::api::v2::DiscoveryRequest*>(state_.getNextRequestAckless());
    return std::unique_ptr<envoy::api::v2::DiscoveryRequest>(ptr);
  }

  UpdateAck deliverDiscoveryResponse(const std::vector<std::string>& resource_names,
                                     const std::string& version_info, const std::string& nonce) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version_info);
    response.set_nonce(nonce);
    response.set_type_url(TypeUrl);
    Protobuf::RepeatedPtrField<envoy::config::endpoint::v3::ClusterLoadAssignment> typed_resources;
    for (const auto& resource_name : resource_names) {
      envoy::config::endpoint::v3::ClusterLoadAssignment* load_assignment = typed_resources.Add();
      load_assignment->set_cluster_name(resource_name);
      response.add_resources()->PackFrom(*load_assignment);
    }
    EXPECT_CALL(callbacks_, onConfigUpdate(_, version_info));
    return state_.handleResponse(&response);
  }

  UpdateAck deliverBadDiscoveryResponse(const std::string& version_info, const std::string& nonce) {
    envoy::service::discovery::v3::DiscoveryResponse message;
    message.set_version_info(version_info);
    message.set_nonce(nonce);
    EXPECT_CALL(callbacks_, onConfigUpdate(_, _)).WillOnce(Throw(EnvoyException("oh no")));
    return state_.handleResponse(&message);
  }

  NiceMock<MockUntypedConfigUpdateCallbacks> callbacks_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  // We start out interested in three resources: name1, name2, and name3.
  SotwSubscriptionState state_;
};

// Basic gaining/losing interest in resources should lead to changes in subscriptions.
TEST_F(SotwSubscriptionStateTest, SubscribeAndUnsubscribe) {
  {
    state_.updateSubscriptionInterest({"name4"}, {"name1"});
    auto cur_request = getNextDiscoveryRequestAckless();
    EXPECT_THAT(cur_request->resource_names(), UnorderedElementsAre("name2", "name3", "name4"));
  }
  {
    state_.updateSubscriptionInterest({"name1"}, {"name3", "name4"});
    auto cur_request = getNextDiscoveryRequestAckless();
    EXPECT_THAT(cur_request->resource_names(), UnorderedElementsAre("name1", "name2"));
  }
}

// Unlike delta, if SotW gets multiple interest updates before being able to send a request, they
// all collapse to a single update. However, even if the updates all cancel each other out, there
// still will be a request generated. All of the following tests explore different such cases.
TEST_F(SotwSubscriptionStateTest, RemoveThenAdd) {
  state_.updateSubscriptionInterest({}, {"name3"});
  state_.updateSubscriptionInterest({"name3"}, {});
  auto cur_request = getNextDiscoveryRequestAckless();
  EXPECT_THAT(cur_request->resource_names(), UnorderedElementsAre("name1", "name2", "name3"));
}

TEST_F(SotwSubscriptionStateTest, AddThenRemove) {
  state_.updateSubscriptionInterest({"name4"}, {});
  state_.updateSubscriptionInterest({}, {"name4"});
  auto cur_request = getNextDiscoveryRequestAckless();
  EXPECT_THAT(cur_request->resource_names(), UnorderedElementsAre("name1", "name2", "name3"));
}

TEST_F(SotwSubscriptionStateTest, AddRemoveAdd) {
  state_.updateSubscriptionInterest({"name4"}, {});
  state_.updateSubscriptionInterest({}, {"name4"});
  state_.updateSubscriptionInterest({"name4"}, {});
  auto cur_request = getNextDiscoveryRequestAckless();
  EXPECT_THAT(cur_request->resource_names(),
              UnorderedElementsAre("name1", "name2", "name3", "name4"));
}

TEST_F(SotwSubscriptionStateTest, RemoveAddRemove) {
  state_.updateSubscriptionInterest({}, {"name3"});
  state_.updateSubscriptionInterest({"name3"}, {});
  state_.updateSubscriptionInterest({}, {"name3"});
  auto cur_request = getNextDiscoveryRequestAckless();
  EXPECT_THAT(cur_request->resource_names(), UnorderedElementsAre("name1", "name2"));
}

TEST_F(SotwSubscriptionStateTest, BothAddAndRemove) {
  state_.updateSubscriptionInterest({"name4"}, {"name1", "name2", "name3"});
  state_.updateSubscriptionInterest({"name1", "name2", "name3"}, {"name4"});
  state_.updateSubscriptionInterest({"name4"}, {"name1", "name2", "name3"});
  auto cur_request = getNextDiscoveryRequestAckless();
  EXPECT_THAT(cur_request->resource_names(), UnorderedElementsAre("name4"));
}

TEST_F(SotwSubscriptionStateTest, CumulativeUpdates) {
  state_.updateSubscriptionInterest({"name4"}, {});
  state_.updateSubscriptionInterest({"name5"}, {});
  auto cur_request = getNextDiscoveryRequestAckless();
  EXPECT_THAT(cur_request->resource_names(),
              UnorderedElementsAre("name1", "name2", "name3", "name4", "name5"));
}

// Verifies that a sequence of good and bad responses from the server all get the appropriate
// ACKs/NACKs from Envoy.
TEST_F(SotwSubscriptionStateTest, AckGenerated) {
  // The xDS server's first response includes items for name1 and 2, but not 3.
  {
    UpdateAck ack = deliverDiscoveryResponse({"name1", "name2"}, "version1", "nonce1");
    EXPECT_EQ("nonce1", ack.nonce_);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // The next response updates 1 and 2, and adds 3.
  {
    UpdateAck ack = deliverDiscoveryResponse({"name1", "name2", "name3"}, "version2", "nonce2");
    EXPECT_EQ("nonce2", ack.nonce_);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // The next response tries but fails to update all 3, and so should produce a NACK.
  {
    UpdateAck ack = deliverBadDiscoveryResponse("version3", "nonce3");
    EXPECT_EQ("nonce3", ack.nonce_);
    EXPECT_NE(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
  // The last response successfully updates all 3.
  {
    UpdateAck ack = deliverDiscoveryResponse({"name1", "name2", "name3"}, "version4", "nonce4");
    EXPECT_EQ("nonce4", ack.nonce_);
    EXPECT_EQ(Grpc::Status::WellKnownGrpcStatus::Ok, ack.error_detail_.code());
  }
}

TEST_F(SotwSubscriptionStateTest, CheckUpdatePending) {
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

TEST_F(SotwSubscriptionStateTest, HandleEstablishmentFailure) {
  // Although establishment failure is not supposed to cause an onConfigUpdateFailed() on the
  // ultimate actual subscription callbacks, the callbacks reference held is actually to
  // the WatchMap, which then calls GrpcSubscriptionImpl(s). It is the GrpcSubscriptionImpl
  // that will decline to pass on an onConfigUpdateFailed(ConnectionFailure).
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(_, _));
  state_.handleEstablishmentFailure();
}

} // namespace
} // namespace Config
} // namespace Envoy
