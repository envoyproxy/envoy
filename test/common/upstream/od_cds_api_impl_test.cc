#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/subscription.h"

#include "common/stats/isolated_store_impl.h"
#include "common/upstream/od_cds_api_impl.h"

#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

using ::testing::ElementsAre;
using ::testing::InSequence;
using ::testing::UnorderedElementsAre;

class OdCdsApiImplTest : public testing::Test {
public:
  void SetUp() override {
    envoy::config::core::v3::ConfigSource odcds_config;
    odcds_ = OdCdsApiImpl::create(odcds_config, nullptr, cm_, store_, validation_visitor_);
    odcds_callbacks_ = cm_.subscription_factory_.callbacks_;
  }

  NiceMock<MockClusterManager> cm_;
  Stats::IsolatedStoreImpl store_;
  OdCdsApiPtr odcds_;
  Config::SubscriptionCallbacks* odcds_callbacks_ = nullptr;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
};

TEST_F(OdCdsApiImplTest, FirstUpdateStarts) {
  InSequence s;

  EXPECT_CALL(*cm_.subscription_factory_.subscription_, start(ElementsAre("fake_cluster")));
  odcds_->updateOnDemand("fake_cluster");
}

TEST_F(OdCdsApiImplTest, FollowingClusterNamesHitAwaitingList) {
  InSequence s;

  EXPECT_CALL(*cm_.subscription_factory_.subscription_, start(ElementsAre("fake_cluster")));
  EXPECT_CALL(*cm_.subscription_factory_.subscription_, requestOnDemandUpdate(_)).Times(0);
  odcds_->updateOnDemand("fake_cluster");
  odcds_->updateOnDemand("another_cluster");
}

TEST_F(OdCdsApiImplTest, AwaitingListIsProcessedOnConfigUpdate) {
  InSequence s;

  odcds_->updateOnDemand("fake_cluster");
  odcds_->updateOnDemand("another_cluster_1");
  odcds_->updateOnDemand("another_cluster_2");

  envoy::config::cluster::v3::Cluster cluster;
  cluster.set_name("fake_cluster");
  const auto decoded_resources = TestUtility::decodeResources({cluster});
  EXPECT_CALL(
      *cm_.subscription_factory_.subscription_,
      requestOnDemandUpdate(UnorderedElementsAre("another_cluster_1", "another_cluster_2")));
  odcds_callbacks_->onConfigUpdate(decoded_resources.refvec_, {}, "0");
}

TEST_F(OdCdsApiImplTest, AwaitingListIsProcessedOnConfigUpdateFailed) {
  InSequence s;

  odcds_->updateOnDemand("fake_cluster");
  odcds_->updateOnDemand("another_cluster_1");
  odcds_->updateOnDemand("another_cluster_2");

  EXPECT_CALL(
      *cm_.subscription_factory_.subscription_,
      requestOnDemandUpdate(UnorderedElementsAre("another_cluster_1", "another_cluster_2")));
  odcds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout,
                                         nullptr);
}

TEST_F(OdCdsApiImplTest, AwaitingListIsProcessedOnceOnly) {
  InSequence s;

  odcds_->updateOnDemand("fake_cluster");
  odcds_->updateOnDemand("another_cluster_1");
  odcds_->updateOnDemand("another_cluster_2");

  EXPECT_CALL(
      *cm_.subscription_factory_.subscription_,
      requestOnDemandUpdate(UnorderedElementsAre("another_cluster_1", "another_cluster_2")));
  odcds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout,
                                         nullptr);
  odcds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout,
                                         nullptr);
}

TEST_F(OdCdsApiImplTest, NothingIsRequestedOnEmptyAwaitingList) {
  InSequence s;

  odcds_->updateOnDemand("fake_cluster");

  EXPECT_CALL(*cm_.subscription_factory_.subscription_, requestOnDemandUpdate(_)).Times(0);
  odcds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout,
                                         nullptr);
}

TEST_F(OdCdsApiImplTest, OnDemandUpdateIsRequestedAfterInitialFetch) {
  InSequence s;

  odcds_->updateOnDemand("fake_cluster");
  envoy::config::cluster::v3::Cluster cluster;
  cluster.set_name("fake_cluster");
  const auto decoded_resources = TestUtility::decodeResources({cluster});
  odcds_callbacks_->onConfigUpdate(decoded_resources.refvec_, {}, "0");
  EXPECT_CALL(*cm_.subscription_factory_.subscription_,
              requestOnDemandUpdate(UnorderedElementsAre("another_cluster")));
  odcds_->updateOnDemand("another_cluster");
}

TEST_F(OdCdsApiImplTest, ValidateDuplicateClusters) {
  InSequence s;

  envoy::config::cluster::v3::Cluster cluster_1;
  cluster_1.set_name("duplicate_cluster");
  const auto decoded_resources = TestUtility::decodeResources({cluster_1, cluster_1});

  EXPECT_THROW_WITH_MESSAGE(odcds_callbacks_->onConfigUpdate(decoded_resources.refvec_, {}, ""),
                            EnvoyException,
                            "Error adding/updating cluster(s) duplicate_cluster: duplicate cluster "
                            "duplicate_cluster found");
}

} // namespace
} // namespace Upstream
} // namespace Envoy
