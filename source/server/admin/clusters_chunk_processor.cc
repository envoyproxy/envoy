#include "clusters_chunk_processor.h"
#include "source/server/admin/clusters_chunk_processor.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <variant>

#include "envoy/buffer/buffer.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/network/address.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/resource_manager.h"
#include "envoy/upstream/upstream.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/upstream/host_utility.h"

#include "absl/strings/string_view.h"

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

  std::vector<Json::Streamer::Map::NameValue> top_level_entries;
  if (const std::string& name = cluster_info->name(); !name.empty()) {
    top_level_entries.emplace_back("name", name);
  }
  if (const std::string& observability_name = cluster_info->observabilityName();
      !observability_name.empty()) {
    top_level_entries.emplace_back("observability_name", observability_name);
  }

  if (const std::string& eds_service_name = cluster_info->edsServiceName();
      !eds_service_name.empty()) {
    top_level_entries.emplace_back("eds_service_name", eds_service_name);
  }

  addCircuitBreakers(cluster_map.get(), cluster_info, response);
  addEjectionThresholds(cluster_map.get(), cluster.get(), response);
  if (bool added_via_api = cluster_info->addedViaApi(); added_via_api) {
    top_level_entries.emplace_back("added_via_api", added_via_api);
  }
  addMapEntries(cluster_map.get(), response, top_level_entries);
  addHostStatuses(cluster_map.get(), cluster, response);
}

void JsonClustersChunkProcessor::addHostStatuses(Json::Streamer::Map* raw_clusters_map_ptr,
                                                 const Upstream::Cluster& unwrapped_cluster,
                                                 Buffer::Instance& response) {
  raw_clusters_map_ptr->addKey("host_statuses");
  Json::Streamer::ArrayPtr host_statuses_ptr = raw_clusters_map_ptr->addArray();
  for (const Upstream::HostSetPtr& host_set :
       unwrapped_cluster.prioritySet().hostSetsPerPriority()) {
    processHostSet(host_statuses_ptr.get(), host_set, response);
  }
}

void JsonClustersChunkProcessor::processHostSet(Json::Streamer::Array* raw_host_statuses_ptr,
                                                const Upstream::HostSetPtr& host_set,
                                                Buffer::Instance& response) {
  for (const Upstream::HostSharedPtr& host : host_set->hosts()) {
    processHost(raw_host_statuses_ptr, host, response);
  }
}

void JsonClustersChunkProcessor::processHost(Json::Streamer::Array* raw_host_statuses_ptr,
                                             const Upstream::HostSharedPtr& host,
                                             Buffer::Instance& response) {
  Buffer::OwnedImpl buffer;
  Json::Streamer::MapPtr host_ptr = raw_host_statuses_ptr->addMap();
  std::vector<Json::Streamer::Map::NameValue> host_config;
  setHostname(host, host_config);
  addAddress(host_ptr.get(), host, buffer);
  setLocality(host_ptr.get(), host, buffer);
  buildHostStats(host_ptr.get(), host, buffer);
  setHealthFlags(host_ptr.get(), host, buffer);
  setSuccessRate(host_ptr.get(), host, buffer);

  if (uint64_t weight = host->weight(); weight) {
    host_config.emplace_back("weight", weight);
  }
  if (uint64_t priority = host->priority(); priority) {
    host_config.emplace_back("priority", priority);
  }

  if (buffer.length()) {
    response.move(buffer);
  }
  if (!host_config.empty()) {
    addMapEntries(host_ptr.get(), response, host_config);
  }
}

void JsonClustersChunkProcessor::setHostname(
    const Upstream::HostSharedPtr& host, std::vector<Json::Streamer::Map::NameValue>& host_config) {
  if (const std::string& hostname = host->hostname(); !hostname.empty()) {
    host_config.emplace_back("hostname", hostname);
  }
}

void JsonClustersChunkProcessor::setSuccessRate(Json::Streamer::Map* raw_host_statuses_ptr,
                                                const Upstream::HostSharedPtr& host,
                                                Buffer::Instance& response) {

  double external_success_rate = host->outlierDetector().successRate(
      Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin);
  double local_success_rate = host->outlierDetector().successRate(
      Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin);
  if (!external_success_rate && !local_success_rate) {
    return;
  }

  if (external_success_rate >= 0.0) {
    raw_host_statuses_ptr->addKey("success_rate");
    Json::Streamer::MapPtr success_rate_map = raw_host_statuses_ptr->addMap();
    std::vector<Json::Streamer::Map::NameValue> success_rate{{"value", external_success_rate}};
    addMapEntries(success_rate_map.get(), response, success_rate);
  }
  if (local_success_rate >= 0.0) {
    raw_host_statuses_ptr->addKey("local_origin_success_rate");
    Json::Streamer::MapPtr local_origin_map = raw_host_statuses_ptr->addMap();
    std::vector<Json::Streamer::Map::NameValue> local_origin_success_rate{
        {"value", local_success_rate}};
    addMapEntries(local_origin_map.get(), response, local_origin_success_rate);
  }
}

