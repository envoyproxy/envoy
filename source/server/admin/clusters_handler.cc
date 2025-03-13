#include "source/server/admin/clusters_handler.h"

#include "envoy/admin/v3/clusters.pb.h"

#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/network/utility.h"
#include "source/common/upstream/host_utility.h"
#include "source/server/admin/utils.h"

namespace Envoy {
namespace Server {

namespace {

void addCircuitBreakerSettingsAsText(const std::string& cluster_name,
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

void addCircuitBreakerSettingsAsJson(const envoy::config::core::v3::RoutingPriority& priority,
                                     Upstream::ResourceManager& resource_manager,
                                     envoy::admin::v3::ClusterStatus& cluster_status) {
  auto& thresholds = *cluster_status.mutable_circuit_breakers()->add_thresholds();
  thresholds.set_priority(priority);
  thresholds.mutable_max_connections()->set_value(resource_manager.connections().max());
  thresholds.mutable_max_pending_requests()->set_value(resource_manager.pendingRequests().max());
  thresholds.mutable_max_requests()->set_value(resource_manager.requests().max());
  thresholds.mutable_max_retries()->set_value(resource_manager.retries().max());
}

} // namespace

ClustersHandler::ClustersHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code ClustersHandler::handlerClusters(Http::ResponseHeaderMap& response_headers,
                                            Buffer::Instance& response, AdminStream& admin_stream) {
  const auto format_value = Utility::formatParam(admin_stream.queryParams());

  if (format_value.has_value() && format_value.value() == "json") {
    writeClustersAsJson(response);
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  } else {
    writeClustersAsText(response);
  }

  return Http::Code::OK;
}

// Helper method that ensures that we've setting flags based on all the health flag values on the
// host.
void setHealthFlag(Upstream::Host::HealthFlag flag, const Upstream::Host& host,
                   envoy::admin::v3::HostHealthStatus& health_status) {
  switch (flag) {
  case Upstream::Host::HealthFlag::FAILED_ACTIVE_HC:
    health_status.set_failed_active_health_check(
        host.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
    break;
  case Upstream::Host::HealthFlag::FAILED_OUTLIER_CHECK:
    health_status.set_failed_outlier_check(
        host.healthFlagGet(Upstream::Host::HealthFlag::FAILED_OUTLIER_CHECK));
    break;
  case Upstream::Host::HealthFlag::FAILED_EDS_HEALTH:
  case Upstream::Host::HealthFlag::DEGRADED_EDS_HEALTH:
    if (host.healthFlagGet(Upstream::Host::HealthFlag::FAILED_EDS_HEALTH)) {
      health_status.set_eds_health_status(envoy::config::core::v3::UNHEALTHY);
    } else if (host.healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_EDS_HEALTH)) {
      health_status.set_eds_health_status(envoy::config::core::v3::DEGRADED);
    } else {
      health_status.set_eds_health_status(envoy::config::core::v3::HEALTHY);
    }
    break;
  case Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC:
    health_status.set_failed_active_degraded_check(
        host.healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC));
    break;
  case Upstream::Host::HealthFlag::PENDING_DYNAMIC_REMOVAL:
    health_status.set_pending_dynamic_removal(
        host.healthFlagGet(Upstream::Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
    break;
  case Upstream::Host::HealthFlag::PENDING_ACTIVE_HC:
    health_status.set_pending_active_hc(
        host.healthFlagGet(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC));
    break;
  case Upstream::Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL:
    health_status.set_excluded_via_immediate_hc_fail(
        host.healthFlagGet(Upstream::Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL));
    break;
  case Upstream::Host::HealthFlag::ACTIVE_HC_TIMEOUT:
    health_status.set_active_hc_timeout(
        host.healthFlagGet(Upstream::Host::HealthFlag::ACTIVE_HC_TIMEOUT));
    break;
  case Upstream::Host::HealthFlag::EDS_STATUS_DRAINING:
    if (host.healthFlagGet(Upstream::Host::HealthFlag::EDS_STATUS_DRAINING)) {
      health_status.set_eds_health_status(envoy::config::core::v3::DRAINING);
    }
    break;
  }
}

// TODO(efimki): Add support of text readouts stats.
void ClustersHandler::writeClustersAsJson(Buffer::Instance& response) {
  envoy::admin::v3::Clusters clusters;
  // TODO(mattklein123): Add ability to see warming clusters in admin output.
  auto all_clusters = server_.clusterManager().clusters();
  for (const auto& [name, cluster_ref] : all_clusters.active_clusters_) {
    UNREFERENCED_PARAMETER(name);
    const Upstream::Cluster& cluster = cluster_ref.get();
    Upstream::ClusterInfoConstSharedPtr cluster_info = cluster.info();

    envoy::admin::v3::ClusterStatus& cluster_status = *clusters.add_cluster_statuses();
    cluster_status.set_name(cluster_info->name());
    cluster_status.set_observability_name(cluster_info->observabilityName());
    if (const auto& name = cluster_info->edsServiceName(); !name.empty()) {
      cluster_status.set_eds_service_name(name);
    }
    addCircuitBreakerSettingsAsJson(
        envoy::config::core::v3::RoutingPriority::DEFAULT,
        cluster.info()->resourceManager(Upstream::ResourcePriority::Default), cluster_status);
    addCircuitBreakerSettingsAsJson(
        envoy::config::core::v3::RoutingPriority::HIGH,
        cluster.info()->resourceManager(Upstream::ResourcePriority::High), cluster_status);

    const Upstream::Outlier::Detector* outlier_detector = cluster.outlierDetector();
    if (outlier_detector != nullptr &&
        outlier_detector->successRateEjectionThreshold(
            Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin) > 0.0) {
      cluster_status.mutable_success_rate_ejection_threshold()->set_value(
          outlier_detector->successRateEjectionThreshold(
              Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin));
    }
    if (outlier_detector != nullptr &&
        outlier_detector->successRateEjectionThreshold(
            Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin) > 0.0) {
      cluster_status.mutable_local_origin_success_rate_ejection_threshold()->set_value(
          outlier_detector->successRateEjectionThreshold(
              Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin));
    }

    cluster_status.set_added_via_api(cluster_info->addedViaApi());

    for (auto& host_set : cluster.prioritySet().hostSetsPerPriority()) {
      for (auto& host : host_set->hosts()) {
        envoy::admin::v3::HostStatus& host_status = *cluster_status.add_host_statuses();
        Network::Utility::addressToProtobufAddress(*host->address(),
                                                   *host_status.mutable_address());
        host_status.set_hostname(host->hostname());
        host_status.mutable_locality()->MergeFrom(host->locality());

        for (const auto& [counter_name, counter] : host->counters()) {
          auto& metric = *host_status.add_stats();
          metric.set_name(std::string(counter_name));
          metric.set_value(counter.get().value());
          metric.set_type(envoy::admin::v3::SimpleMetric::COUNTER);
        }

        for (const auto& [gauge_name, gauge] : host->gauges()) {
          auto& metric = *host_status.add_stats();
          metric.set_name(std::string(gauge_name));
          metric.set_value(gauge.get().value());
          metric.set_type(envoy::admin::v3::SimpleMetric::GAUGE);
        }

        envoy::admin::v3::HostHealthStatus& health_status = *host_status.mutable_health_status();

// Invokes setHealthFlag for each health flag.
#define SET_HEALTH_FLAG(name, notused)                                                             \
  setHealthFlag(Upstream::Host::HealthFlag::name, *host, health_status);
        HEALTH_FLAG_ENUM_VALUES(SET_HEALTH_FLAG)
#undef SET_HEALTH_FLAG

        double success_rate = host->outlierDetector().successRate(
            Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin);
        if (success_rate >= 0.0) {
          host_status.mutable_success_rate()->set_value(success_rate);
        }

        host_status.set_weight(host->weight());

        host_status.set_priority(host->priority());
        success_rate = host->outlierDetector().successRate(
            Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin);
        if (success_rate >= 0.0) {
          host_status.mutable_local_origin_success_rate()->set_value(success_rate);
        }
      }
    }
  }
  response.add(MessageUtil::getJsonStringFromMessageOrError(clusters, true)); // pretty-print
}

// TODO(efimki): Add support of text readouts stats.
void ClustersHandler::writeClustersAsText(Buffer::Instance& response) {
  // TODO(mattklein123): Add ability to see warming clusters in admin output.
  auto all_clusters = server_.clusterManager().clusters();
  for (const auto& [name, cluster_ref] : all_clusters.active_clusters_) {
    UNREFERENCED_PARAMETER(name);
    const Upstream::Cluster& cluster = cluster_ref.get();
    const std::string& cluster_name = cluster.info()->name();
    response.add(fmt::format("{}::observability_name::{}\n", cluster_name,
                             cluster.info()->observabilityName()));
    addOutlierInfo(cluster_name, cluster.outlierDetector(), response);

    addCircuitBreakerSettingsAsText(
        cluster_name, "default",
        cluster.info()->resourceManager(Upstream::ResourcePriority::Default), response);
    addCircuitBreakerSettingsAsText(
        cluster_name, "high", cluster.info()->resourceManager(Upstream::ResourcePriority::High),
        response);

    response.add(
        fmt::format("{}::added_via_api::{}\n", cluster_name, cluster.info()->addedViaApi()));
    if (const auto& name = cluster.info()->edsServiceName(); !name.empty()) {
      response.add(fmt::format("{}::eds_service_name::{}\n", cluster_name, name));
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
          response.add(
              fmt::format("{}::{}::{}::{}\n", cluster_name, host_address, stat_name, stat));
        }

        response.add(
            fmt::format("{}::{}::hostname::{}\n", cluster_name, host_address, host->hostname()));
        response.add(fmt::format("{}::{}::health_flags::{}\n", cluster_name, host_address,
                                 Upstream::HostUtility::healthFlagsToString(*host)));
        response.add(
            fmt::format("{}::{}::weight::{}\n", cluster_name, host_address, host->weight()));
        response.add(fmt::format("{}::{}::region::{}\n", cluster_name, host_address,
                                 host->locality().region()));
        response.add(
            fmt::format("{}::{}::zone::{}\n", cluster_name, host_address, host->locality().zone()));
        response.add(fmt::format("{}::{}::sub_zone::{}\n", cluster_name, host_address,
                                 host->locality().sub_zone()));
        response.add(
            fmt::format("{}::{}::canary::{}\n", cluster_name, host_address, host->canary()));
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
}

void ClustersHandler::addOutlierInfo(const std::string& cluster_name,
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

} // namespace Server
} // namespace Envoy
