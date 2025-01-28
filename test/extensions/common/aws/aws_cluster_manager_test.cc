#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/extensions/common/aws/aws_cluster_manager.h"

#include "test/mocks/server/server_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class AwsClusterManagerTest : public testing::Test {
public:
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Init::TargetHandlePtr init_target_;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher_;
  NiceMock<Upstream::MockClusterManager> cm_;
};

class MockAwsManagedClusterUpdateCallbacks : public AwsManagedClusterUpdateCallbacks {
public:
  MOCK_METHOD(void, onClusterAddOrUpdate, ());
};

class AwsClusterManagerFriend {
public:
  AwsClusterManagerFriend(std::shared_ptr<AwsClusterManagerImpl> aws_cluster_manager)
      : aws_cluster_manager_(aws_cluster_manager) {}

  void onClusterAddOrUpdate(absl::string_view cluster_name,
                            Upstream::ThreadLocalClusterCommand& command) {
    return aws_cluster_manager_->onClusterAddOrUpdate(cluster_name, command);
  }

  void onClusterRemoval(const std::string& cluster_name) {
    return aws_cluster_manager_->onClusterRemoval(cluster_name);
  }

  std::shared_ptr<AwsClusterManagerImpl> aws_cluster_manager_;
};

// Checks that we can add clusters to the cluster manager and that they are de-duped correctly
TEST_F(AwsClusterManagerTest, AddClusters) {
  auto aws_cluster_manager = std::make_unique<AwsClusterManagerImpl>(context_);
  auto status = aws_cluster_manager->addManagedCluster(
      "cluster_1",
      envoy::config::cluster::v3::Cluster::DiscoveryType::Cluster_DiscoveryType_STRICT_DNS,
      "uri_1");
  status = aws_cluster_manager->addManagedCluster(
      "cluster_2", envoy::config::cluster::v3::Cluster::DiscoveryType::Cluster_DiscoveryType_STATIC,
      "uri_2");
  status = aws_cluster_manager->addManagedCluster(
      "cluster_3",
      envoy::config::cluster::v3::Cluster::DiscoveryType::Cluster_DiscoveryType_STRICT_DNS,
      "uri_3");
  EXPECT_TRUE(aws_cluster_manager->getUriFromClusterName("cluster_1").ok());
  EXPECT_EQ(aws_cluster_manager->getUriFromClusterName("cluster_1").value(), "uri_1");
  EXPECT_TRUE(aws_cluster_manager->getUriFromClusterName("cluster_2").ok());
  EXPECT_EQ(aws_cluster_manager->getUriFromClusterName("cluster_2").value(), "uri_2");
  EXPECT_TRUE(aws_cluster_manager->getUriFromClusterName("cluster_3").ok());
  EXPECT_EQ(aws_cluster_manager->getUriFromClusterName("cluster_3").value(), "uri_3");
  // Adding an extra with the same cluster name has no effect
  status = aws_cluster_manager->addManagedCluster(
      "cluster_1",
      envoy::config::cluster::v3::Cluster::DiscoveryType::Cluster_DiscoveryType_STRICT_DNS,
      "new_url");
  EXPECT_EQ(absl::StatusCode::kAlreadyExists, status.code());
  EXPECT_EQ(aws_cluster_manager->getUriFromClusterName("cluster_1").value(), "uri_1");
}

TEST_F(AwsClusterManagerTest, CantGetUriForNonExistentCluster) {
  auto aws_cluster_manager = std::make_unique<AwsClusterManagerImpl>(context_);
  auto status = aws_cluster_manager->getUriFromClusterName("cluster_1");
  EXPECT_EQ(status.status().code(), absl::StatusCode::kInvalidArgument);
}

