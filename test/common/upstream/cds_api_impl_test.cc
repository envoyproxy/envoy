#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/api/v2/core/config_source.pb.validate.h"

#include "common/config/utility.h"
#include "common/protobuf/utility.h"
#include "common/upstream/cds_api_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/upstream/mocks.h"
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
    envoy::api::v2::core::ConfigSource cds_config;
    cds_ = CdsApiImpl::create(cds_config, cm_, store_, validation_visitor_);
    cds_->setInitializedCb([this]() -> void { initialized_.ready(); });

    EXPECT_CALL(*cm_.subscription_factory_.subscription_, start(_));
    cds_->initialize();
    cds_callbacks_ = cm_.subscription_factory_.callbacks_;
  }

  void expectAdd(const std::string& cluster_name, const std::string& version = std::string("")) {
    EXPECT_CALL(cm_, addOrUpdateCluster(WithName(cluster_name), version)).WillOnce(Return(true));
  }

  void expectAddToThrow(const std::string& cluster_name, const std::string& exception_msg) {
    EXPECT_CALL(cm_, addOrUpdateCluster(WithName(cluster_name), _))
        .WillOnce(Throw(EnvoyException(exception_msg)));
  }

  ClusterManager::ClusterInfoMap makeClusterMap(const std::vector<std::string>& clusters) {
    ClusterManager::ClusterInfoMap map;
    for (const auto& cluster : clusters) {
      map.emplace(cluster, cm_.thread_local_cluster_.cluster_);
    }
    return map;
  }

  NiceMock<MockClusterManager> cm_;
  Upstream::ClusterManager::ClusterInfoMap cluster_map_;
  Upstream::MockClusterMockPrioritySet mock_cluster_;
  Stats::IsolatedStoreImpl store_;
  CdsApiPtr cds_;
  Config::SubscriptionCallbacks* cds_callbacks_{};
  ReadyWatcher initialized_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
};

// Negative test for protoc-gen-validate constraints.
TEST_F(CdsApiImplTest, ValidateFail) {
  InSequence s;

  setup();

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> clusters;
  envoy::api::v2::Cluster cluster;
  clusters.Add()->PackFrom(cluster);

  EXPECT_CALL(cm_, clusters()).WillRepeatedly(Return(cluster_map_));
  EXPECT_CALL(initialized_, ready());
  EXPECT_THROW(cds_callbacks_->onConfigUpdate(clusters, ""), EnvoyException);
}

// Regression test against only updating versionInfo() if at least one cluster
// is are added/updated even if one or more are removed.
TEST_F(CdsApiImplTest, UpdateVersionOnClusterRemove) {
  InSequence s;

  setup();

  const std::string response1_yaml = R"EOF(
version_info: '0'
resources:
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster1
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
)EOF";
  auto response1 = TestUtility::parseYaml<envoy::api::v2::DiscoveryResponse>(response1_yaml);

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(ClusterManager::ClusterInfoMap{}));
  expectAdd("cluster1", "0");
  EXPECT_CALL(initialized_, ready());
  EXPECT_EQ("", cds_->versionInfo());

  cds_callbacks_->onConfigUpdate(response1.resources(), response1.version_info());
  EXPECT_EQ("0", cds_->versionInfo());

  const std::string response2_yaml = R"EOF(
version_info: '1'
resources:
)EOF";
  auto response2 = TestUtility::parseYaml<envoy::api::v2::DiscoveryResponse>(response2_yaml);
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(makeClusterMap({"cluster1"})));
  EXPECT_CALL(cm_, removeCluster("cluster1")).WillOnce(Return(true));
  cds_callbacks_->onConfigUpdate(response2.resources(), response2.version_info());
  EXPECT_EQ("1", cds_->versionInfo());
}

// Validate onConfigUpdate throws EnvoyException with duplicate clusters.
TEST_F(CdsApiImplTest, ValidateDuplicateClusters) {
  InSequence s;

  setup();

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> clusters;
  envoy::api::v2::Cluster cluster_1;
  cluster_1.set_name("duplicate_cluster");
  clusters.Add()->PackFrom(cluster_1);
  clusters.Add()->PackFrom(cluster_1);

  EXPECT_CALL(cm_, clusters()).WillRepeatedly(Return(cluster_map_));
  EXPECT_CALL(initialized_, ready());
  EXPECT_THROW_WITH_MESSAGE(cds_callbacks_->onConfigUpdate(clusters, ""), EnvoyException,
                            "Error adding/updating cluster(s) duplicate_cluster: duplicate cluster "
                            "duplicate_cluster found");
}

TEST_F(CdsApiImplTest, EmptyConfigUpdate) {
  InSequence s;

  setup();

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(ClusterManager::ClusterInfoMap{}));
  EXPECT_CALL(initialized_, ready());

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> clusters;
  cds_callbacks_->onConfigUpdate(clusters, "");
}

