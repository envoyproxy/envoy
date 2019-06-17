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
using testing::AnyNumber;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;
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
    resetCdsInitializedCb();

    EXPECT_CALL(*cm_.subscription_factory_.subscription_, start(_));
    cds_->initialize();
    cds_callbacks_ = cm_.subscription_factory_.callbacks_;
  }

  void resetCdsInitializedCb() {
    cds_->setInitializedCb([this]() -> void {
      initialized_.ready();
      cm_.finishClusterWarming();
    });
  }

  ClusterManager::ClusterInfoMap makeClusterMap(const std::vector<std::string>& clusters) {
    ClusterManager::ClusterInfoMap map;
    for (const auto& cluster : clusters) {
      map.emplace(cluster, cm_.thread_local_cluster_.cluster_);
    }
    return map;
  }

  class MockWarmingClusterManager : public MockClusterManager {
  public:
    explicit MockWarmingClusterManager(TimeSource& time_source) : MockClusterManager(time_source) {}

    MockWarmingClusterManager() {}

    void expectAdd(const std::string& cluster_name, const std::string& version = std::string("")) {
      EXPECT_CALL(*this, addOrUpdateCluster(WithName(cluster_name), version, _))
          .WillOnce(Return(true));
    }

    void expectAddToThrow(const std::string& cluster_name, const std::string& exception_msg) {
      EXPECT_CALL(*this, addOrUpdateCluster(WithName(cluster_name), _, _))
          .WillOnce(Throw(EnvoyException(exception_msg)));
    }

    void expectAddWithWarming(const std::string& cluster_name, const std::string& version,
                              bool immediately_warm_up = false) {
      EXPECT_CALL(*this, addOrUpdateCluster(_, version, _))
          .WillOnce(Invoke([this, cluster_name,
                            immediately_warm_up](const envoy::api::v2::Cluster& cluster,
                                                 const std::string&, auto warming_cb) -> bool {
            EXPECT_EQ(cluster_name, cluster.name());
            EXPECT_EQ(warming_cbs_.cend(), warming_cbs_.find(cluster.name()));
            warming_cbs_[cluster.name()] = warming_cb;
            warming_cb(cluster.name(), ClusterManager::ClusterWarmingState::Starting);
            if (immediately_warm_up) {
              warming_cbs_.erase(cluster.name());
              warming_cb(cluster.name(), ClusterManager::ClusterWarmingState::Finished);
            }
            return true;
          }));
    }

    void expectWarmingClusterCount(int times = 1) {
      EXPECT_CALL(*this, warmingClusterCount()).Times(times).WillRepeatedly(Invoke([this]() {
        return warming_cbs_.size();
      }));
    }

    void finishClusterWarming() {
      for (const auto& cluster : clusters_to_warm_up_) {
        EXPECT_NE(warming_cbs_.cend(), warming_cbs_.find(cluster));
        auto callback = warming_cbs_[cluster];
        warming_cbs_.erase(cluster);
        callback(cluster, ClusterManager::ClusterWarmingState::Finished);
      }
      clusters_to_warm_up_.clear();
    }

    void clustersToWarmUp(const std::vector<std::string>&& clusters) {
      clusters_to_warm_up_ = clusters;
    }

  private:
    std::map<std::string, ClusterManager::ClusterWarmingCallback> warming_cbs_;
    std::vector<std::string> clusters_to_warm_up_;
  };

  NiceMock<MockWarmingClusterManager> cm_;
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
  cm_.expectAdd("cluster1", "0");
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
  cm_.expectAdd("cluster_1");

  envoy::api::v2::Cluster cluster_2;
  cluster_2.set_name("cluster_2");
  clusters.Add()->PackFrom(cluster_2);
  cm_.expectAdd("cluster_2");

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
      cm_.expectAdd("cluster_1", "v1");
      auto* resource = resources.Add();
      resource->mutable_resource()->PackFrom(cluster);
      resource->set_name("cluster_1");
      resource->set_version("v1");
    }
    {
      envoy::api::v2::Cluster cluster;
      cluster.set_name("cluster_2");
      cm_.expectAdd("cluster_2", "v1");
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
      cm_.expectAdd("cluster_3", "v2");
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
  cm_.expectAddToThrow("cluster_1", "An exception");

  envoy::api::v2::Cluster cluster_2;
  cluster_2.set_name("cluster_2");
  clusters.Add()->PackFrom(cluster_2);
  cm_.expectAdd("cluster_2");

  envoy::api::v2::Cluster cluster_3;
  cluster_3.set_name("cluster_3");
  clusters.Add()->PackFrom(cluster_3);
  cm_.expectAddToThrow("cluster_3", "Another exception");

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
  cm_.expectAdd("cluster1", "0");
  cm_.expectAdd("cluster2", "0");
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
  cm_.expectAdd("cluster1", "1");
  cm_.expectAdd("cluster3", "1");
  EXPECT_CALL(cm_, removeCluster("cluster2"));
  cds_callbacks_->onConfigUpdate(response2.resources(), response2.version_info());

  EXPECT_EQ("1", cds_->versionInfo());
}

