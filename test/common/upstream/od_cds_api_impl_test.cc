#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/subscription.h"

#include "source/common/stats/isolated_store_impl.h"
#include "source/common/upstream/od_cds_api_impl.h"

#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/missing_cluster_notifier.h"

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
    OptRef<xds::core::v3::ResourceLocator> null_locator;
    odcds_ = OdCdsApiImpl::create(odcds_config, null_locator, cm_, notifier_, *store_.rootScope(),
                                  validation_visitor_);
    odcds_callbacks_ = cm_.subscription_factory_.callbacks_;
  }

  NiceMock<MockClusterManager> cm_;
  Stats::IsolatedStoreImpl store_;
  MockMissingClusterNotifier notifier_;
  OdCdsApiSharedPtr odcds_;
  Config::SubscriptionCallbacks* odcds_callbacks_ = nullptr;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
};

// Check that the subscription is started on the first (initial) request.
TEST_F(OdCdsApiImplTest, FirstUpdateStarts) {
  InSequence s;

  EXPECT_CALL(*cm_.subscription_factory_.subscription_, start(ElementsAre("fake_cluster")));
  odcds_->updateOnDemand("fake_cluster");
}

// Check that the cluster names are added to the awaiting list, when we still wait for the response
// for the initial request.
TEST_F(OdCdsApiImplTest, FollowingClusterNamesHitAwaitingList) {
  InSequence s;

  EXPECT_CALL(*cm_.subscription_factory_.subscription_, start(ElementsAre("fake_cluster")));
  EXPECT_CALL(*cm_.subscription_factory_.subscription_, requestOnDemandUpdate(_)).Times(0);
  odcds_->updateOnDemand("fake_cluster");
  odcds_->updateOnDemand("another_cluster");
}

// Check that the awaiting list is processed when we receive a successful response for the initial
// request.
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
  ASSERT_TRUE(odcds_callbacks_->onConfigUpdate(decoded_resources.refvec_, {}, "0").ok());
}

// Check that the awaiting list is processed when we receive a failure response for the initial
// request.
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

// Check that the awaiting list is processed only once, so on the first config update or config
// update failed.
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

// Check that we don't do extra request if there's nothing on the awaiting list.
TEST_F(OdCdsApiImplTest, NothingIsRequestedOnEmptyAwaitingList) {
  InSequence s;

  odcds_->updateOnDemand("fake_cluster");

  EXPECT_CALL(*cm_.subscription_factory_.subscription_, requestOnDemandUpdate(_)).Times(0);
  odcds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout,
                                         nullptr);
}

// Check that we send the requests for clusters after receiving the initial response instead of
// putting the names into the awaiting list.
TEST_F(OdCdsApiImplTest, OnDemandUpdateIsRequestedAfterInitialFetch) {
  InSequence s;

  odcds_->updateOnDemand("fake_cluster");
  envoy::config::cluster::v3::Cluster cluster;
  cluster.set_name("fake_cluster");
  const auto decoded_resources = TestUtility::decodeResources({cluster});
  ASSERT_TRUE(odcds_callbacks_->onConfigUpdate(decoded_resources.refvec_, {}, "0").ok());
  EXPECT_CALL(*cm_.subscription_factory_.subscription_,
              requestOnDemandUpdate(UnorderedElementsAre("another_cluster")));
  odcds_->updateOnDemand("another_cluster");
}

// Check that we report an error when we received a duplicated cluster.
TEST_F(OdCdsApiImplTest, ValidateDuplicateClusters) {
  InSequence s;

  envoy::config::cluster::v3::Cluster cluster_1;
  cluster_1.set_name("duplicate_cluster");
  const auto decoded_resources = TestUtility::decodeResources({cluster_1, cluster_1});

  ASSERT_EQ(odcds_callbacks_->onConfigUpdate(decoded_resources.refvec_, {}, "").message(),
            "Error adding/updating cluster(s) duplicate_cluster: duplicate cluster "
            "duplicate_cluster found");
}

// Check that notifier gets a message about potentially missing cluster.
TEST_F(OdCdsApiImplTest, NotifierGetsUsed) {
  InSequence s;

  odcds_->updateOnDemand("cluster");
  EXPECT_CALL(notifier_, notifyMissingCluster("missing_cluster"));
  std::vector<std::string> v{"missing_cluster"};
  Protobuf::RepeatedPtrField<std::string> removed(v.begin(), v.end());
  ASSERT_TRUE(odcds_callbacks_->onConfigUpdate({}, removed, "").ok());
}

// Check that notifier won't be used for a requested cluster that did
// not appear in the response.
TEST_F(OdCdsApiImplTest, NotifierNotUsed) {
  InSequence s;

  envoy::config::cluster::v3::Cluster cluster;
  cluster.set_name("some_cluster");
  const auto some_cluster_resource = TestUtility::decodeResources({cluster});
  cluster.set_name("some_cluster2");
  const auto some_cluster2_resource = TestUtility::decodeResources({cluster});

  std::vector<std::string> v{"another_cluster"};
  Protobuf::RepeatedPtrField<std::string> removed(v.begin(), v.end());
  std::vector<std::string> v2{"another_cluster2"};
  Protobuf::RepeatedPtrField<std::string> removed2(v2.begin(), v2.end());

  odcds_->updateOnDemand("cluster");
  EXPECT_CALL(notifier_, notifyMissingCluster(_)).Times(2);
  EXPECT_CALL(notifier_, notifyMissingCluster("cluster")).Times(0);
  ASSERT_TRUE(odcds_callbacks_->onConfigUpdate(some_cluster_resource.refvec_, {}, "").ok());
  ASSERT_TRUE(odcds_callbacks_->onConfigUpdate({}, removed, "").ok());
  ASSERT_TRUE(odcds_callbacks_->onConfigUpdate({}, {}, "").ok());
  ASSERT_TRUE(odcds_callbacks_->onConfigUpdate(some_cluster2_resource.refvec_, removed2, "").ok());
}

} // namespace
} // namespace Upstream
} // namespace Envoy