void JsonClustersChunkProcessor::setLocality(Json::Streamer::Map* raw_host_ptr,
                                             const Upstream::HostSharedPtr& host,
                                             Buffer::Instance& response) {

  if (host->locality().region().empty() && host->locality().zone().empty() &&
      host->locality().sub_zone().empty()) {
    return;
  }
  raw_host_ptr->addKey("locality");
  Json::Streamer::MapPtr locality_ptr = raw_host_ptr->addMap();
  std::vector<Json::Streamer::Map::NameValue> locality;
  if (const std::string& region = host->locality().region(); !region.empty()) {
    locality.emplace_back("region", region);
  }
  if (const std::string& zone = host->locality().zone(); !zone.empty()) {
    locality.emplace_back("zone", zone);
  }
  if (const std::string& sub_zone = host->locality().sub_zone(); !sub_zone.empty()) {
    locality.emplace_back("sub_zone", sub_zone);
  }
  addMapEntries(locality_ptr.get(), response, locality);
}

void JsonClustersChunkProcessor::addAddress(Json::Streamer::Map* raw_host_ptr,
                                            const Upstream::HostSharedPtr& host,
                                            Buffer::Instance& response) {
  switch (host->address()->type()) {
  case Network::Address::Type::Pipe: {
    if (const std::string& path = host->address()->asString(); !path.empty()) {
      raw_host_ptr->addKey("address");
      Json::Streamer::MapPtr address_ptr = raw_host_ptr->addMap();
      address_ptr->addKey("pipe");
      Json::Streamer::MapPtr pipe_ptr = address_ptr->addMap();
      std::vector<Json::Streamer::Map::NameValue> pipe{
          {"pipe", host->address()->asString()},
      };
      addMapEntries(pipe_ptr.get(), response, pipe);
    }
    break;
  }
  case Network::Address::Type::Ip: {
    if (!host->address()->ip()->addressAsString().empty() || host->address()->ip()->port()) {
      raw_host_ptr->addKey("address");
      Json::Streamer::MapPtr address_ptr = raw_host_ptr->addMap();
      address_ptr->addKey("socket_address");
      Json::Streamer::MapPtr socket_address_ptr = address_ptr->addMap();
      std::vector<Json::Streamer::Map::NameValue> socket_address;
      if (const std::string& address = host->address()->ip()->addressAsString(); !address.empty()) {
        socket_address.emplace_back("address", address);
      }
      if (uint64_t port = uint64_t(host->address()->ip()->port()); port) {
        socket_address.emplace_back("port_value", port);
      }
      addMapEntries(socket_address_ptr.get(), response, socket_address);
    }
    break;
  }
  case Network::Address::Type::EnvoyInternal: {
    if (!host->address()->envoyInternalAddress()->addressId().empty() ||
        !host->address()->envoyInternalAddress()->endpointId().empty()) {
      raw_host_ptr->addKey("address");
      Json::Streamer::MapPtr address_ptr = raw_host_ptr->addMap();
      raw_host_ptr->addKey("envoy_internal_address");
      Json::Streamer::MapPtr envoy_internal_address_ptr = raw_host_ptr->addMap();
      std::vector<Json::Streamer::Map::NameValue> envoy_internal_address;
      if (const std::string& server_listener_name =
              host->address()->envoyInternalAddress()->addressId();
          !server_listener_name.empty()) {
        envoy_internal_address.emplace_back("server_listener_name", server_listener_name);
      }
      if (const std::string& endpoint_id = host->address()->envoyInternalAddress()->endpointId();
          !endpoint_id.empty()) {
        envoy_internal_address.emplace_back("endpoint_id", endpoint_id);
      }
      addMapEntries(envoy_internal_address_ptr.get(), response, envoy_internal_address);
    }
    break;
  }
  }
}

