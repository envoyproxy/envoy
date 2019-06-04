#include "test/common/config/delta_subscription_test_harness.h"

namespace Envoy {
namespace Config {
namespace {

class DeltaSubscriptionImplTest : public DeltaSubscriptionTestHarness, public testing::Test {
protected:
  DeltaSubscriptionImplTest() : DeltaSubscriptionTestHarness() {}

  // We need to destroy the subscription before the test's destruction, because the subscription's
  // destructor removes its watch from the GrpcDeltaXdsContext, and that removal process involves
  // some things held by the test fixture.
  void TearDown() override {
    //  if (subscription_started_) {
    //  EXPECT_CALL(async_stream_, sendMessage(_,_));
    //     subscription_.reset();
    // }
    doSubscriptionTearDown();
  }
};

TEST_F(DeltaSubscriptionImplTest, UpdateResourcesCausesRequest) {
  startSubscription({"name1", "name2", "name3"});
  expectSendMessage({"name4"}, {"name1", "name2"}, Grpc::Status::GrpcStatus::Ok, "", {});
  subscription_->updateResources({"name3", "name4"});
  expectSendMessage({"name1", "name2"}, {}, Grpc::Status::GrpcStatus::Ok, "", {});
  subscription_->updateResources({"name1", "name2", "name3", "name4"});
  expectSendMessage({}, {"name1", "name2"}, Grpc::Status::GrpcStatus::Ok, "", {});
  subscription_->updateResources({"name3", "name4"});
  expectSendMessage({"name1", "name2"}, {}, Grpc::Status::GrpcStatus::Ok, "", {});
  subscription_->updateResources({"name1", "name2", "name3", "name4"});
  expectSendMessage({}, {"name1", "name2", "name3"}, Grpc::Status::GrpcStatus::Ok, "", {});
  subscription_->updateResources({"name4"});
}

// Checks that after a pause(), no requests are sent until resume().
// Also demonstrates the collapsing of subscription interest updates into a single
// request. (This collapsing happens any time multiple updates arrive before a request
// can be sent, not just with pausing: rate limiting or a down gRPC stream would also do it).
TEST_F(DeltaSubscriptionImplTest, PauseHoldsRequest) {
  startSubscription({"name1", "name2", "name3"});
  subscription_->pause();

  expectSendMessage({"name4"}, {"name1", "name2"}, Grpc::Status::GrpcStatus::Ok, "", {});
  // If not for the pause, these updates would make the expectSendMessage fail due to too many
  // messages being sent.
  subscription_->updateResources({"name3", "name4"});
  subscription_->updateResources({"name1", "name2", "name3", "name4"});
  subscription_->updateResources({"name3", "name4"});
  subscription_->updateResources({"name1", "name2", "name3", "name4"});
  subscription_->updateResources({"name3", "name4"});

  subscription_->resume();
}

TEST_F(DeltaSubscriptionImplTest, ResponseCausesAck) {
  startSubscription({"name1"});
  deliverConfigUpdate({"name1"}, "someversion", true);
}

// Checks that after a pause(), no ACK requests are sent until resume(), but that after the
// resume, *all* ACKs that arrived during the pause are sent (in order).
TEST_F(DeltaSubscriptionImplTest, PauseQueuesAcks) {
  startSubscription({"name1", "name2", "name3"});
  subscription_->pause();
  // The server gives us our first version of resource name1.
  // subscription_ now wants to ACK name1 (but can't due to pause).
  {
    auto message = std::make_unique<envoy::api::v2::DeltaDiscoveryResponse>();
    auto* resource = message->mutable_resources()->Add();
    resource->set_name("name1");
    resource->set_version("version1A");
    const std::string nonce = std::to_string(HashUtil::xxHash64("version1A"));
    message->set_nonce(nonce);
    message->set_type_url(Config::TypeUrl::get().ClusterLoadAssignment);
    nonce_acks_required_.push(nonce);
    static_cast<GrpcDeltaXdsContext*>(subscription_->getContextForTest().get())
        ->onDiscoveryResponse(std::move(message));
  }
  // The server gives us our first version of resource name2.
  // subscription_ now wants to ACK name1 and then name2 (but can't due to pause).
  {
    auto message = std::make_unique<envoy::api::v2::DeltaDiscoveryResponse>();
    auto* resource = message->mutable_resources()->Add();
    resource->set_name("name2");
    resource->set_version("version2A");
    const std::string nonce = std::to_string(HashUtil::xxHash64("version2A"));
    message->set_nonce(nonce);
    message->set_type_url(Config::TypeUrl::get().ClusterLoadAssignment);
    nonce_acks_required_.push(nonce);
    static_cast<GrpcDeltaXdsContext*>(subscription_->getContextForTest().get())
        ->onDiscoveryResponse(std::move(message));
  }
  // The server gives us an updated version of resource name1.
  // subscription_ now wants to ACK name1A, then name2, then name1B (but can't due to pause).
  {
    auto message = std::make_unique<envoy::api::v2::DeltaDiscoveryResponse>();
    auto* resource = message->mutable_resources()->Add();
    resource->set_name("name1");
    resource->set_version("version1B");
    const std::string nonce = std::to_string(HashUtil::xxHash64("version1B"));
    message->set_nonce(nonce);
    message->set_type_url(Config::TypeUrl::get().ClusterLoadAssignment);
    nonce_acks_required_.push(nonce);
    static_cast<GrpcDeltaXdsContext*>(subscription_->getContextForTest().get())
        ->onDiscoveryResponse(std::move(message));
  }
  // All ACK sendMessage()s will happen upon calling resume().
  EXPECT_CALL(async_stream_, sendMessage(_, _))
      .WillRepeatedly([this](const Protobuf::Message& message, bool) {
        const std::string nonce =
            static_cast<const envoy::api::v2::DeltaDiscoveryRequest&>(message).response_nonce();
        if (!nonce.empty()) {
          nonce_acks_sent_.push(nonce);
        }
      });
  subscription_->resume();
  // DeltaSubscriptionTestHarness's dtor will check that all ACKs were sent with the correct nonces,
  // in the correct order.
}

TEST_F(DeltaSubscriptionImplTest, NoGrpcStream) {
  EXPECT_CALL(async_stream_, sendMessage(_, _)).Times(0);
  subscription_->start({"name1"});
  subscription_->updateResources({"name1", "name2"});
}

} // namespace
} // namespace Config
} // namespace Envoy