TEST_F(CdsApiImplTest, CdsPauseOnWarming) {
  EXPECT_CALL(cm_, clusters()).WillRepeatedly(Return(ClusterManager::ClusterInfoMap{}));
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

  // Two clusters updated, both warmed up.
  EXPECT_CALL(cm_.ads_mux_, pause(Config::TypeUrl::get().ClusterLoadAssignment));
  cm_.expectAddWithWarming("cluster1", "0");
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(cm_.ads_mux_, pause(Config::TypeUrl::get().Cluster));
  cm_.expectAddWithWarming("cluster2", "0");
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(initialized_, ready());
  cm_.expectWarmingClusterCount(2);
  EXPECT_CALL(cm_.ads_mux_, resume(Config::TypeUrl::get().Cluster));
  EXPECT_CALL(cm_.ads_mux_, resume(Config::TypeUrl::get().ClusterLoadAssignment));
  cm_.clustersToWarmUp({"cluster1", "cluster2"});
  cds_callbacks_->onConfigUpdate(response1.resources(), response1.version_info());

  // Two clusters updated, only one warmed up.
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

  EXPECT_CALL(cm_.ads_mux_, pause(Config::TypeUrl::get().ClusterLoadAssignment));
  cm_.expectAddWithWarming("cluster1", "1");
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(cm_.ads_mux_, pause(Config::TypeUrl::get().Cluster));
  cm_.expectAddWithWarming("cluster3", "1");
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(initialized_, ready());
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(cm_.ads_mux_, resume(Config::TypeUrl::get().ClusterLoadAssignment));
  resetCdsInitializedCb();
  cm_.clustersToWarmUp({"cluster1"});
  cds_callbacks_->onConfigUpdate(response2.resources(), response2.version_info());

  // One cluster updated and warmed up. Also finish warming up of the previously added cluster3.
  const std::string response3_yaml = R"EOF(
version_info: '2'
resources:
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster4
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
)EOF";
  auto response3 = TestUtility::parseYaml<envoy::api::v2::DiscoveryResponse>(response3_yaml);

  EXPECT_CALL(cm_.ads_mux_, pause(Config::TypeUrl::get().ClusterLoadAssignment));
  cm_.expectAddWithWarming("cluster4", "2");
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(initialized_, ready());
  cm_.expectWarmingClusterCount(2);
  EXPECT_CALL(cm_.ads_mux_, resume(Config::TypeUrl::get().Cluster));
  EXPECT_CALL(cm_.ads_mux_, resume(Config::TypeUrl::get().ClusterLoadAssignment));
  resetCdsInitializedCb();
  cm_.clustersToWarmUp({"cluster4", "cluster3"});
  cds_callbacks_->onConfigUpdate(response3.resources(), response3.version_info());

  const std::string response4_yaml = R"EOF(
version_info: '3'
resources:
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster5
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster6
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
)EOF";
  auto response4 = TestUtility::parseYaml<envoy::api::v2::DiscoveryResponse>(response4_yaml);

  // Two clusters updated, first one warmed up before processing of the second one starts.
  EXPECT_CALL(cm_.ads_mux_, pause(Config::TypeUrl::get().ClusterLoadAssignment));
  cm_.expectAddWithWarming("cluster5", "3", true);
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(cm_.ads_mux_, pause(Config::TypeUrl::get().Cluster));
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(cm_.ads_mux_, resume(Config::TypeUrl::get().Cluster));
  cm_.expectAddWithWarming("cluster6", "3");
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(cm_.ads_mux_, pause(Config::TypeUrl::get().Cluster));
  EXPECT_CALL(initialized_, ready());
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(cm_.ads_mux_, resume(Config::TypeUrl::get().Cluster));
  EXPECT_CALL(cm_.ads_mux_, resume(Config::TypeUrl::get().ClusterLoadAssignment));
  resetCdsInitializedCb();
  cm_.clustersToWarmUp({"cluster6"});
  cds_callbacks_->onConfigUpdate(response4.resources(), response4.version_info());
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
  cds_callbacks_->onConfigUpdateFailed({});
  EXPECT_EQ("", cds_->versionInfo());
}

} // namespace
} // namespace Upstream
} // namespace Envoy