// Checks that the cluster callbacks are called
TEST_F(AwsClusterManagerTest, AddClusterCallbacks) {
  auto callbacks1 = std::make_shared<NiceMock<MockAwsManagedClusterUpdateCallbacks>>();
  EXPECT_CALL(*callbacks1, onClusterAddOrUpdate);
  auto callbacks2 = std::make_shared<NiceMock<MockAwsManagedClusterUpdateCallbacks>>();
  EXPECT_CALL(*callbacks2, onClusterAddOrUpdate);
  auto callbacks3 = std::make_shared<NiceMock<MockAwsManagedClusterUpdateCallbacks>>();
  EXPECT_CALL(*callbacks3, onClusterAddOrUpdate);
  auto aws_cluster_manager = std::make_shared<AwsClusterManagerImpl>(context_);
  auto manager_friend = AwsClusterManagerFriend(aws_cluster_manager);

  auto status = aws_cluster_manager->addManagedCluster(
      "cluster_1",
      envoy::config::cluster::v3::Cluster::DiscoveryType::Cluster_DiscoveryType_STRICT_DNS,
      "new_url");
  auto handle1Or = aws_cluster_manager->addManagedClusterUpdateCallbacks("cluster_1", *callbacks1);
  auto handle2Or = aws_cluster_manager->addManagedClusterUpdateCallbacks("cluster_1", *callbacks2);
  auto handle3Or = aws_cluster_manager->addManagedClusterUpdateCallbacks("cluster_1", *callbacks3);
  auto command = Upstream::ThreadLocalClusterCommand();
  manager_friend.onClusterAddOrUpdate("cluster_1", command);
}

// Checks that RAII cleans up callbacks correctly
TEST_F(AwsClusterManagerTest, ClusterCallbacksAreDeleted) {
  auto callbacks1 = std::make_unique<NiceMock<MockAwsManagedClusterUpdateCallbacks>>();
  EXPECT_CALL(*callbacks1, onClusterAddOrUpdate).Times(0);
  auto callbacks2 = std::make_unique<NiceMock<MockAwsManagedClusterUpdateCallbacks>>();
  EXPECT_CALL(*callbacks2, onClusterAddOrUpdate).Times(0);
  auto callbacks3 = std::make_unique<NiceMock<MockAwsManagedClusterUpdateCallbacks>>();
  EXPECT_CALL(*callbacks3, onClusterAddOrUpdate).Times(0);
  auto aws_cluster_manager = std::make_shared<AwsClusterManagerImpl>(context_);
  auto manager_friend = AwsClusterManagerFriend(aws_cluster_manager);

  auto status = aws_cluster_manager->addManagedCluster(
      "cluster_1",
      envoy::config::cluster::v3::Cluster::DiscoveryType::Cluster_DiscoveryType_STRICT_DNS,
      "new_url");
  auto handle1Or = aws_cluster_manager->addManagedClusterUpdateCallbacks("cluster_1", *callbacks1);
  auto handle2Or = aws_cluster_manager->addManagedClusterUpdateCallbacks("cluster_1", *callbacks2);
  auto handle3Or = aws_cluster_manager->addManagedClusterUpdateCallbacks("cluster_1", *callbacks3);
  // Delete the handles, which should clean up the callback list via RAII
  handle1Or->reset();
  handle2Or->reset();
  handle3Or->reset();
  auto command = Upstream::ThreadLocalClusterCommand();
  manager_friend.onClusterAddOrUpdate("cluster_1", command);
}

// Checks that aws cluster manager constructor adds an init target and the target is called
TEST_F(AwsClusterManagerTest, CreateQueuedViaInitManager) {
  EXPECT_CALL(context_.init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
    init_target_ = target.createHandle("test");
  }));
  EXPECT_CALL(context_, clusterManager()).WillRepeatedly(ReturnRef(cm_));
  auto aws_cluster_manager = std::make_shared<AwsClusterManagerImpl>(context_);
  auto status = aws_cluster_manager->addManagedCluster(
      "cluster_1",
      envoy::config::cluster::v3::Cluster::DiscoveryType::Cluster_DiscoveryType_STRICT_DNS,
      "new_url");
  // Cluster creation should be queued at this point
  EXPECT_CALL(cm_, addOrUpdateCluster(_, _, _));

  init_target_->initialize(init_watcher_);
}

