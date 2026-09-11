#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/upstream/cds_api_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/test_common/printers.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::Envoy::StatusHelpers::IsOk;
using testing::_;
using testing::InSequence;
using ::testing::Not;
using testing::Return;
using testing::StrEq;
using testing::Throw;

namespace Envoy {
namespace Upstream {
namespace {

MATCHER_P(WithName, expectedName, "") { return arg.name() == expectedName; }

class CdsApiImplTest : public testing::Test {
protected:
  void setup(bool support_multi_ads_sources = false) {
    envoy::config::core::v3::ConfigSource cds_config;
    cds_ = *CdsApiImpl::create(cds_config, nullptr, cm_, *scope_.rootScope(), validation_visitor_,
                               server_factory_context_, support_multi_ads_sources);
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
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
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
  EXPECT_OK(cds_callbacks_->onConfigUpdate(decoded_resources.refvec_, response1.version_info()));
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
  EXPECT_OK(cds_callbacks_->onConfigUpdate(decoded_resources_2.refvec_, response2.version_info()));
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

  EXPECT_OK(cds_callbacks_->onConfigUpdate({}, ""));
  EXPECT_EQ(0UL, scope_.counter("cluster_manager.cds.config_reload").value());
  EXPECT_EQ(
      0UL,
      scope_.findGaugeByString("cluster_manager.cds.config_reload_time_ms").value().get().value());
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
  EXPECT_OK(cds_callbacks_->onConfigUpdate(decoded_resources.refvec_, ""));
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
      std::ignore = resource->mutable_resource()->PackFrom(cluster);
      resource->set_name("cluster_1");
      resource->set_version("v1");
    }
    {
      envoy::config::cluster::v3::Cluster cluster;
      cluster.set_name("cluster_2");
      expectAdd("cluster_2", "v1");
      auto* resource = resources.Add();
      std::ignore = resource->mutable_resource()->PackFrom(cluster);
      resource->set_name("cluster_2");
      resource->set_version("v1");
    }
    const auto decoded_resources =
        TestUtility::decodeResources<envoy::config::cluster::v3::Cluster>(resources);
    EXPECT_OK(cds_callbacks_->onConfigUpdate(decoded_resources.refvec_, {}, "v1"));
  }

  {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> resources;
    {
      envoy::config::cluster::v3::Cluster cluster;
      cluster.set_name("cluster_3");
      expectAdd("cluster_3", "v3");
      auto* resource = resources.Add();
      std::ignore = resource->mutable_resource()->PackFrom(cluster);
      resource->set_name("cluster_3");
      resource->set_version("v3");
    }
    Protobuf::RepeatedPtrField<std::string> removed;
    *removed.Add() = "cluster_1";
    EXPECT_CALL(cm_, removeCluster(StrEq("cluster_1"), false)).WillOnce(Return(true));
    const auto decoded_resources =
        TestUtility::decodeResources<envoy::config::cluster::v3::Cluster>(resources);
    EXPECT_OK(cds_callbacks_->onConfigUpdate(decoded_resources.refvec_, removed, "v2"));
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
  EXPECT_OK(cds_callbacks_->onConfigUpdate(decoded_resources.refvec_, response1.version_info()));
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
  EXPECT_OK(cds_callbacks_->onConfigUpdate(decoded_resources_2.refvec_, response2.version_info()));

  EXPECT_EQ("1", cds_->versionInfo());
  EXPECT_EQ(2UL, scope_.counter("cluster_manager.cds.config_reload").value());
  EXPECT_TRUE(
      scope_.findGaugeByString("cluster_manager.cds.config_reload_time_ms").value().get().value() >
      0UL);
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
  EXPECT_THAT(cds_callbacks_->onConfigUpdate(decoded_resources.refvec_, response1.version_info()),
              Not(IsOk()));
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

// Tests that when a SotW update happens, a cluster that was added by another
// source is not removed.
TEST_F(CdsApiImplTest, MultiAdsSourcesEnabledSotW) {
  InSequence s;
  setup(true);

  // 1. Initial SotW update introduces "sotw_cluster_1".
  const std::string response1_yaml = R"EOF(
version_info: '0'
resources:
- "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
  name: sotw_cluster_1
)EOF";
  auto response1 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response1_yaml);
  const auto decoded_resources1 =
      TestUtility::decodeResources<envoy::config::cluster::v3::Cluster>(response1);

