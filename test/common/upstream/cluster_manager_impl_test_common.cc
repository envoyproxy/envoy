#include "test/common/upstream/cluster_manager_impl_test_common.h"

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/config/null_grpc_mux_impl.h"

#include "test/common/upstream/test_cluster_manager.h"

namespace Envoy {
namespace Upstream {

using ::testing::Return;

Bootstrap parseBootstrapFromV3Yaml(const std::string& yaml) {
  Bootstrap bootstrap;
  TestUtility::loadFromYaml(yaml, bootstrap);
  return bootstrap;
}

Bootstrap defaultConfig() {
  const std::string yaml = R"EOF(
static_resources:
  clusters: []
  )EOF";

  return parseBootstrapFromV3Yaml(yaml);
}

std::string clustersJson(const std::vector<std::string>& clusters) {
  return fmt::sprintf("\"clusters\": [%s]", absl::StrJoin(clusters, ","));
}

// Override postThreadLocalClusterUpdate so we can test that merged updates calls
// it with the right values at the right times.
class MockedUpdatedClusterManagerImpl : public TestClusterManagerImpl {
public:
  using TestClusterManagerImpl::TestClusterManagerImpl;

  MockedUpdatedClusterManagerImpl(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                                  ClusterManagerFactory& factory,
                                  Server::Configuration::ServerFactoryContext& factory_context,
                                  MockLocalClusterUpdate& local_cluster_update,
                                  MockLocalHostsRemoved& local_hosts_removed,
                                  absl::Status& creation_status)
      : TestClusterManagerImpl(bootstrap, factory, factory_context, creation_status),
        local_cluster_update_(local_cluster_update), local_hosts_removed_(local_hosts_removed) {}

protected:
  void postThreadLocalClusterUpdate(ClusterManagerCluster&,
                                    ThreadLocalClusterUpdateParams&& params) override {
    for (const auto& per_priority : params.per_priority_update_params_) {
      local_cluster_update_.post(per_priority.priority_, per_priority.hosts_added_,
                                 per_priority.hosts_removed_);
    }
  }

  void postThreadLocalRemoveHosts(const Cluster&, const HostVector& hosts_removed) override {
    local_hosts_removed_.post(hosts_removed);
  }

  MockLocalClusterUpdate& local_cluster_update_;
  MockLocalHostsRemoved& local_hosts_removed_;
};

ClusterManagerImplTest::ClusterManagerImplTest() : registered_dns_factory_(dns_resolver_factory_) {
  // Using the NullGrpcMuxImpl by default making the calls a no-op.
  ON_CALL(factory_.server_context_.xds_manager_, adsMux())
      .WillByDefault(Return(std::make_shared<Config::NullGrpcMuxImpl>()));
}

void ClusterManagerImplTest::create(const Bootstrap& bootstrap) {
  // Override the bootstrap used by the mock Server::Instance object.
  cluster_manager_ = TestClusterManagerImpl::createTestClusterManager(bootstrap, factory_,
                                                                      factory_.server_context_);
  ON_CALL(factory_.server_context_, clusterManager()).WillByDefault(ReturnRef(*cluster_manager_));
  THROW_IF_NOT_OK(cluster_manager_->initialize(bootstrap));

  cluster_manager_->setPrimaryClustersInitializedCb([this, bootstrap]() {
    THROW_IF_NOT_OK(cluster_manager_->initializeSecondaryClusters(bootstrap));
  });
}

void ClusterManagerImplTest::createWithBasicStaticCluster() {
  const std::string yaml = R"EOF(
static_resources:
  clusters:
  - name: cluster_1
    connect_timeout: 0.250s
    lb_policy: ROUND_ROBIN
    type: STATIC
    load_assignment:
      cluster_name: cluster_1
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 11001
)EOF";

  create(parseBootstrapFromV3Yaml(yaml));
}

void ClusterManagerImplTest::createWithLocalClusterUpdate(const bool enable_merge_window) {
  std::string yaml = R"EOF(
static_resources:
  clusters:
  - name: cluster_1
    connect_timeout: 0.250s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: cluster_1
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 11001
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 11002
)EOF";
  const std::string merge_window_enabled = R"EOF(
    common_lb_config:
      update_merge_window: 3s
)EOF";
  const std::string merge_window_disabled = R"EOF(
    common_lb_config:
      update_merge_window: 0s
)EOF";

  yaml += enable_merge_window ? merge_window_enabled : merge_window_disabled;

  const auto& bootstrap = parseBootstrapFromV3Yaml(yaml);
  absl::Status creation_status = absl::OkStatus();
  cluster_manager_ = std::make_unique<MockedUpdatedClusterManagerImpl>(
      bootstrap, factory_, factory_.server_context_, local_cluster_update_, local_hosts_removed_,
      creation_status);
  THROW_IF_NOT_OK_REF(creation_status);
  THROW_IF_NOT_OK(cluster_manager_->initialize(bootstrap));
}

void ClusterManagerImplTest::checkStats(uint64_t added, uint64_t modified, uint64_t removed,
                                        uint64_t active, uint64_t warming) {
  EXPECT_EQ(added, factory_.stats_.counter("cluster_manager.cluster_added").value());
  EXPECT_EQ(modified, factory_.stats_.counter("cluster_manager.cluster_modified").value());
  EXPECT_EQ(removed, factory_.stats_.counter("cluster_manager.cluster_removed").value());
  EXPECT_EQ(active,
            factory_.stats_
                .gauge("cluster_manager.active_clusters", Stats::Gauge::ImportMode::NeverImport)
                .value());
  EXPECT_EQ(warming,
            factory_.stats_
                .gauge("cluster_manager.warming_clusters", Stats::Gauge::ImportMode::NeverImport)
                .value());
}

void ClusterManagerImplTest::checkConfigDump(const std::string& expected_dump_yaml,
                                             const Matchers::StringMatcher& name_matcher) {
  auto message_ptr =
      factory_.server_context_.admin_.config_tracker_.config_tracker_callbacks_["clusters"](
          name_matcher);
  const auto& clusters_config_dump =
      dynamic_cast<const envoy::admin::v3::ClustersConfigDump&>(*message_ptr);

  envoy::admin::v3::ClustersConfigDump expected_clusters_config_dump;
  TestUtility::loadFromYaml(expected_dump_yaml, expected_clusters_config_dump);
  EXPECT_EQ(expected_clusters_config_dump.DebugString(), clusters_config_dump.DebugString());
}

MetadataConstSharedPtr ClusterManagerImplTest::buildMetadata(const std::string& version) const {
  envoy::config::core::v3::Metadata metadata;

  if (!version.empty()) {
    Envoy::Config::Metadata::mutableMetadataValue(metadata, Config::MetadataFilters::get().ENVOY_LB,
                                                  "version")
        .set_string_value(version);
  }

  return std::make_shared<const envoy::config::core::v3::Metadata>(metadata);
}

} // namespace Upstream
} // namespace Envoy
