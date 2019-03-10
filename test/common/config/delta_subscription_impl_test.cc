#include "common/config/delta_subscription_impl.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Config {
namespace {

typedef DeltaSubscriptionImpl<envoy::api::v2::ClusterLoadAssignment> DeltaEdsSubscriptionImpl;

class DeltaSubscriptionImplTest : public testing::Test {
public:
  DeltaSubscriptionImplTest()
      : stats_(Utility::generateStats(stats_store_)),
        method_descriptor_(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.api.v2.EndpointDiscoveryService.StreamEndpoints")),
        async_client_(new Grpc::MockAsyncClient()) {}

  void createSubscription(std::chrono::milliseconds init_fetch_timeout) {
    EXPECT_CALL(dispatcher_, createTimer_(_));
    subscription_ = std::make_unique<DeltaEdsSubscriptionImpl>(
        local_info_, std::unique_ptr<Grpc::MockAsyncClient>(async_client_), dispatcher_,
        *method_descriptor_, random_, stats_store_, rate_limit_settings_, stats_,
        init_fetch_timeout);
  }

  void startSubscription(const std::vector<std::string>& cluster_names) {
    EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
    EXPECT_CALL(async_stream_, sendMessage(_, _)).Times(2);
    subscription_->start(cluster_names, callbacks_);
  }

  Stats::IsolatedStoreImpl stats_store_;
  SubscriptionStats stats_;
  const Protobuf::MethodDescriptor* method_descriptor_;
  Grpc::MockAsyncClient* async_client_;
  Event::MockDispatcher dispatcher_;
  Runtime::MockRandomGenerator random_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Grpc::MockAsyncStream async_stream_;
  std::unique_ptr<DeltaEdsSubscriptionImpl> subscription_;
  Envoy::Config::RateLimitSettings rate_limit_settings_;
  Event::MockTimer* init_timeout_timer_;
  NiceMock<Config::MockSubscriptionCallbacks<envoy::api::v2::ClusterLoadAssignment>> callbacks_;
};

// Validate that initial fetch timer is created and calls callback on timeout
TEST_F(DeltaSubscriptionImplTest, InitialFetchTimeout) {
  InSequence s;
  createSubscription(std::chrono::milliseconds(1000));

  init_timeout_timer_ = new Event::MockTimer(&dispatcher_);

  EXPECT_CALL(*init_timeout_timer_, enableTimer(std::chrono::milliseconds(1000)));
  startSubscription({"cluster1"});

  EXPECT_CALL(callbacks_, onConfigUpdateFailed(nullptr));
  init_timeout_timer_->callback_();
}

// Validate that initial fetch timer is not created
TEST_F(DeltaSubscriptionImplTest, NoInitialFetchTimeout) {
  InSequence s;
  createSubscription(std::chrono::milliseconds(0));

  EXPECT_CALL(dispatcher_, createTimer_(_)).Times(0);
  startSubscription({"cluster1"});
}

// Validate that initial fetch timer is disabled on config update
TEST_F(DeltaSubscriptionImplTest, DisableInitTimeoutOnSuccess) {
  InSequence s;
  createSubscription(std::chrono::milliseconds(1000));

  init_timeout_timer_ = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*init_timeout_timer_, enableTimer(std::chrono::milliseconds(1000)));
  startSubscription({"cluster1"});

  EXPECT_CALL(*init_timeout_timer_, disableTimer()).Times(1);
  auto response_ = envoy::api::v2::DeltaDiscoveryResponse();
  subscription_->onConfigUpdate(response_.resources(), response_.removed_resources(), "");
}

// Validate that initial fetch timer is disabled on config update failed
TEST_F(DeltaSubscriptionImplTest, DisableInitTimeoutOnFail) {
  InSequence s;
  createSubscription(std::chrono::milliseconds(1000));

  init_timeout_timer_ = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*init_timeout_timer_, enableTimer(std::chrono::milliseconds(1000)));
  startSubscription({"cluster1"});

  EXPECT_CALL(*init_timeout_timer_, disableTimer()).Times(1);
  subscription_->handleEstablishmentFailure();
}

} // namespace
} // namespace Config
} // namespace Envoy