  expectAdd("sotw_cluster_1", "0");
  EXPECT_CALL(initialized_, ready());
  EXPECT_OK(cds_callbacks_->onConfigUpdate(decoded_resources1.refvec_, response1.version_info()));
  EXPECT_EQ("0", cds_->versionInfo());

  // 2. A second SotW update removes "sotw_cluster_1" and adds "sotw_cluster_2".
  // We also imagine an on-demand cluster "od_cluster_1" now exists.
  const std::string response2_yaml = R"EOF(
version_info: '1'
resources:
- "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
  name: sotw_cluster_2
)EOF";
  auto response2 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response2_yaml);
  const auto decoded_resources2 =
      TestUtility::decodeResources<envoy::config::cluster::v3::Cluster>(response2);

  // The update should add the new cluster.
  expectAdd("sotw_cluster_2", "1");
  // Crucially, it should ONLY remove the cluster it knew about ("sotw_cluster_1").
  // "od_cluster_1" should NOT be removed.
  EXPECT_CALL(cm_, removeCluster("sotw_cluster_1", false));
  EXPECT_CALL(cm_, removeCluster("od_cluster_1", false)).Times(0);

  EXPECT_OK(cds_callbacks_->onConfigUpdate(decoded_resources2.refvec_, response2.version_info()));
  EXPECT_EQ("1", cds_->versionInfo());
}

// Tests that if a SotW update contains all the clusters it previously managed,
// no clusters are removed, even if other on-demand clusters exist.
TEST_F(CdsApiImplTest, MultiAdsSourcesEnabledNoRemoval) {
  InSequence s;
  setup(true);

  // 1. Initial SotW update introduces "sotw_cluster_1".
  const std::string response1_yaml = R"EOF(
version_info: '0'
resources:
- "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
  name: sotw_cluster_1
)EOF";
  auto response1 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response1_yaml);
  const auto decoded_resources1 =
      TestUtility::decodeResources<envoy::config::cluster::v3::Cluster>(response1);

  expectAdd("sotw_cluster_1", "0");
  EXPECT_CALL(initialized_, ready());
  EXPECT_OK(cds_callbacks_->onConfigUpdate(decoded_resources1.refvec_, response1.version_info()));

  // 2. A second SotW update still contains "sotw_cluster_1".
  // An on-demand cluster "od_cluster_1" has also been added.
  const std::string response2_yaml = R"EOF(
version_info: '1'
resources:
- "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
  name: sotw_cluster_1
)EOF";
  auto response2 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response2_yaml);
  const auto decoded_resources2 =
      TestUtility::decodeResources<envoy::config::cluster::v3::Cluster>(response2);

  // The existing cluster is updated.
  expectAdd("sotw_cluster_1", "1");
  // No clusters should be removed.
  EXPECT_CALL(cm_, removeCluster(_, false)).Times(0);

  EXPECT_OK(cds_callbacks_->onConfigUpdate(decoded_resources2.refvec_, response2.version_info()));
  EXPECT_EQ("1", cds_->versionInfo());
}

// Verifies that CdsApiHelper requests an RAII ClusterUpdateBatch from ClusterManager
// when processing a CDS config update, batching all cluster additions/updates in the response.
TEST_F(CdsApiImplTest, BatchClusterUpdatesOnCds) {
  setup();

  const auto decoded_resources = TestUtility::decodeResources(
      {defaultStaticCluster("cluster_1"), defaultStaticCluster("cluster_2")});
  EXPECT_CALL(cm_, createSourceBatch()).WillOnce(Return(nullptr));
  expectAdd("cluster_1");
  expectAdd("cluster_2");
  EXPECT_CALL(initialized_, ready());

  EXPECT_OK(cds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "1"));
}

