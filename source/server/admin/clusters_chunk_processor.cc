#include "clusters_chunk_processor.h"
#include "source/server/admin/clusters_chunk_processor.h"

#include <cstdint>
#include <functional>
#include <memory>

#include "envoy/admin/v3/clusters.pb.h"
#include "envoy/buffer/buffer.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/resource_manager.h"
#include "envoy/upstream/upstream.h"

#include "source/common/network/utility.h"
#include "source/common/upstream/host_utility.h"

namespace Envoy {
namespace Server {

void setHealthFlag(Json::Streamer::Map* raw_host_ptr, Upstream::Host::HealthFlag flag,
                   const Upstream::Host& host);

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
  Upstream::ClusterInfoConstSharedPtr cluster_info = cluster.get().info();

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

  addCircuitBreakers(cluster_map.get(), cluster_info, response);
  addEjectionThresholds(cluster_map.get(), cluster.get(), response);
  std::vector<const Json::Streamer::Map::NameValue> added_via_api{
      {"added_via_api", cluster_info->addedViaApi()},
  };
  addMapEntries(cluster_map.get(), response, added_via_api);
}

void JsonClustersChunkProcessor::addHostStatuses(Json::Streamer::Map* raw_clusters_map_ptr,
                                                 const Upstream::Cluster& unwrapped_cluster,
                                                 Buffer::Instance& response) {

  Json::Streamer::ArrayPtr host_sets = raw_clusters_map_ptr->addArray();
  for (const Upstream::HostSetPtr& host_set :
       unwrapped_cluster.prioritySet().hostSetsPerPriority()) {
    Json::Streamer::ArrayPtr hosts = raw_clusters_map_ptr->addArray();
    processHostSet(host_sets.get(), host_set, response);
  }
}

void JsonClustersChunkProcessor::processHostSet(Json::Streamer::Array* raw_hosts_statuses_ptr,
                                                const Upstream::HostSetPtr& host_set,
                                                Buffer::Instance& response) {
  Json::Streamer::ArrayPtr host_statuses_ptr = raw_hosts_statuses_ptr->addArray();
  for (const Upstream::HostSharedPtr& host : host_set->hosts()) {
    processHost(host_statuses_ptr.get(), host, response);
  }
}

void JsonClustersChunkProcessor::processHost(Json::Streamer::Array* raw_host_statuses_ptr,
                                             const Upstream::HostSharedPtr& host,
                                             Buffer::Instance& response) {
  Json::Streamer::MapPtr host_ptr = raw_host_statuses_ptr->addMap();
  std::vector<const Json::Streamer::Map::NameValue> hostname{
      {"hostname", host->hostname()},
  };
  addMapEntries(host_ptr.get(), response, hostname);
  {
    host_ptr->addKey("locality");
    Json::Streamer::MapPtr locality_ptr = host_ptr->addMap();
    std::vector<const Json::Streamer::Map::NameValue> locality{
        {"region", host->locality().region()},
        {"zone", host->locality().zone()},
        {"sub_zone", host->locality().sub_zone()},
    };
    addMapEntries(locality_ptr.get(), response, locality);
  }

  setHealthFlags(host_ptr.get(), host, response);

  double success_rate = host->outlierDetector().successRate(
      Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin);
  if (success_rate >= 0.0) {
    std::vector<const Json::Streamer::Map::NameValue> success_rate_property{
        {"suceess_rate", success_rate},
    };
    addMapEntries(host_ptr.get(), response, success_rate_property);
  }

  std::vector<const Json::Streamer::Map::NameValue> weight_and_priority{
      {"weight", uint64_t(host->weight())},
      {"priority", uint64_t(host->priority())},
  };
  addMapEntries(host_ptr.get(), response, weight_and_priority);
  success_rate = host->outlierDetector().successRate(
      Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin);
  if (success_rate >= 0.0) {
    std::vector<const Json::Streamer::Map::NameValue> success_rate_property{
        {"local_origin_success_rate", success_rate},
    };
    addMapEntries(host_ptr.get(), response, success_rate_property);
  }
}

void JsonClustersChunkProcessor::buildHostStats(Json::Streamer::Map* raw_host_ptr,
                                                const Upstream::HostSharedPtr& host,
                                                Buffer::Instance& response) {
  Json::Streamer::ArrayPtr stats = raw_host_ptr->addArray();
  for (const auto& [counter_name, counter] : host->counters()) {
    Json::Streamer::MapPtr stat_obj = stats->addMap();
    // TODO(demitriswan) Make sure this name conversion works as expected
    std::vector<const Json::Streamer::Map::NameValue> stats_properties{
        {"type", envoy::admin::v3::SimpleMetric_Type_Name(envoy::admin::v3::SimpleMetric::COUNTER)},
        {"name", counter_name},
        {"value", counter.get().value()},
    };
    addMapEntries(raw_host_ptr, response, stats_properties);
  }
  for (const auto& [gauge_name, gauge] : host->gauges()) {
    Json::Streamer::MapPtr stat_obj = stats->addMap();
    // TODO(demitriswan) Make sure this name conversion works as expected
    std::vector<const Json::Streamer::Map::NameValue> stats_properties{
        {"type", envoy::admin::v3::SimpleMetric_Type_Name(envoy::admin::v3::SimpleMetric::GAUGE)},
        {"name", gauge_name},
        {"value", gauge.get().value()},
    };
    addMapEntries(raw_host_ptr, response, stats_properties);
  }
}

void JsonClusterChunkProcessor::setHealthFlags(Json::Streamer::Map* raw_host_ptr,
                                               const Upstream::HostSharedPtr& host,
                                               Buffer::Instance& response) {
  raw_host_ptr->addKey("health_stats");
  Json::Streamer::MapPtr heath_status_ptr = raw_host_ptr->addMap();
// Invokes setHealthFlag for each health flag.
#define SET_HEALTH_FLAG(name, notused)                                                             \
  setHealthFlag(map_status_ptr.get(), Upstream::Host::HealthFlag::name, host, response);
  HEALTH_FLAG_ENUM_VALUES(SET_HEALTH_FLAG)
#undef SET_HEALTH_FLAG
}

void JsonClustersChunkProcessor::setHealthFlag(Json::Streamer::Map* health_status_ptr,
                                               Upstream::Host::HealthFlag flag,
                                               const Upstream::HostSharedPtr& host,
                                               Buffer::Instance& response) {
  switch (flag) {
  case Upstream::Host::HealthFlag::FAILED_ACTIVE_HC: {
    std::vector<const Json::Streamer::Map::NameValue> status{
        {"failed_active_health_check", host.get()->healthFlagGet(flag)},
    };
    addMapEntries(health_status_ptr, response, status);
    break;
  }
  case Upstream::Host::HealthFlag::FAILED_OUTLIER_CHECK: {
    std::vector<const Json::Streamer::Map::NameValue> status{
        {"failed_outlier_check", host.get()->healthFlagGet(flag)},
    };
    addMapEntries(health_status_ptr, response, status);
    break;
  }
  case Upstream::Host::HealthFlag::FAILED_EDS_HEALTH:
  case Upstream::Host::HealthFlag::DEGRADED_EDS_HEALTH:
  case Upstream::Host::HealthFlag::EDS_STATUS_DRAINING: {
    // TODO(demitriswan) make sure this name conversion works as expected.
    std::vector<const Json::Streamer::Map::NameValue> status{
        {"eds_health_status",
         envoy::config::core::v3::HealthStatus_Name(host.get()->healthFlagGet(flag))},
    };
    addMapEntries(health_status_ptr, response, status);
    break;
  }
  case Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC: {
    std::vector<const Json::Streamer::Map::NameValue> status{
        {"failed_active_degraded_check", host.get()->healthFlagGet(flag)},
    };
    addMapEntries(health_status_ptr, response, status);
    break;
  }
  case Upstream::Host::HealthFlag::PENDING_DYNAMIC_REMOVAL: {
    std::vector<const Json::Streamer::Map::NameValue> status{
        {"pending_dynamic_removal", host.get()->healthFlagGet(flag)},
    };
    addMapEntries(health_status_ptr, response, status);
    break;
  }
  case Upstream::Host::HealthFlag::PENDING_ACTIVE_HC: {
    std::vector<const Json::Streamer::Map::NameValue> status{
        {"pending_active_hc", host.get()->healthFlagGet(flag)},
    };
    addMapEntries(health_status_ptr, response, status);
    break;
  }
  case Upstream::Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL: {
    std::vector<const Json::Streamer::Map::NameValue> status{
        {"excluded_via_immediate_hc_fail", host.get()->healthFlagGet(flag)},
    };
    addMapEntries(health_status_ptr, response, status);
    break;
  }
  case Upstream::Host::HealthFlag::ACTIVE_HC_TIMEOUT: {
    std::vector<const Json::Streamer::Map::NameValue> status{
        {"active_hc_timeout", host.get()->healthFlagGet(flag)},
    };
    addMapEntries(health_status_ptr, response, status);
    break;
  }
  }

  void JsonClustersChunkProcessor::addEjectionThresholds(Json::Streamer::Map * raw_clusters_map_ptr,
                                                         const Upstream::Cluster& unwrapped_cluster,
                                                         Buffer::Instance& response) {
    const Upstream::Outlier::Detector* outlier_detector = unwrapped_cluster.outlierDetector();
    if (outlier_detector != nullptr &&
        outlier_detector->successRateEjectionThreshold(
            Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin) > 0.0) {
      std::vector<const Json::Streamer::Map::NameValue> success_rate_ejection_threshold{
          {"success_rate_ejection_threshold",
           double(outlier_detector->successRateEjectionThreshold(
               Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin))},
      };
      addMapEntries(raw_clusters_map_ptr, response, success_rate_ejection_threshold);
    }
    if (outlier_detector != nullptr &&
        outlier_detector->successRateEjectionThreshold(
            Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin) > 0.0) {
      std::vector<const Json::Streamer::Map::NameValue> local_success_rate_ejection_threshold{
          {"local_success_rate_ejection_threshold",
           double(outlier_detector->successRateEjectionThreshold(
               Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin))},
      };
      addMapEntries(raw_clusters_map_ptr, response, local_success_rate_ejection_threshold);
    }
  }

  void JsonClustersChunkProcessor::addCircuitBreakers(
      Json::Streamer::Map * raw_clusters_map_ptr, Upstream::ClusterInfoConstSharedPtr cluster_info,
      Buffer::Instance & response) {
    raw_clusters_map_ptr->addKey("circuit_breakers");
    Json::Streamer::MapPtr circuit_breakers = raw_clusters_map_ptr->addMap();
    circuit_breakers->addKey("thresholds");
    Json::Streamer::ArrayPtr thresholds = circuit_breakers->addArray();
    addCircuitBreakerForPriority(
        envoy::config::core::v3::RoutingPriority::DEFAULT, thresholds.get(), response,
        cluster_info->resourceManager(Upstream::ResourcePriority::Default));
    addCircuitBreakerForPriority(envoy::config::core::v3::RoutingPriority::HIGH, thresholds.get(),
                                 response,
                                 cluster_info->resourceManager(Upstream::ResourcePriority::High));
  }

  void JsonClustersChunkProcessor::addCircuitBreakerForPriority(
      const envoy::config::core::v3::RoutingPriority& priority,
      Json::Streamer::Array* raw_thresholds_ptr, Buffer::Instance& response,
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
  void JsonClustersChunkProcessor::drainBufferIntoResponse(Buffer::Instance & response) {
    if (&response != &buffer_) {
      response.move(buffer_);
    }
  }

  void JsonClustersChunkProcessor::addMapEntries(
      Json::Streamer::Map * raw_map_ptr, Buffer::Instance & response,
      std::vector<const Json::Streamer::Map::NameValue> & entries) {
    raw_map_ptr->addEntries(entries);
    drainBufferIntoResponse(response);
  }

  // Start destruction of the ClustersJsonContext to render the closing tokens and push to the
  // buffer. Since we've pushed data into the buffer in the Json::Streamer, we'll need to drain
  // the contents into the response.
  void JsonClustersChunkProcessor::finalize(Buffer::Instance & response) {
    json_context_holder_.pop_back();
    drainBufferIntoResponse(response);
  }

} // namespace Server
} // namespace Envoy
