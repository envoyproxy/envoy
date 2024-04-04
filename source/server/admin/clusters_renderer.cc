#include "source/server/admin/clusters_renderer.h"

#include <cstdint>

#include "envoy/buffer/buffer.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/resource_manager.h"

#include "source/common/upstream/host_utility.h"

namespace Envoy {
namespace Server {

ClustersJsonRenderer::ClustersJsonRenderer(
    uint64_t chunk_limit, Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
    const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map)
    : chunk_limit_{chunk_limit}, response_headers_{response_headers}, response_{response},
      cluster_info_map_{cluster_info_map}, it_{cluster_info_map.begin()} {}

// TODO(demitriswan) implement using iterator state.
bool ClustersJsonRenderer::nextChunk() {
  UNREFERENCED_PARAMETER(response_headers_);
  UNREFERENCED_PARAMETER(response_);
  UNREFERENCED_PARAMETER(chunk_limit_);
  UNREFERENCED_PARAMETER(cluster_info_map_);
  return false;
}

// TODO(demitriswan) implement. See clusters_handler.cc.
void ClustersJsonRenderer::render(std::reference_wrapper<const Upstream::Cluster> cluster) {
  UNREFERENCED_PARAMETER(cluster);
}

ClustersTextRenderer::ClustersTextRenderer(
    uint64_t chunk_limit, Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
    const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map)
    : chunk_limit_{chunk_limit}, response_headers_{response_headers}, response_{response},
      cluster_info_map_{cluster_info_map}, it_{cluster_info_map.begin()} {}

bool ClustersTextRenderer::nextChunk() {
  const uint64_t original_request_size = response_.length();
  for (;
       it_ != cluster_info_map_.end() && response_.length() - original_request_size < chunk_limit_;
       it_++) {
    const Upstream::Cluster& cluster = it_->second.get();
    const std::string& cluster_name = cluster.info()->name();
    response_.add(fmt::format("{}::observability_name::{}\n", cluster_name,
                              cluster.info()->observabilityName()));
    addOutlierInfo(cluster_name, cluster.outlierDetector(), response_);

    addCircuitBreakerSettings(cluster_name, "default",
                              cluster.info()->resourceManager(Upstream::ResourcePriority::Default),
                              response_);
    addCircuitBreakerSettings(cluster_name, "high",
                              cluster.info()->resourceManager(Upstream::ResourcePriority::High),
                              response_);

    response_.add(
        fmt::format("{}::added_via_api::{}\n", cluster_name, cluster.info()->addedViaApi()));
    if (const auto& name = cluster.info()->edsServiceName(); !name.empty()) {
      response_.add(fmt::format("{}::eds_service_name::{}\n", cluster_name, name));
    }
    for (auto& host_set : cluster.prioritySet().hostSetsPerPriority()) {
      for (auto& host : host_set->hosts()) {
        const std::string& host_address = host->address()->asString();
        std::map<absl::string_view, uint64_t> all_stats;
        for (const auto& [counter_name, counter] : host->counters()) {
          all_stats[counter_name] = counter.get().value();
        }

        for (const auto& [gauge_name, gauge] : host->gauges()) {
          all_stats[gauge_name] = gauge.get().value();
        }

        for (const auto& [stat_name, stat] : all_stats) {
          response_.add(
              fmt::format("{}::{}::{}::{}\n", cluster_name, host_address, stat_name, stat));
        }

        response_.add(
            fmt::format("{}::{}::hostname::{}\n", cluster_name, host_address, host->hostname()));
        response_.add(fmt::format("{}::{}::health_flags::{}\n", cluster_name, host_address,
                                  Upstream::HostUtility::healthFlagsToString(*host)));
        response_.add(
            fmt::format("{}::{}::weight::{}\n", cluster_name, host_address, host->weight()));
        response_.add(fmt::format("{}::{}::region::{}\n", cluster_name, host_address,
                                  host->locality().region()));
        response_.add(
            fmt::format("{}::{}::zone::{}\n", cluster_name, host_address, host->locality().zone()));
        response_.add(fmt::format("{}::{}::sub_zone::{}\n", cluster_name, host_address,
                                  host->locality().sub_zone()));
        response_.add(
            fmt::format("{}::{}::canary::{}\n", cluster_name, host_address, host->canary()));
        response_.add(
            fmt::format("{}::{}::priority::{}\n", cluster_name, host_address, host->priority()));
        response_.add(fmt::format(
            "{}::{}::success_rate::{}\n", cluster_name, host_address,
            host->outlierDetector().successRate(
                Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin)));
        response_.add(fmt::format(
            "{}::{}::local_origin_success_rate::{}\n", cluster_name, host_address,
            host->outlierDetector().successRate(
                Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin)));
      }
    }
  }
  // TODO(demitriswan) See if these need to be updated here.
  UNREFERENCED_PARAMETER(response_headers_);
  return false;
}

// TODO(demitriswan) implement. See clusters_handler.cc.
void ClustersTextRenderer::render(std::reference_wrapper<const Upstream::Cluster> cluster) {
  UNREFERENCED_PARAMETER(cluster);
}

void ClustersTextRenderer::addOutlierInfo(const std::string& cluster_name,
                                          const Upstream::Outlier::Detector* outlier_detector,
                                          Buffer::Instance& response) {
  if (outlier_detector) {
    response.add(fmt::format(
        "{}::outlier::success_rate_average::{:g}\n", cluster_name,
        outlier_detector->successRateAverage(
            Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin)));
    response.add(fmt::format(
        "{}::outlier::success_rate_ejection_threshold::{:g}\n", cluster_name,
        outlier_detector->successRateEjectionThreshold(
            Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin)));
    response.add(fmt::format(
        "{}::outlier::local_origin_success_rate_average::{:g}\n", cluster_name,
        outlier_detector->successRateAverage(
            Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin)));
    response.add(fmt::format(
        "{}::outlier::local_origin_success_rate_ejection_threshold::{:g}\n", cluster_name,
        outlier_detector->successRateEjectionThreshold(
            Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin)));
  }
}

void ClustersTextRenderer::addCircuitBreakerSettings(const std::string& cluster_name,
                                                     const std::string& priority_str,
                                                     Upstream::ResourceManager& resource_manager,
                                                     Buffer::Instance& response) {
  response.add(fmt::format("{}::{}_priority::max_connections::{}\n", cluster_name, priority_str,
                           resource_manager.connections().max()));
  response.add(fmt::format("{}::{}_priority::max_pending_requests::{}\n", cluster_name,
                           priority_str, resource_manager.pendingRequests().max()));
  response.add(fmt::format("{}::{}_priority::max_requests::{}\n", cluster_name, priority_str,
                           resource_manager.requests().max()));
  response.add(fmt::format("{}::{}_priority::max_retries::{}\n", cluster_name, priority_str,
                           resource_manager.retries().max()));
}

} // namespace Server
} // namespace Envoy