// Checks that aws cluster manager constructor adds an init target and the target is called
TEST_F(AwsClusterManagerTest, CreateQueuedViaInitManagerWithFailedCluster) {
  EXPECT_CALL(context_.init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
    init_target_ = target.createHandle("test");
  }));
  EXPECT_CALL(context_, clusterManager()).WillRepeatedly(ReturnRef(cm_));
  EXPECT_CALL(cm_, addOrUpdateCluster(_, _, _)).WillOnce(Return(absl::InternalError("")));

  auto aws_cluster_manager = std::make_shared<AwsClusterManagerImpl>(context_);
  auto status = aws_cluster_manager->addManagedCluster(
      "cluster_1",
      envoy::config::cluster::v3::Cluster::DiscoveryType::Cluster_DiscoveryType_STRICT_DNS,
      "new_url");
  // Cluster creation should be queued at this point
  init_target_->initialize(init_watcher_);
  EXPECT_FALSE(aws_cluster_manager->getUriFromClusterName("cluster_1").ok());
}

// Checks that aws cluster manager constructor does not add an init target if the init manager is
// already initialized
TEST_F(AwsClusterManagerTest, DontUseInitWhenInitialized) {
  EXPECT_CALL(context_.init_manager_, state())
      .WillRepeatedly(Return(Envoy::Init::Manager::State::Initialized));
  EXPECT_CALL(context_.init_manager_, add(_)).Times(0);
  EXPECT_CALL(context_, clusterManager()).WillRepeatedly(ReturnRef(cm_));
  auto aws_cluster_manager = std::make_shared<AwsClusterManagerImpl>(context_);
}

// Cluster callbacks should not be added for non-existent clusters
TEST_F(AwsClusterManagerTest, CantAddCallbacksForNonExistentCluster) {

  auto aws_cluster_manager = std::make_shared<AwsClusterManagerImpl>(context_);
  auto callbacks1 = std::make_unique<NiceMock<MockAwsManagedClusterUpdateCallbacks>>();
  auto status = aws_cluster_manager->addManagedClusterUpdateCallbacks("cluster_1", *callbacks1);
  EXPECT_EQ(absl::StatusCode::kInvalidArgument, status.status().code());
}

// If the cluster is online, then adding a callback should trigger the callback immediately
TEST_F(AwsClusterManagerTest, CallbacksTriggeredImmediatelyWhenClusterIsLive) {
  auto aws_cluster_manager = std::make_shared<AwsClusterManagerImpl>(context_);
  auto status = aws_cluster_manager->addManagedCluster(
      "cluster_1",
      envoy::config::cluster::v3::Cluster::DiscoveryType::Cluster_DiscoveryType_STRICT_DNS,
      "new_url");
  auto manager_friend = AwsClusterManagerFriend(aws_cluster_manager);
  auto command = Upstream::ThreadLocalClusterCommand();
  manager_friend.onClusterAddOrUpdate("cluster_1", command);
  auto callbacks1 = std::make_unique<NiceMock<MockAwsManagedClusterUpdateCallbacks>>();
  EXPECT_CALL(*callbacks1, onClusterAddOrUpdate);
  auto status1 = aws_cluster_manager->addManagedClusterUpdateCallbacks("cluster_1", *callbacks1);
}

// Cluster manager cannot add a cluster
TEST_F(AwsClusterManagerTest, ClusterManagerCannotAdd) {
  EXPECT_CALL(context_, clusterManager()).WillRepeatedly(ReturnRef(cm_));
  EXPECT_CALL(cm_, addOrUpdateCluster(_, _, _)).WillOnce(Return(absl::InternalError("")));
  EXPECT_CALL(context_.init_manager_, state())
      .WillRepeatedly(Return(Envoy::Init::Manager::State::Initialized));

  auto aws_cluster_manager = std::make_shared<AwsClusterManagerImpl>(context_);
  auto status = aws_cluster_manager->addManagedCluster(
      "cluster_1",
      envoy::config::cluster::v3::Cluster::DiscoveryType::Cluster_DiscoveryType_STRICT_DNS,
      "new_url");
  EXPECT_EQ(absl::StatusCode::kInternal, status.code());
  EXPECT_FALSE(aws_cluster_manager->getUriFromClusterName("cluster_1").ok());
}

// Noop test for coverage
TEST_F(AwsClusterManagerTest, OnClusterRemovalCoverage) {
  auto aws_cluster_manager = std::make_shared<AwsClusterManagerImpl>(context_);
  auto manager_friend = AwsClusterManagerFriend(aws_cluster_manager);
  manager_friend.onClusterRemoval("cluster_1");
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
