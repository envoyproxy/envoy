#pragma once

#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_manager.h"

namespace Envoy {
namespace Upstream {

using ::testing::Return;
using ::testing::ReturnPointee;
using ::testing::ReturnRef;

class PerEndpointMetricsTestHelper {
public:
  PerEndpointMetricsTestHelper() {
    ON_CALL(cm_, clusters()).WillByDefault(ReturnPointee(&cluster_info_maps_));
  }

  MockClusterMockPrioritySet& makeCluster(absl::string_view name, uint32_t num_hosts = 1,
                                          bool warming = false) {
    clusters_.emplace_back(std::make_unique<NiceMock<MockClusterMockPrioritySet>>());
    clusters_.back()->info_->name_ = name;
    ON_CALL(*clusters_.back()->info_, perEndpointStatsEnabled()).WillByDefault(Return(true));
    ON_CALL(*clusters_.back()->info_, observabilityName())
        .WillByDefault(ReturnRef(clusters_.back()->info_->name_));
    static Stats::TagVector empty_tags;
    ON_CALL(clusters_.back()->info_->stats_store_, fixedTags())
        .WillByDefault(ReturnRef(empty_tags));

    if (warming) {
      cluster_info_maps_.warming_clusters_.emplace(name, *clusters_.back());
    } else {
      cluster_info_maps_.active_clusters_.emplace(name, *clusters_.back());
    }

    addHosts(*clusters_.back(), num_hosts);

    return *clusters_.back();
  }

  MockHost& addHost(MockClusterMockPrioritySet& cluster, uint32_t priority = 0) {
    host_count_++;
    MockHostSet* host_set = cluster.priority_set_.getMockHostSet(priority);
    auto host = std::make_shared<NiceMock<MockHost>>();
    ON_CALL(*host, address())
        .WillByDefault(Return(Network::Utility::parseInternetAddressAndPortNoThrow(
            fmt::format("127.0.0.{}:80", host_count_))));
    ON_CALL(*host, hostname()).WillByDefault(ReturnRef(EMPTY_STRING));
    ON_CALL(*host, coarseHealth()).WillByDefault(Return(Host::Health::Healthy));

    counters_.emplace_back();
    auto& c1 = counters_.back();
    c1.add((host_count_ * 10) + 1);
    counters_.emplace_back();
    auto& c2 = counters_.back();
    c2.add((host_count_ * 10) + 2);
    gauges_.emplace_back();
    auto& g1 = gauges_.back();
    g1.add((host_count_ * 10) + 3);
    gauges_.emplace_back();
    auto& g2 = gauges_.back();
    g2.add((host_count_ * 10) + 4);

    ON_CALL(*host, counters())
        .WillByDefault(
            Return(std::vector<std::pair<absl::string_view, Stats::PrimitiveCounterReference>>{
                {"c1", c1}, {"c2", c2}}));
    ON_CALL(*host, gauges())
        .WillByDefault(
            Return(std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>>{
                {"g1", g1}, {"g2", g2}}));
    host_set->hosts_.push_back(host);
    return *host;
  }

  MockHost& addHostSingleCounter(MockClusterMockPrioritySet& cluster, uint32_t priority = 0) {
    host_count_++;
    MockHostSet* host_set = cluster.priority_set_.getMockHostSet(priority);
    auto host = std::make_shared<NiceMock<MockHost>>();
    ON_CALL(*host, address())
        .WillByDefault(Return(Network::Utility::parseInternetAddressAndPortNoThrow(
            fmt::format("127.0.0.{}:80", host_count_))));
    ON_CALL(*host, hostname()).WillByDefault(ReturnRef(EMPTY_STRING));
    ON_CALL(*host, coarseHealth()).WillByDefault(Return(Host::Health::Healthy));

    counters_.emplace_back();
    auto& c1 = counters_.back();
    c1.add((host_count_ * 10) + 1);

    ON_CALL(*host, counters())
        .WillByDefault(
            Return(std::vector<std::pair<absl::string_view, Stats::PrimitiveCounterReference>>{
                {"c1", c1}}));
    ON_CALL(*host, gauges())
        .WillByDefault(
            Return(std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>>{}));
    host_set->hosts_.push_back(host);
    return *host;
  }

  MockHost& addHostSingleGauge(MockClusterMockPrioritySet& cluster, uint32_t priority = 0) {
    host_count_++;
    MockHostSet* host_set = cluster.priority_set_.getMockHostSet(priority);
    auto host = std::make_shared<NiceMock<MockHost>>();
    ON_CALL(*host, address())
        .WillByDefault(Return(Network::Utility::parseInternetAddressAndPortNoThrow(
            fmt::format("127.0.0.{}:80", host_count_))));
    ON_CALL(*host, hostname()).WillByDefault(ReturnRef(EMPTY_STRING));
    ON_CALL(*host, coarseHealth()).WillByDefault(Return(Host::Health::Healthy));

    gauges_.emplace_back();
    auto& g1 = gauges_.back();
    g1.add((host_count_ * 10) + 1);

    ON_CALL(*host, counters())
        .WillByDefault(
            Return(std::vector<std::pair<absl::string_view, Stats::PrimitiveCounterReference>>{}));
    ON_CALL(*host, gauges())
        .WillByDefault(Return(
            std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>>{{"g1", g1}}));
    host_set->hosts_.push_back(host);
    return *host;
  }

  void addHosts(MockClusterMockPrioritySet& cluster, uint32_t count = 1) {
    for (uint32_t i = 0; i < count; i++) {
      addHost(cluster);
    }
  }

  NiceMock<MockClusterManager> cm_;
  ClusterManager::ClusterInfoMaps cluster_info_maps_;
  std::vector<std::unique_ptr<MockClusterMockPrioritySet>> clusters_;
  std::list<Stats::PrimitiveCounter> counters_;
  std::list<Stats::PrimitiveGauge> gauges_;
  uint32_t host_count_{0};
};

} // namespace Upstream
} // namespace Envoy