TEST_F(CdsApiImplTest, ConfigUpdateWith2ValidClusters) {
  {
    InSequence s;
    setup();
  }

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(ClusterManager::ClusterInfoMap{}));
  EXPECT_CALL(initialized_, ready());

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> clusters;

  envoy::api::v2::Cluster cluster_1;
  cluster_1.set_name("cluster_1");
  clusters.Add()->PackFrom(cluster_1);
  expectAdd("cluster_1");

  envoy::api::v2::Cluster cluster_2;
  cluster_2.set_name("cluster_2");
  clusters.Add()->PackFrom(cluster_2);
  expectAdd("cluster_2");

  cds_callbacks_->onConfigUpdate(clusters, "");
}

TEST_F(CdsApiImplTest, DeltaConfigUpdate) {
  {
    InSequence s;
    setup();
  }
  EXPECT_CALL(initialized_, ready());

  {
    Protobuf::RepeatedPtrField<envoy::api::v2::Resource> resources;
    {
      envoy::api::v2::Cluster cluster;
      cluster.set_name("cluster_1");
      expectAdd("cluster_1", "v1");
      auto* resource = resources.Add();
      resource->mutable_resource()->PackFrom(cluster);
      resource->set_name("cluster_1");
      resource->set_version("v1");
    }
    {
      envoy::api::v2::Cluster cluster;
      cluster.set_name("cluster_2");
      expectAdd("cluster_2", "v1");
      auto* resource = resources.Add();
      resource->mutable_resource()->PackFrom(cluster);
      resource->set_name("cluster_2");
      resource->set_version("v1");
    }
    cds_callbacks_->onConfigUpdate(resources, {}, "v1");
  }

  {
    Protobuf::RepeatedPtrField<envoy::api::v2::Resource> resources;
    {
      envoy::api::v2::Cluster cluster;
      cluster.set_name("cluster_3");
      expectAdd("cluster_3", "v2");
      auto* resource = resources.Add();
      resource->mutable_resource()->PackFrom(cluster);
      resource->set_name("cluster_3");
      resource->set_version("v2");
    }
    Protobuf::RepeatedPtrField<std::string> removed;
    *removed.Add() = "cluster_1";
    EXPECT_CALL(cm_, removeCluster(StrEq("cluster_1"))).WillOnce(Return(true));
    cds_callbacks_->onConfigUpdate(resources, removed, "v2");
  }
}

TEST_F(CdsApiImplTest, ConfigUpdateAddsSecondClusterEvenIfFirstThrows) {
  {
    InSequence s;
    setup();
  }

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(ClusterManager::ClusterInfoMap{}));
  EXPECT_CALL(initialized_, ready());

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> clusters;

  envoy::api::v2::Cluster cluster_1;
  cluster_1.set_name("cluster_1");
  clusters.Add()->PackFrom(cluster_1);
  expectAddToThrow("cluster_1", "An exception");

  envoy::api::v2::Cluster cluster_2;
  cluster_2.set_name("cluster_2");
  clusters.Add()->PackFrom(cluster_2);
  expectAdd("cluster_2");

  envoy::api::v2::Cluster cluster_3;
  cluster_3.set_name("cluster_3");
  clusters.Add()->PackFrom(cluster_3);
  expectAddToThrow("cluster_3", "Another exception");

  EXPECT_THROW_WITH_MESSAGE(
      cds_callbacks_->onConfigUpdate(clusters, ""), EnvoyException,
      "Error adding/updating cluster(s) cluster_1: An exception, cluster_3: Another exception");
}

TEST_F(CdsApiImplTest, Basic) {
  InSequence s;

  setup();

  const std::string response1_yaml = R"EOF(
version_info: '0'
resources:
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster1
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster2
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
)EOF";
  auto response1 = TestUtility::parseYaml<envoy::api::v2::DiscoveryResponse>(response1_yaml);

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(ClusterManager::ClusterInfoMap{}));
  expectAdd("cluster1", "0");
  expectAdd("cluster2", "0");
  EXPECT_CALL(initialized_, ready());
  EXPECT_EQ("", cds_->versionInfo());
  cds_callbacks_->onConfigUpdate(response1.resources(), response1.version_info());
  EXPECT_EQ("0", cds_->versionInfo());

  const std::string response2_yaml = R"EOF(
version_info: '1'
resources:
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster1
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster3
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
)EOF";
  auto response2 = TestUtility::parseYaml<envoy::api::v2::DiscoveryResponse>(response2_yaml);

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(makeClusterMap({"cluster1", "cluster2"})));
  expectAdd("cluster1", "1");
  expectAdd("cluster3", "1");
  EXPECT_CALL(cm_, removeCluster("cluster2"));
  cds_callbacks_->onConfigUpdate(response2.resources(), response2.version_info());

  EXPECT_EQ("1", cds_->versionInfo());
}

// Validate behavior when the config is delivered but it fails PGV validation.
TEST_F(CdsApiImplTest, FailureInvalidConfig) {
  InSequence s;

  setup();

  const std::string response1_yaml = R"EOF(
version_info: '0'
resources:
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster1
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster1
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
)EOF";
  auto response1 = TestUtility::parseYaml<envoy::api::v2::DiscoveryResponse>(response1_yaml);

  EXPECT_CALL(cm_, clusters()).WillRepeatedly(Return(cluster_map_));
  EXPECT_CALL(initialized_, ready());
  EXPECT_THROW(cds_callbacks_->onConfigUpdate(response1.resources(), response1.version_info()),
               EnvoyException);
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
