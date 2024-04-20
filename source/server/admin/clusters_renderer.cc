#include "source/server/admin/clusters_renderer.h"

#include <cstdint>
#include <functional>

#include "envoy/buffer/buffer.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/resource_manager.h"

#include "source/common/common/logger.h"
#include "source/common/upstream/host_utility.h"

namespace Envoy {
namespace Server {

ClustersJsonRenderer::ClustersJsonRenderer(
    uint64_t chunk_limit, Http::ResponseHeaderMap& response_headers,
    const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map)
    : chunk_limit_(chunk_limit), response_headers_(response_headers),
      cluster_info_map_(cluster_info_map), it_(cluster_info_map.cbegin()) {}

// TODO(demitriswan) implement using iterator state.
bool ClustersJsonRenderer::nextChunk(Buffer::Instance& response) {
  UNREFERENCED_PARAMETER(response);
  UNREFERENCED_PARAMETER(response_headers_);
  UNREFERENCED_PARAMETER(chunk_limit_);
  UNREFERENCED_PARAMETER(cluster_info_map_);
  return false;
}

// TODO(demitriswan) implement. See clusters_handler.cc.
void ClustersJsonRenderer::render(std::reference_wrapper<const Upstream::Cluster> cluster,
                                  Buffer::Instance& response) {
  UNREFERENCED_PARAMETER(cluster);
  UNREFERENCED_PARAMETER(response);
}

ClustersTextRenderer::ClustersTextRenderer(
    uint64_t chunk_limit, Http::ResponseHeaderMap& response_headers,
    const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map)
    : chunk_limit_(chunk_limit), response_headers_(response_headers), idx_(0) {
  std::vector<std::reference_wrapper<const Upstream::Cluster>> v;
  for (const auto& pair : cluster_info_map) {
    clusters_.push_back(pair.second);
  }
}

bool ClustersTextRenderer::nextChunk(Buffer::Instance& response) {
  const uint64_t original_request_size = response.length();
  for (; idx_ < clusters_.size() && response.length() - original_request_size < chunk_limit_;
       idx_++) {

    render(clusters_[idx_], response);
  }
  // TODO(demitriswan) See if these need to be updated here.
  UNREFERENCED_PARAMETER(response_headers_);
  return idx_ < clusters_.size() ? true : false;
}

void ClustersTextRenderer::render(std::reference_wrapper<const Upstream::Cluster> cluster,
                                  Buffer::Instance& response) {
  const Upstream::Cluster& unwrapped_cluster = cluster.get();
  const std::string& cluster_name = unwrapped_cluster.info()->name();
  response.add(fmt::format("{}::observability_name::{}\n", cluster_name,
                           unwrapped_cluster.info()->observabilityName()));
  addOutlierInfo(cluster_name, unwrapped_cluster.outlierDetector(), response);

  addCircuitBreakerSettings(
      cluster_name, "default",
      unwrapped_cluster.info()->resourceManager(Upstream::ResourcePriority::Default), response);
  addCircuitBreakerSettings(
      cluster_name, "high",
      unwrapped_cluster.info()->resourceManager(Upstream::ResourcePriority::High), response);

  response.add(fmt::format("{}::added_via_api::{}\n", cluster_name,
                           unwrapped_cluster.info()->addedViaApi()));
  if (const auto& name = unwrapped_cluster.info()->edsServiceName(); !name.empty()) {
    response.add(fmt::format("{}::eds_service_name::{}\n", cluster_name, name));
  }
  for (auto& host_set : unwrapped_cluster.prioritySet().hostSetsPerPriority()) {
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
        response.add(fmt::format("{}::{}::{}::{}\n", cluster_name, host_address, stat_name, stat));
      }

      response.add(
          fmt::format("{}::{}::hostname::{}\n", cluster_name, host_address, host->hostname()));
      response.add(fmt::format("{}::{}::health_flags::{}\n", cluster_name, host_address,
                               Upstream::HostUtility::healthFlagsToString(*host)));
      response.add(fmt::format("{}::{}::weight::{}\n", cluster_name, host_address, host->weight()));
      response.add(fmt::format("{}::{}::region::{}\n", cluster_name, host_address,
                               host->locality().region()));
      response.add(
          fmt::format("{}::{}::zone::{}\n", cluster_name, host_address, host->locality().zone()));
      response.add(fmt::format("{}::{}::sub_zone::{}\n", cluster_name, host_address,
                               host->locality().sub_zone()));
      response.add(fmt::format("{}::{}::canary::{}\n", cluster_name, host_address, host->canary()));
      response.add(
          fmt::format("{}::{}::priority::{}\n", cluster_name, host_address, host->priority()));
      response.add(fmt::format(
          "{}::{}::success_rate::{}\n", cluster_name, host_address,
          host->outlierDetector().successRate(
              Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin)));
      response.add(fmt::format(
          "{}::{}::local_origin_success_rate::{}\n", cluster_name, host_address,
          host->outlierDetector().successRate(
              Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin)));
    }
  }
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
