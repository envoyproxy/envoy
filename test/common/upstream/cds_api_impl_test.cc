#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/upstream/cds_api_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Return;
using testing::StrEq;
using testing::Throw;

namespace Envoy {
namespace Upstream {
namespace {

MATCHER_P(WithName, expectedName, "") { return arg.name() == expectedName; }

class CdsApiImplTest : public testing::Test {
protected:
  void setup() {
    envoy::config::core::v3::ConfigSource cds_config;
    cds_ = CdsApiImpl::create(cds_config, nullptr, cm_, *store_.rootScope(), validation_visitor_);
    cds_->setInitializedCb([this]() -> void { initialized_.ready(); });

    EXPECT_CALL(*cm_.subscription_factory_.subscription_, start(_));
    cds_->initialize();
    cds_callbacks_ = cm_.subscription_factory_.callbacks_;
  }

  void expectAdd(const std::string& cluster_name, const std::string& version = std::string("")) {
    EXPECT_CALL(cm_, addOrUpdateCluster(WithName(cluster_name), version, false))
        .WillOnce(Return(true));
  }

  void expectAddToThrow(const std::string& cluster_name, const std::string& exception_msg) {
    EXPECT_CALL(cm_, addOrUpdateCluster(WithName(cluster_name), _, false))
        .WillOnce(Throw(EnvoyException(exception_msg)));
  }

  ClusterManager::ClusterInfoMaps
  makeClusterInfoMaps(const std::vector<std::string>& active_clusters,
                      const std::vector<std::string>& warming_clusters = {}) {
    ClusterManager::ClusterInfoMaps maps;
    for (const auto& cluster : active_clusters) {
      maps.active_clusters_.emplace(cluster, cm_.thread_local_cluster_.cluster_);
    }
    for (const auto& cluster : warming_clusters) {
      maps.warming_clusters_.emplace(cluster, cm_.thread_local_cluster_.cluster_);
    }
    return maps;
  }

  NiceMock<MockClusterManager> cm_;
  Upstream::MockClusterMockPrioritySet mock_cluster_;
  Stats::IsolatedStoreImpl store_;
  CdsApiPtr cds_;
  Config::SubscriptionCallbacks* cds_callbacks_{};
  ReadyWatcher initialized_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
};

// Regression test against only updating versionInfo() if at least one cluster
// is are added/updated even if one or more are removed.
TEST_F(CdsApiImplTest, UpdateVersionOnClusterRemove) {
  InSequence s;

  setup();

  const std::string response1_yaml = R"EOF(
version_info: '0'
resources:
- "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
  name: cluster1
  type: EDS
  eds_cluster_config:
    eds_config:
      path_config_source:
        path: eds path
)EOF";
  auto response1 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response1_yaml);

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(makeClusterInfoMaps({})));
  expectAdd("cluster1", "0");
  EXPECT_CALL(initialized_, ready());
  EXPECT_EQ("", cds_->versionInfo());

  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::cluster::v3::Cluster>(response1);
  EXPECT_TRUE(
      cds_callbacks_->onConfigUpdate(decoded_resources.refvec_, response1.version_info()).ok());
  EXPECT_EQ("0", cds_->versionInfo());

  const std::string response2_yaml = R"EOF(
version_info: '1'
resources:
)EOF";
  auto response2 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response2_yaml);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(makeClusterInfoMaps({"cluster1"})));
  EXPECT_CALL(cm_, removeCluster("cluster1", false)).WillOnce(Return(true));
  const auto decoded_resources_2 =
      TestUtility::decodeResources<envoy::config::cluster::v3::Cluster>(response2);
  EXPECT_TRUE(
      cds_callbacks_->onConfigUpdate(decoded_resources_2.refvec_, response2.version_info()).ok());
  EXPECT_EQ("1", cds_->versionInfo());
}

