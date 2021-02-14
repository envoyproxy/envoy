#include "cluster_manager.h"

#include <chrono>
#include <functional>

#include "common/upstream/cluster_manager_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

using ::testing::_;
using ::testing::Eq;
using ::testing::Return;
using ::testing::ReturnRef;

MockClusterManager::MockClusterManager(TimeSource&) : MockClusterManager() {}

MockClusterManager::MockClusterManager()
    : cluster_stat_names_(*symbol_table_), cluster_load_report_stat_names_(*symbol_table_),
      cluster_circuit_breakers_stat_names_(*symbol_table_),
      cluster_request_response_size_stat_names_(*symbol_table_),
      cluster_timeout_budget_stat_names_(*symbol_table_) {
  ON_CALL(*this, bindConfig()).WillByDefault(ReturnRef(bind_config_));
  ON_CALL(*this, adsMux()).WillByDefault(Return(ads_mux_));
  ON_CALL(*this, grpcAsyncClientManager()).WillByDefault(ReturnRef(async_client_manager_));
  ON_CALL(*this, localClusterName()).WillByDefault((ReturnRef(local_cluster_name_)));
  ON_CALL(*this, subscriptionFactory()).WillByDefault(ReturnRef(subscription_factory_));
  ON_CALL(*this, futureThreadLocalCluster(_))
      .WillByDefault(testing::WithArg<0>(Invoke([this](absl::string_view name) {
        return std::make_shared<Upstream::ReadyFutureCluster>(name, *this);
      })));
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
}

void MockClusterManager::initializeThreadLocalClusters(
    const std::vector<std::string>& cluster_names) {
  // TODO(mattklein123): This should create a dedicated and new mock for each initialized cluster,
  // but this has larger test implications. I will fix this in a follow up.
  for (const auto& cluster_name : cluster_names) {
    ON_CALL(*this, getThreadLocalCluster(cluster_name))
        .WillByDefault(Return(&thread_local_cluster_));
  }
}

} // namespace Upstream
} // namespace Envoy