// Verifies that CdsApiHelper correctly records rejection error messages when addOrUpdateCluster
// fails or when duplicate clusters are present in the update response, all within a batch scope.
TEST_F(CdsApiImplTest, CdsApiHelperRejectionReportingAndDuplicateHandling) {
  setup();

  const auto decoded_resources = TestUtility::decodeResources(
      {defaultStaticCluster("duplicate_cluster"), defaultStaticCluster("duplicate_cluster"),
       defaultStaticCluster("failing_cluster")});

  EXPECT_CALL(cm_, createSourceBatch()).WillOnce(Return(nullptr));
  expectAdd("duplicate_cluster");
  EXPECT_CALL(cm_, addOrUpdateCluster(WithName("failing_cluster"), "", false))
      .WillOnce(Return(absl::InvalidArgumentError("invalid cluster config")));

  EXPECT_CALL(initialized_, ready());

  const auto status = cds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "1");
  EXPECT_THAT(status, StatusHelpers::StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(status.message(), testing::HasSubstr("duplicate cluster duplicate_cluster found"));
  EXPECT_THAT(status.message(), testing::HasSubstr("failing_cluster: invalid cluster config"));
}

// Verifies that CDS updates properly batch interleaved additions, updates, and removals
// under an RAII batch scope.
TEST_F(CdsApiImplTest, BatchInterleavedAddUpdateRemove) {
  setup();

  // First establish an active cluster to remove in the subsequent update.
  const auto decoded_resources1 =
      TestUtility::decodeResources({defaultStaticCluster("cluster_to_remove")});
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(makeClusterInfoMaps({})));
  EXPECT_CALL(cm_, createSourceBatch()).WillOnce(Return(nullptr));
  expectAdd("cluster_to_remove");
  EXPECT_CALL(initialized_, ready());

  EXPECT_OK(cds_callbacks_->onConfigUpdate(decoded_resources1.refvec_, "1"));

  // Second update: simultaneously add cluster_new and remove cluster_to_remove.
  const auto decoded_resources2 =
      TestUtility::decodeResources({defaultStaticCluster("cluster_new")});
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(makeClusterInfoMaps({"cluster_to_remove"})));
  EXPECT_CALL(cm_, createSourceBatch()).WillOnce(Return(nullptr));
  expectAdd("cluster_new");
  EXPECT_CALL(cm_, removeCluster(StrEq("cluster_to_remove"), false)).WillOnce(Return(true));

  EXPECT_OK(cds_callbacks_->onConfigUpdate(decoded_resources2.refvec_, "2"));
  EXPECT_EQ("2", cds_->versionInfo());
}

// Verifies that CdsApiHelper catches EnvoyException when adding/updating clusters in CDS responses,
// records the rejection reason, and returns an InvalidArgument error while maintaining the batch
// scope.
TEST_F(CdsApiImplTest, CdsApiHelperExceptionHandling) {
  setup();

  const auto decoded_resources =
      TestUtility::decodeResources({defaultStaticCluster("exception_cluster")});
  EXPECT_CALL(cm_, createSourceBatch()).WillOnce(Return(nullptr));
  expectAddToThrow("exception_cluster", "syntax error in cluster configuration");
  EXPECT_CALL(initialized_, ready());

  const auto status = cds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "1");
  EXPECT_THAT(status, StatusHelpers::StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(status.message(),
              testing::HasSubstr("exception_cluster: syntax error in cluster configuration"));
}

// Verifies that an empty CDS response under an active batch executes cleanly without errors.
TEST_F(CdsApiImplTest, BatchEmptyCdsResponse) {
  setup();

  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::cluster::v3::Cluster>({});
  EXPECT_CALL(cm_, clusters()).WillOnce(Return(makeClusterInfoMaps({})));
  EXPECT_CALL(cm_, createSourceBatch()).WillOnce(Return(nullptr));
  EXPECT_CALL(initialized_, ready());

  EXPECT_OK(cds_callbacks_->onConfigUpdate(decoded_resources.refvec_, "1"));
  EXPECT_EQ("", cds_->versionInfo());
  EXPECT_EQ(0UL, scope_.counter("cluster_manager.cds.config_reload").value());
}

} // namespace
} // namespace Upstream
} // namespace Envoy