// Validate onConfigUpdate throws EnvoyException with duplicate clusters.
TEST_F(CdsApiImplTest, ValidateDuplicateClusters) {
  InSequence s;

  setup();

  envoy::config::cluster::v3::Cluster cluster_1;
  cluster_1.set_name("duplicate_cluster");
  const auto decoded_resources = TestUtility::decodeResources({cluster_1, cluster_1});

  EXPECT_CALL(cm_, clusters()).WillRepeatedly(Return(makeClusterInfoMaps({})));
  EXPECT_CALL(initialized_, ready());
  EXPECT_EQ(cds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "").message(),
            "Error adding/updating cluster(s) duplicate_cluster: duplicate cluster "
            "duplicate_cluster found");
}

TEST_F(CdsApiImplTest, EmptyConfigUpdate) {
  InSequence s;

  setup();

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(makeClusterInfoMaps({})));
  EXPECT_CALL(initialized_, ready());

  EXPECT_TRUE(cds_callbacks_->onConfigUpdate({}, "").ok());
}

TEST_F(CdsApiImplTest, ConfigUpdateWith2ValidClusters) {
  {
    InSequence s;
    setup();
  }

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(makeClusterInfoMaps({})));
  EXPECT_CALL(initialized_, ready());

  envoy::config::cluster::v3::Cluster cluster_1;
  cluster_1.set_name("cluster_1");
  expectAdd("cluster_1");

  envoy::config::cluster::v3::Cluster cluster_2;
  cluster_2.set_name("cluster_2");
  expectAdd("cluster_2");

  const auto decoded_resources = TestUtility::decodeResources({cluster_1, cluster_2});
  EXPECT_TRUE(cds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "").ok());
}

TEST_F(CdsApiImplTest, DeltaConfigUpdate) {
  {
    InSequence s;
    setup();
  }
  EXPECT_CALL(initialized_, ready());

  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> resources;
    {
      envoy::config::cluster::v3::Cluster cluster;
      cluster.set_name("cluster_1");
      expectAdd("cluster_1", "v1");
      auto* resource = resources.Add();
      resource->mutable_resource()->PackFrom(cluster);
      resource->set_name("cluster_1");
      resource->set_version("v1");
    }
    {
      envoy::config::cluster::v3::Cluster cluster;
      cluster.set_name("cluster_2");
      expectAdd("cluster_2", "v1");
      auto* resource = resources.Add();
      resource->mutable_resource()->PackFrom(cluster);
      resource->set_name("cluster_2");
      resource->set_version("v1");
    }
    const auto decoded_resources =
        TestUtility::decodeResources<envoy::config::cluster::v3::Cluster>(resources);
    EXPECT_TRUE(cds_callbacks_->onConfigUpdate(decoded_resources.refvec_, {}, "v1").ok());
  }

  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> resources;
    {
      envoy::config::cluster::v3::Cluster cluster;
      cluster.set_name("cluster_3");
      expectAdd("cluster_3", "v3");
      auto* resource = resources.Add();
      resource->mutable_resource()->PackFrom(cluster);
      resource->set_name("cluster_3");
      resource->set_version("v3");
    }
    Protobuf::RepeatedPtrField<std::string> removed;
    *removed.Add() = "cluster_1";
    EXPECT_CALL(cm_, removeCluster(StrEq("cluster_1"), false)).WillOnce(Return(true));
    const auto decoded_resources =
        TestUtility::decodeResources<envoy::config::cluster::v3::Cluster>(resources);
    EXPECT_TRUE(cds_callbacks_->onConfigUpdate(decoded_resources.refvec_, removed, "v2").ok());
  }
}

