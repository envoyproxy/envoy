#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/extensions/common/aws/aws_cluster_manager.h"

#include "test/mocks/server/server_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class AwsClusterManagerTest : public testing::Test {
public:
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(AwsClusterManagerTest, AddClusters) {
  auto aws_cluster_manager = std::make_unique<AwsClusterManager>(context_);
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

class MockAwsManagedClusterUpdateCallbacks : public AwsManagedClusterUpdateCallbacks {
public:
  MOCK_METHOD(void, onClusterAddOrUpdate, ());
};

class AwsClusterManagerFriend {
public:
  AwsClusterManagerFriend(std::shared_ptr<AwsClusterManager> aws_cluster_manager)
      : aws_cluster_manager_(aws_cluster_manager) {}

  void onClusterAddOrUpdate(absl::string_view cluster_name,
                            Upstream::ThreadLocalClusterCommand& command) {
    return aws_cluster_manager_->onClusterAddOrUpdate(cluster_name, command);
  }
  std::shared_ptr<AwsClusterManager> aws_cluster_manager_;
};

TEST_F(AwsClusterManagerTest, AddClusterCallbacks) {
  auto callbacks1 = std::make_shared<NiceMock<MockAwsManagedClusterUpdateCallbacks>>();
  EXPECT_CALL(*callbacks1, onClusterAddOrUpdate);
  auto callbacks2 = std::make_shared<NiceMock<MockAwsManagedClusterUpdateCallbacks>>();
  EXPECT_CALL(*callbacks2, onClusterAddOrUpdate);
  auto callbacks3 = std::make_shared<NiceMock<MockAwsManagedClusterUpdateCallbacks>>();
  EXPECT_CALL(*callbacks3, onClusterAddOrUpdate);
  auto aws_cluster_manager = std::make_shared<AwsClusterManager>(context_);
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

TEST_F(AwsClusterManagerTest, ClusterCallbacksAreDeleted) {
  auto callbacks1 = std::make_shared<NiceMock<MockAwsManagedClusterUpdateCallbacks>>();
  auto callbacks2 = std::make_shared<NiceMock<MockAwsManagedClusterUpdateCallbacks>>();
  auto callbacks3 = std::make_shared<NiceMock<MockAwsManagedClusterUpdateCallbacks>>();
  auto aws_cluster_manager = std::make_shared<AwsClusterManager>(context_);
  auto manager_friend = AwsClusterManagerFriend(aws_cluster_manager);

  auto status = aws_cluster_manager->addManagedCluster(
      "cluster_1",
      envoy::config::cluster::v3::Cluster::DiscoveryType::Cluster_DiscoveryType_STRICT_DNS,
      "new_url");
  auto handle1Or = aws_cluster_manager->addManagedClusterUpdateCallbacks("cluster_1", *callbacks1);
  auto handle2Or = aws_cluster_manager->addManagedClusterUpdateCallbacks("cluster_1", *callbacks2);
  auto handle3Or = aws_cluster_manager->addManagedClusterUpdateCallbacks("cluster_1", *callbacks3);
  handle1Or->release();
  handle2Or->release();
  handle3Or->release();
  auto command = Upstream::ThreadLocalClusterCommand();
  manager_friend.onClusterAddOrUpdate("cluster_1", command);
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