void JsonClustersChunkProcessor::buildHostStats(Json::Streamer::Map* raw_host_ptr,
                                                const Upstream::HostSharedPtr& host,
                                                Buffer::Instance& response) {
  if (host->counters().empty() && host->gauges().empty()) {
    return;
  }
  raw_host_ptr->addKey("stats");
  Json::Streamer::ArrayPtr stats_ptr = raw_host_ptr->addArray();
  for (const auto& [counter_name, counter] : host->counters()) {
    if (counter_name.empty() || counter.get().value() == 0) {
      continue;
    }
    std::vector<Json::Streamer::Map::NameValue> counter_object{
        {"type",
         envoy::admin::v3::SimpleMetric_Type_Name(envoy::admin::v3::SimpleMetric::COUNTER)}};
    if (!counter_name.empty()) {
      counter_object.emplace_back("name", counter_name);
    }
    if (uint64_t value = counter.get().value(); value) {
      counter_object.emplace_back("value", value);
    }
    Json::Streamer::MapPtr stats_obj_ptr = stats_ptr->addMap();
    addMapEntries(stats_obj_ptr.get(), response, counter_object);
  }
  for (const auto& [gauge_name, gauge] : host->gauges()) {
    if (gauge_name.empty() || gauge.get().value() == 0) {
      continue;
    }
    std::vector<Json::Streamer::Map::NameValue> gauge_object{
        {"type", envoy::admin::v3::SimpleMetric_Type_Name(envoy::admin::v3::SimpleMetric::GAUGE)}};
    if (!gauge_name.empty()) {
      gauge_object.emplace_back("name", gauge_name);
    }
    if (uint64_t value = gauge.get().value(); value) {
      gauge_object.emplace_back("value", value);
    }
    Json::Streamer::MapPtr stats_obj_ptr = stats_ptr->addMap();
    addMapEntries(stats_obj_ptr.get(), response, gauge_object);
  }
}

void JsonClustersChunkProcessor::setHealthFlags(Json::Streamer::Map* raw_host_ptr,
                                                const Upstream::HostSharedPtr& host,
                                                Buffer::Instance& response) {
  absl::btree_map<absl::string_view, absl::variant<bool, absl::string_view>> flag_map;
  // Invokes setHealthFlag for each health flag.
#define SET_HEALTH_FLAG(name, notused)                                                             \
  loadHealthFlagMap(flag_map, Upstream::Host::HealthFlag::name, host);
  HEALTH_FLAG_ENUM_VALUES(SET_HEALTH_FLAG)
#undef SET_HEALTH_FLAG
  if (flag_map.empty()) {
    return;
  }
  raw_host_ptr->addKey("health_status");
  Json::Streamer::MapPtr health_flags_ptr = raw_host_ptr->addMap();
  std::vector<Json::Streamer::Map::NameValue> flags;
  for (const auto& [name, flag_value] : flag_map) {
    if (name == "eds_health_status") {
      if (absl::holds_alternative<absl::string_view>(flag_value)) {
        flags.emplace_back(name, std::get<absl::string_view>(flag_value));
      }
    } else {
      if (absl::holds_alternative<bool>(flag_value)) {
        flags.emplace_back(name, std::get<bool>(flag_value));
      }
    }
  }
  addMapEntries(health_flags_ptr.get(), response, flags);
}

void JsonClustersChunkProcessor::loadHealthFlagMap(
    absl::btree_map<absl::string_view, absl::variant<bool, absl::string_view>>& flag_map,
    Upstream::Host::HealthFlag flag, const Upstream::HostSharedPtr& host) {
  switch (flag) {
  case Upstream::Host::HealthFlag::FAILED_ACTIVE_HC:
    if (bool value = host.get()->healthFlagGet(flag); value) {
      flag_map.insert_or_assign("failed_active_health_check", value);
    }
    break;
  case Upstream::Host::HealthFlag::FAILED_OUTLIER_CHECK:
    if (bool value = host.get()->healthFlagGet(flag); value) {
      flag_map.insert_or_assign("failed_outlier_check", value);
    }
    break;
  case Upstream::Host::HealthFlag::DEGRADED_EDS_HEALTH:
  case Upstream::Host::HealthFlag::FAILED_EDS_HEALTH:
    if (host->healthFlagGet(Upstream::Host::HealthFlag::FAILED_EDS_HEALTH) ||
        host->healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_EDS_HEALTH)) {
      if (host->healthFlagGet(Upstream::Host::HealthFlag::FAILED_EDS_HEALTH)) {
        flag_map.insert_or_assign("eds_health_status", envoy::config::core::v3::HealthStatus_Name(
                                                           envoy::config::core::v3::UNHEALTHY));
      } else {
        flag_map.insert_or_assign("eds_health_status", envoy::config::core::v3::HealthStatus_Name(
                                                           envoy::config::core::v3::DEGRADED));
      }
    } else {
      flag_map.insert_or_assign("eds_health_status", envoy::config::core::v3::HealthStatus_Name(
                                                         envoy::config::core::v3::HEALTHY));
    }
    break;
  case Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC:
    if (bool value = host.get()->healthFlagGet(flag); value) {
      flag_map.insert_or_assign("failed_active_degraded_check", value);
    }
    break;
  case Upstream::Host::HealthFlag::PENDING_DYNAMIC_REMOVAL:
    if (bool value = host.get()->healthFlagGet(flag); value) {
      flag_map.insert_or_assign("pending_dynamic_removal", value);
    }
    break;
  case Upstream::Host::HealthFlag::PENDING_ACTIVE_HC:
    if (bool value = host.get()->healthFlagGet(flag); value) {
      flag_map.insert_or_assign("pending_active_hc", value);
    }
    break;
  case Upstream::Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL:
    if (bool value = host.get()->healthFlagGet(flag); value) {
      flag_map.insert_or_assign("excluded_via_immediate_hc_fail", value);
    }
    break;
  case Upstream::Host::HealthFlag::ACTIVE_HC_TIMEOUT:
    if (bool value = host.get()->healthFlagGet(flag); value) {
      flag_map.insert_or_assign("active_hc_timeout", value);
    }
    break;
  case Upstream::Host::HealthFlag::EDS_STATUS_DRAINING:
    if (bool value = host.get()->healthFlagGet(flag); value) {
      flag_map.insert_or_assign("eds_health_status", envoy::config::core::v3::HealthStatus_Name(
                                                         envoy::config::core::v3::DRAINING));
    }
    break;
  }
}