TEST_F(CdsApiImplTest, ConfigUpdateAddsSecondClusterEvenIfFirstThrows) {
  {
    InSequence s;
    setup();
  }

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(makeClusterInfoMaps({})));
  EXPECT_CALL(initialized_, ready());

  envoy::config::cluster::v3::Cluster cluster_1;
  cluster_1.set_name("cluster_1");
  expectAddToThrow("cluster_1", "An exception");

  envoy::config::cluster::v3::Cluster cluster_2;
  cluster_2.set_name("cluster_2");
  expectAdd("cluster_2");

  envoy::config::cluster::v3::Cluster cluster_3;
  cluster_3.set_name("cluster_3");
  expectAddToThrow("cluster_3", "Another exception");

  const auto decoded_resources = TestUtility::decodeResources({cluster_1, cluster_2, cluster_3});
  EXPECT_EQ(
      cds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "").message(),
      "Error adding/updating cluster(s) cluster_1: An exception, cluster_3: Another exception");
}

TEST_F(CdsApiImplTest, Basic) {
  InSequence s;

  setup();

  const std::string response1_yaml = R"EOF(
version_info: '0'
resources:
- "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
  name: cluster1
  type: EDS
  eds_cluster_config:
    eds_config:
      path_config_source:
        path: eds path
- "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
  name: cluster2
  type: EDS
  eds_cluster_config:
    eds_config:
      path_config_source:
        path: eds path
)EOF";
  auto response1 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response1_yaml);

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(makeClusterInfoMaps({})));
  expectAdd("cluster1", "0");
  expectAdd("cluster2", "0");
  EXPECT_CALL(initialized_, ready());
  EXPECT_EQ("", cds_->versionInfo());
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::cluster::v3::Cluster>(response1);
  EXPECT_TRUE(
      cds_callbacks_->onConfigUpdate(decoded_resources.refvec_, response1.version_info()).ok());
  EXPECT_EQ("0", cds_->versionInfo());

  const std::string response2_yaml = R"EOF(
version_info: '1'
resources:
- "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
  name: cluster1
  type: EDS
  eds_cluster_config:
    eds_config:
      path_config_source:
        path: eds path
- "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
  name: cluster3
  type: EDS
  eds_cluster_config:
    eds_config:
      path_config_source:
        path: eds path
)EOF";
  auto response2 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response2_yaml);

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(makeClusterInfoMaps({"cluster1", "cluster2"})));
  expectAdd("cluster1", "1");
  expectAdd("cluster3", "1");
  EXPECT_CALL(cm_, removeCluster("cluster2", false));
  const auto decoded_resources_2 =
      TestUtility::decodeResources<envoy::config::cluster::v3::Cluster>(response2);
  EXPECT_TRUE(
      cds_callbacks_->onConfigUpdate(decoded_resources_2.refvec_, response2.version_info()).ok());

  EXPECT_EQ("1", cds_->versionInfo());
}

// Validate behavior when the config is delivered but it fails PGV validation.
TEST_F(CdsApiImplTest, FailureInvalidConfig) {
  InSequence s;

  setup();

  const std::string response1_yaml = R"EOF(
version_info: '0'
resources:
- "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
  name: cluster1
  type: EDS
  eds_cluster_config:
    eds_config:
      path_config_source:
        path: eds path
- "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
  name: cluster1
  type: EDS
  eds_cluster_config:
    eds_config:
      path_config_source:
        path: eds path
)EOF";
  auto response1 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response1_yaml);

  EXPECT_CALL(cm_, clusters()).WillRepeatedly(Return(makeClusterInfoMaps({})));
  EXPECT_CALL(initialized_, ready());
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::cluster::v3::Cluster>(response1);
  EXPECT_FALSE(
      cds_callbacks_->onConfigUpdate(decoded_resources.refvec_, response1.version_info()).ok());
  EXPECT_EQ("", cds_->versionInfo());
}

// Validate behavior when the config fails delivery at the subscription level.
TEST_F(CdsApiImplTest, FailureSubscription) {
  InSequence s;

  setup();

  EXPECT_CALL(initialized_, ready());
  // onConfigUpdateFailed() should not be called for gRPC stream connection failure
  cds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout, {});
  EXPECT_EQ("", cds_->versionInfo());
}

} // namespace
} // namespace Upstream
} // namespace Envoy
