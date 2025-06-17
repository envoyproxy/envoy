#include "cluster_manager.h"

#include <chrono>
#include <functional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

using ::testing::Return;
using ::testing::ReturnRef;

MockClusterManager::MockClusterManager(TimeSource&) : MockClusterManager() {}

MockClusterManager::MockClusterManager()
    : cluster_stat_names_(*symbol_table_), cluster_config_update_stat_names_(*symbol_table_),
      cluster_lb_stat_names_(*symbol_table_), cluster_endpoint_stat_names_(*symbol_table_),
      cluster_load_report_stat_names_(*symbol_table_),
      cluster_circuit_breakers_stat_names_(*symbol_table_),
      cluster_request_response_size_stat_names_(*symbol_table_),
      cluster_timeout_budget_stat_names_(*symbol_table_) {
  ON_CALL(*this, bindConfig()).WillByDefault(ReturnRef(bind_config_));
  ON_CALL(*this, adsMux()).WillByDefault(Return(ads_mux_));
  ON_CALL(*this, grpcAsyncClientManager()).WillByDefault(ReturnRef(async_client_manager_));
  ON_CALL(*this, localClusterName()).WillByDefault((ReturnRef(local_cluster_name_)));
  ON_CALL(*this, subscriptionFactory()).WillByDefault(ReturnRef(subscription_factory_));
  ON_CALL(*this, allocateOdCdsApi(_, _, _, _))
      .WillByDefault(
          Invoke([](OdCdsCreationFunction, const envoy::config::core::v3::ConfigSource&,
                    OptRef<xds::core::v3::ResourceLocator>,
                    ProtobufMessage::ValidationVisitor&) { return MockOdCdsApiHandle::create(); }));
  ON_CALL(*this, addOrUpdateCluster(_, _, _)).WillByDefault(Return(false));
}

MockClusterManager::~MockClusterManager() = default;

void MockClusterManager::initializeClusters(const std::vector<std::string>& active_cluster_names,
                                            const std::vector<std::string>&) {
  active_clusters_.clear();
  ClusterManager::ClusterInfoMaps info_map;
  for (const auto& name : active_cluster_names) {
    auto new_cluster = std::make_unique<NiceMock<MockCluster>>();
    new_cluster->info_->name_ = name;
    info_map.active_clusters_.emplace(name, *new_cluster);
    active_clusters_.emplace(name, std::move(new_cluster));
  }

  // TODO(mattklein123): Add support for warming clusters when needed.

  ON_CALL(*this, clusters()).WillByDefault(Return(info_map));
  ON_CALL(*this, getActiveCluster(_))
      .WillByDefault(Invoke([this](absl::string_view cluster_name) -> OptRef<const Cluster> {
        if (const auto& it = active_clusters_.find(cluster_name); it != active_clusters_.end()) {
          return *it->second;
        }
        return absl::nullopt;
      }));
  ON_CALL(*this, hasCluster(_))
      .WillByDefault(Invoke([this](const std::string& cluster_name) -> bool {
        return active_clusters_.find(cluster_name) != active_clusters_.end();
      }));
}

void MockClusterManager::initializeThreadLocalClusters(
    const std::vector<std::string>& cluster_names) {
  // TODO(mattklein123): This should create a dedicated and new mock for each initialized cluster,
  // but this has larger test implications. I will fix this in a follow up.
  for (const auto& cluster_name : cluster_names) {
    ON_CALL(*this, getThreadLocalCluster(absl::string_view(cluster_name)))
        .WillByDefault(Return(&thread_local_cluster_));
  }
}

envoy::config::core::v3::BindConfig& MockClusterManager::mutableBindConfig() {
  if (!bind_config_.has_value()) {
    bind_config_ = envoy::config::core::v3::BindConfig{};
  }
  return *bind_config_;
}

} // namespace Upstream
} // namespace Envoy
