#include "source/server/admin/clusters_chunk_processor.h"

#include <cstdint>
#include <functional>
#include <memory>

#include "envoy/admin/v3/clusters.pb.h"
#include "envoy/buffer/buffer.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/resource_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/upstream/host_utility.h"

namespace Envoy {
namespace Server {

TextClustersChunkProcessor::TextClustersChunkProcessor(
    uint64_t chunk_limit, Http::ResponseHeaderMap& response_headers,
    const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map)
    : chunk_limit_(chunk_limit), response_headers_(response_headers), idx_(0) {
  for (const auto& pair : cluster_info_map) {
    clusters_.push_back(pair.second);
  }
}

bool TextClustersChunkProcessor::nextChunk(Buffer::Instance& response) {
  const uint64_t original_request_size = response.length();
  for (; idx_ < clusters_.size() && response.length() - original_request_size < chunk_limit_;
       idx_++) {

    render(clusters_[idx_], response);
  }
  // TODO(demitriswan) See if these need to be updated here.
  UNREFERENCED_PARAMETER(response_headers_);
  return idx_ < clusters_.size() ? true : false;
}

void TextClustersChunkProcessor::render(std::reference_wrapper<const Upstream::Cluster> cluster,
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

void TextClustersChunkProcessor::addOutlierInfo(const std::string& cluster_name,
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

void TextClustersChunkProcessor::addCircuitBreakerSettings(
    const std::string& cluster_name, const std::string& priority_str,
    Upstream::ResourceManager& resource_manager, Buffer::Instance& response) {
  response.add(fmt::format("{}::{}_priority::max_connections::{}\n", cluster_name, priority_str,
                           resource_manager.connections().max()));
  response.add(fmt::format("{}::{}_priority::max_pending_requests::{}\n", cluster_name,
                           priority_str, resource_manager.pendingRequests().max()));
  response.add(fmt::format("{}::{}_priority::max_requests::{}\n", cluster_name, priority_str,
                           resource_manager.requests().max()));
  response.add(fmt::format("{}::{}_priority::max_retries::{}\n", cluster_name, priority_str,
                           resource_manager.retries().max()));
}

JsonClustersChunkProcessor::JsonClustersChunkProcessor(
    uint64_t chunk_limit, Http::ResponseHeaderMap& response_headers,
    const Upstream::ClusterManager::ClusterInfoMap& cluster_info_map)
    : chunk_limit_(chunk_limit), response_headers_(response_headers), idx_(0) {
  for (const auto& pair : cluster_info_map) {
    clusters_.push_back(pair.second);
  }
  std::unique_ptr<Json::Streamer> streamer = std::make_unique<Json::Streamer>(buffer_);
  Json::Streamer::MapPtr root_map = streamer->makeRootMap();
  root_map->addKey("cluster_statuses");
  Json::Streamer::ArrayPtr clusters = root_map->addArray();
  json_context_holder_.push_back(std::make_unique<ClustersJsonContext>(
      std::move(streamer), buffer_, std::move(root_map), std::move(clusters)));
}

bool JsonClustersChunkProcessor::nextChunk(Buffer::Instance& response) {
  // TODO(demitriswan) See if these need to be updated here.
  UNREFERENCED_PARAMETER(response_headers_);

  const uint64_t original_request_size = response.length();
  for (; idx_ < clusters_.size() && response.length() - original_request_size < chunk_limit_;
       idx_++) {

    render(clusters_[idx_], response);
  }
  if (idx_ < clusters_.size()) {
    return true;
  }
  finalize(response);
  return false;
}

void JsonClustersChunkProcessor::render(std::reference_wrapper<const Upstream::Cluster> cluster,
                                        Buffer::Instance& response) {
  Json::Streamer::MapPtr cluster_map = json_context_holder_.back()->clusters_->addMap();
  const Upstream::Cluster& unwrapped_cluster = cluster.get();
  Upstream::ClusterInfoConstSharedPtr cluster_info = unwrapped_cluster.info();

  std::vector<const Json::Streamer::Map::NameValue> top_level_entries{
      {"name", cluster_info->name()},
      {"observability_name", cluster_info->observabilityName()},
  };
  addMapEntries(cluster_map.get(), response, top_level_entries);

  if (const std::string& name = cluster_info->edsServiceName(); !name.empty()) {
    std::vector<const Json::Streamer::Map::NameValue> eds_service_name_entry{
        {"eds_service_name", name},
    };
    addMapEntries(cluster_map.get(), response, eds_service_name_entry);
  }

  {
    cluster_map->addKey("circuit_breakers");
    Json::Streamer::MapPtr circuit_breakers = cluster_map->addMap();
    circuit_breakers->addKey("thresholds");
    Json::Streamer::ArrayPtr thresholds = circuit_breakers->addArray();
    addCircuitBreakerSettingsAsJson(
        thresholds.get(), response, envoy::config::core::v3::RoutingPriority::DEFAULT,
        cluster_info->resourceManager(Upstream::ResourcePriority::Default));
    addCircuitBreakerSettingsAsJson(
        thresholds.get(), response, envoy::config::core::v3::RoutingPriority::HIGH,
        cluster_info->resourceManager(Upstream::ResourcePriority::High));
  } // Terminate the map.
}

void JsonClustersChunkProcessor::addCircuitBreakerSettingsAsJson(
    Json::Streamer::Array* raw_thresholds_ptr, Buffer::Instance& response,
    const envoy::config::core::v3::RoutingPriority& priority,
    Upstream::ResourceManager& resource_manager) {
  Json::Streamer::MapPtr threshold = raw_thresholds_ptr->addMap();
  std::vector<const Json::Streamer::Map::NameValue> entries{
      {"priority",
       priority == envoy::config::core::v3::RoutingPriority::DEFAULT ? "DEFAULT" : "HIGH"},
      {"max_connections", resource_manager.connections().max()},
      {"max_pending_requests", resource_manager.pendingRequests().max()},
      {"max_requests", resource_manager.requests().max()},
      {"max_retries", resource_manager.retries().max()},
  };
  addMapEntries(threshold.get(), response, entries);
}

// Json::Streamer holds a reference to a Buffer::Instance reference but the API for Request
// takes a Buffer::Instance reference on each call to nextChunk. So, at the end of each
// Json::Streamer function invocation, call drainBufferIntoResponse to ensure that the
// contents written to its buffer gets moved and appended to the response.
void JsonClustersChunkProcessor::drainBufferIntoResponse(Buffer::Instance& response) {
  if (&response != &buffer_) {
    response.move(buffer_);
  }
}

void JsonClustersChunkProcessor::addMapEntries(
    Json::Streamer::Map* raw_map_ptr, Buffer::Instance& response,
    std::vector<const Json::Streamer::Map::NameValue>& entries) {
  raw_map_ptr->addEntries(entries);
  drainBufferIntoResponse(response);
}

// Start destruction of the ClustersJsonContext to render the closing tokens and push to the
// buffer. Since we've pushed data into the buffer in the Json::Streamer, we'll need to drain
// the contents into the response.
void JsonClustersChunkProcessor::finalize(Buffer::Instance& response) {
  json_context_holder_.pop_back();
  drainBufferIntoResponse(response);
}

} // namespace Server
} // namespace Envoy
