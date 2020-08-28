#include "common/upstream/cluster_update_tracker.h"

#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/thread_local_cluster.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Upstream {
namespace {

class ClusterUpdateTrackerTest : public testing::Test {
public:
  ClusterUpdateTrackerTest() {
    expected_.cluster_.info_->name_ = cluster_name_;
    irrelevant_.cluster_.info_->name_ = "unrelated_cluster";
  }

  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Upstream::MockThreadLocalCluster> expected_;
  NiceMock<Upstream::MockThreadLocalCluster> irrelevant_;
  const std::string cluster_name_{"fake_cluster"};
};

TEST_F(ClusterUpdateTrackerTest, ClusterDoesNotExistAtConstructionTime) {
  EXPECT_CALL(cm_, get(cluster_name_)).WillOnce(Return(nullptr));

  ClusterUpdateTracker cluster_tracker(cm_, cluster_name_);

  EXPECT_FALSE(cluster_tracker.exists());
  EXPECT_EQ(cluster_tracker.info(), nullptr);
}

TEST_F(ClusterUpdateTrackerTest, ClusterDoesExistAtConstructionTime) {
  EXPECT_CALL(cm_, get(cluster_name_)).WillOnce(Return(&expected_));

  ClusterUpdateTracker cluster_tracker(cm_, cluster_name_);

  EXPECT_TRUE(cluster_tracker.exists());
  EXPECT_EQ(cluster_tracker.info(), expected_.cluster_.info_);
}

TEST_F(ClusterUpdateTrackerTest, ShouldProperlyHandleUpdateCallbacks) {
  EXPECT_CALL(cm_, get(cluster_name_)).WillOnce(Return(nullptr));

  ClusterUpdateTracker cluster_tracker(cm_, cluster_name_);

  {
    EXPECT_FALSE(cluster_tracker.exists());
    EXPECT_EQ(cluster_tracker.info(), nullptr);
  }

  {
    // Simulate addition of an irrelevant cluster.
    cluster_tracker.onClusterAddOrUpdate(irrelevant_);

    EXPECT_FALSE(cluster_tracker.exists());
    EXPECT_EQ(cluster_tracker.info(), nullptr);
  }

  {
    // Simulate addition of the relevant cluster.
    cluster_tracker.onClusterAddOrUpdate(expected_);

    EXPECT_TRUE(cluster_tracker.exists());
    EXPECT_EQ(cluster_tracker.info(), expected_.cluster_.info_);
  }

  {
    // Simulate removal of an irrelevant cluster.
    cluster_tracker.onClusterRemoval(irrelevant_.cluster_.info_->name_);

    EXPECT_TRUE(cluster_tracker.exists());
    EXPECT_EQ(cluster_tracker.info(), expected_.cluster_.info_);
  }

  {
    // Simulate removal of the relevant cluster.
    cluster_tracker.onClusterRemoval(cluster_name_);

    EXPECT_FALSE(cluster_tracker.exists());
    EXPECT_EQ(cluster_tracker.info(), nullptr);
  }
}

} // namespace
} // namespace Upstream
} // namespace Envoy