void JsonClustersChunkProcessor::addEjectionThresholds(Json::Streamer::Map* raw_clusters_map_ptr,
                                                       const Upstream::Cluster& unwrapped_cluster,
                                                       Buffer::Instance& response) {
  const Upstream::Outlier::Detector* outlier_detector = unwrapped_cluster.outlierDetector();
  if (outlier_detector != nullptr &&
      outlier_detector->successRateEjectionThreshold(
          Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin) > 0.0) {
    raw_clusters_map_ptr->addKey("success_rate_ejection_threshold");
    Json::Streamer::MapPtr success_rate_map_ptr = raw_clusters_map_ptr->addMap();
    std::vector<Json::Streamer::Map::NameValue> success_rate_ejection_threshold;
    success_rate_ejection_threshold.emplace_back(
        "value",
        double(outlier_detector->successRateEjectionThreshold(
            Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin)));
    addMapEntries(success_rate_map_ptr.get(), response, success_rate_ejection_threshold);
  }
  if (outlier_detector != nullptr &&
      outlier_detector->successRateEjectionThreshold(
          Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin) > 0.0) {
    raw_clusters_map_ptr->addKey("local_origin_success_rate_ejection_threshold");
    Json::Streamer::MapPtr local_success_rate_map_ptr = raw_clusters_map_ptr->addMap();
    std::vector<Json::Streamer::Map::NameValue> local_origin_success_rate_ejection_threshold;
    local_origin_success_rate_ejection_threshold.emplace_back(
        "value", double(outlier_detector->successRateEjectionThreshold(
                     Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin)));
    addMapEntries(local_success_rate_map_ptr.get(), response,
                  local_origin_success_rate_ejection_threshold);
  }
}

void JsonClustersChunkProcessor::addCircuitBreakers(
    Json::Streamer::Map* raw_clusters_map_ptr, Upstream::ClusterInfoConstSharedPtr cluster_info,
    Buffer::Instance& response) {
  raw_clusters_map_ptr->addKey("circuit_breakers");
  Json::Streamer::MapPtr circuit_breakers = raw_clusters_map_ptr->addMap();
  circuit_breakers->addKey("thresholds");
  Json::Streamer::ArrayPtr thresholds = circuit_breakers->addArray();
  addCircuitBreakerForPriority(envoy::config::core::v3::RoutingPriority::DEFAULT, thresholds.get(),
                               response,
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
  std::vector<Json::Streamer::Map::NameValue> entries{
      {"priority",
       priority == envoy::config::core::v3::RoutingPriority::DEFAULT ? "DEFAULT" : "HIGH"}};
  if (uint64_t max_connections = resource_manager.connections().max(); max_connections) {
    entries.emplace_back("max_connections", max_connections);
  }
  if (uint64_t max_pending_requests = resource_manager.pendingRequests().max();
      max_pending_requests) {
    entries.emplace_back("max_pending_requests", max_pending_requests);
  }
  if (uint64_t max_requests = resource_manager.requests().max(); max_requests) {
    entries.emplace_back("max_requests", max_requests);
  }
  if (uint64_t max_retries = resource_manager.retries().max(); max_retries) {
    entries.emplace_back("max_retries", max_retries);
  }
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
    std::vector<Json::Streamer::Map::NameValue>& entries) {
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
