#include "server/admin/admin.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "envoy/admin/v3/certs.pb.h"
#include "envoy/admin/v3/clusters.pb.h"
#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/admin/v3/metrics.pb.h"
#include "envoy/admin/v3/server_info.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/server/hot_restart.h"
#include "envoy/server/instance.h"
#include "envoy/server/options.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/upstream.h"

#include "common/access_log/access_log_impl.h"
#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/common/mutex_tracer_impl.h"
#include "common/common/utility.h"
#include "common/formatter/substitution_formatter.h"
#include "common/html/utility.h"
#include "common/http/codes.h"
#include "common/http/conn_manager_utility.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/memory/utils.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/router/config_impl.h"
#include "common/upstream/host_utility.h"

#include "server/admin/utils.h"

#include "extensions/access_loggers/file/file_access_log_impl.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {

namespace {

/**
 * Favicon base64 image was harvested by screen-capturing the favicon from a Chrome tab
 * while visiting https://www.envoyproxy.io/. The resulting PNG was translated to base64
 * by dropping it into https://www.base64-image.de/ and then pasting the resulting string
 * below.
 *
 * The actual favicon source for that, https://www.envoyproxy.io/img/favicon.ico is nicer
 * because it's transparent, but is also 67646 bytes, which is annoying to inline. We could
 * just reference that rather than inlining it, but then the favicon won't work when visiting
 * the admin page from a network that can't see the internet.
 */
const char EnvoyFavicon[] =
    "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABgAAAAYCAYAAADgdz34AAAAAXNSR0IArs4c6QAAAARnQU1"
    "BAACxjwv8YQUAAAAJcEhZcwAAEnQAABJ0Ad5mH3gAAAH9SURBVEhL7ZRdTttAFIUrUFaAX5w9gIhgUfzshFRK+gIbaVbA"
    "zwaqCly1dSpKk5A485/YCdXpHTB4BsdgVe0bD0cZ3Xsm38yZ8byTUuJ/6g3wqqoBrBhPTzmmLfptMbAzttJTpTKAF2MWC"
    "7ADCdNIwXZpvMMwayiIwwS874CcOc9VuQPR1dBBChPMITpFXXU45hukIIH6kHhzVqkEYB8F5HYGvZ5B7EvwmHt9K/59Cr"
    "U3QbY2RNYaQPYmJc+jPIBICNCcg20ZsAsCPfbcrFlRF+cJZpvXSJt9yMTxO/IAzJrCOfhJXiOgFEX/SbZmezTWxyNk4Q9"
    "anHMmjnzAhEyhAW8LCE6wl26J7ZFHH1FMYQxh567weQBOO1AW8D7P/UXAQySq/QvL8Fu9HfCEw4SKALm5BkC3bwjwhSKr"
    "A5hYAMXTJnPNiMyRBVzVjcgCyHiSm+8P+WGlnmwtP2RzbCMiQJ0d2KtmmmPorRHEhfMROVfTG5/fYrF5iWXzE80tfy9WP"
    "sCqx5Buj7FYH0LvDyHiqd+3otpsr4/fa5+xbEVQPfrYnntylQG5VGeMLBhgEfyE7o6e6qYzwHIjwl0QwXSvvTmrVAY4D5"
    "ddvT64wV0jRrr7FekO/XEjwuwwhuw7Ef7NY+dlfXpLb06EtHUJdVbsxvNUqBrwj/QGeEUSfwBAkmWHn5Bb/gAAAABJRU5"
    "ErkJggg==";

const char AdminHtmlStart[] = R"(
<head>
  <title>Envoy Admin</title>
  <link rel='shortcut icon' type='image/png' href='@FAVICON@'/>
  <style>
    .home-table {
      font-family: sans-serif;
      font-size: medium;
      border-collapse: collapse;
    }

    .home-row:nth-child(even) {
      background-color: #dddddd;
    }

    .home-data {
      border: 1px solid #dddddd;
      text-align: left;
      padding: 8px;
    }

    .home-form {
      margin-bottom: 0;
    }
  </style>
</head>
<body>
  <table class='home-table'>
    <thead>
      <th class='home-data'>Command</th>
      <th class='home-data'>Description</th>
     </thead>
     <tbody>
)";

const char AdminHtmlEnd[] = R"(
    </tbody>
  </table>
</body>
)";

// Helper method to get the resource parameter.
absl::optional<std::string> resourceParam(const Http::Utility::QueryParams& params) {
  return Utility::queryParam(params, "resource");
}

// Helper method to get the mask parameter.
absl::optional<std::string> maskParam(const Http::Utility::QueryParams& params) {
  return Utility::queryParam(params, "mask");
}

// Helper method to get the eds parameter.
bool shouldIncludeEdsInDump(const Http::Utility::QueryParams& params) {
  return Utility::queryParam(params, "include_eds") != absl::nullopt;
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
  }
}

// Apply a field mask to a resource message. A simple field mask might look
// like "cluster.name,cluster.alt_stat_name,last_updated" for a StaticCluster
// resource. Unfortunately, since the "cluster" field is Any and the in-built
// FieldMask utils can't mask inside an Any field, we need to do additional work
// below.
//
// We take advantage of the fact that for the most part (with the exception of
// DynamicListener) that ConfigDump resources have a single Any field where the
// embedded resources lives. This allows us to construct an inner field mask for
// the Any resource and an outer field mask for the enclosing message. In the
// above example, the inner field mask would be "name,alt_stat_name" and the
// outer field mask "cluster,last_updated". The masks are applied to their
// respective messages, with the Any resource requiring an unpack/mask/pack
// series of operations.
//
// TODO(htuch): we could make field masks more powerful in future and generalize
// this to allow arbitrary indexing through Any fields. This is pretty
// complicated, we would need to build a FieldMask tree similar to how the C++
// Protobuf library does this internally.
void trimResourceMessage(const Protobuf::FieldMask& field_mask, Protobuf::Message& message) {
  const Protobuf::Descriptor* descriptor = message.GetDescriptor();
  const Protobuf::Reflection* reflection = message.GetReflection();
  // Figure out which paths cover Any fields. For each field, gather the paths to
  // an inner mask, switch the outer mask to cover only the original field.
  Protobuf::FieldMask outer_field_mask;
  Protobuf::FieldMask inner_field_mask;
  std::string any_field_name;
  for (int i = 0; i < field_mask.paths().size(); ++i) {
    const std::string& path = field_mask.paths(i);
    std::vector<std::string> frags = absl::StrSplit(path, '.');
    if (frags.empty()) {
      continue;
    }
    const Protobuf::FieldDescriptor* field = descriptor->FindFieldByName(frags[0]);
    // Only a single Any field supported, repeated fields don't support further
    // indexing.
    // TODO(htuch): should add support for DynamicListener for multiple Any
    // fields in the future, see
    // https://github.com/envoyproxy/envoy/issues/9669.
    if (field != nullptr && field->message_type() != nullptr && !field->is_repeated() &&
        field->message_type()->full_name() == "google.protobuf.Any") {
      if (any_field_name.empty()) {
        any_field_name = frags[0];
      } else {
        // This should be structurally true due to the ConfigDump proto
        // definition (but not for DynamicListener today).
        ASSERT(any_field_name == frags[0],
               "Only a single Any field in a config dump resource is supported.");
      }
      outer_field_mask.add_paths(frags[0]);
      frags.erase(frags.begin());
      inner_field_mask.add_paths(absl::StrJoin(frags, "."));
    } else {
      outer_field_mask.add_paths(path);
    }
  }

  if (!any_field_name.empty()) {
    const Protobuf::FieldDescriptor* any_field = descriptor->FindFieldByName(any_field_name);
    if (reflection->HasField(message, any_field)) {
      ASSERT(any_field != nullptr);
      // Unpack to a DynamicMessage.
      ProtobufWkt::Any any_message;
      any_message.MergeFrom(reflection->GetMessage(message, any_field));
      Protobuf::DynamicMessageFactory dmf;
      const absl::string_view inner_type_name =
          TypeUtil::typeUrlToDescriptorFullName(any_message.type_url());
      const Protobuf::Descriptor* inner_descriptor =
          Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
              static_cast<std::string>(inner_type_name));
      ASSERT(inner_descriptor != nullptr);
      std::unique_ptr<Protobuf::Message> inner_message;
      inner_message.reset(dmf.GetPrototype(inner_descriptor)->New());
      MessageUtil::unpackTo(any_message, *inner_message);
      // Trim message.
      ProtobufUtil::FieldMaskUtil::TrimMessage(inner_field_mask, inner_message.get());
      // Pack it back into the Any resource.
      any_message.PackFrom(*inner_message);
      reflection->MutableMessage(&message, any_field)->CopyFrom(any_message);
    }
  }
  ProtobufUtil::FieldMaskUtil::TrimMessage(outer_field_mask, &message);
}

} // namespace

void AdminImpl::addOutlierInfo(const std::string& cluster_name,
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

void AdminImpl::addCircuitSettings(const std::string& cluster_name, const std::string& priority_str,
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

// TODO(efimki): Add support of text readouts stats.
void AdminImpl::writeClustersAsJson(Buffer::Instance& response) {
  envoy::admin::v3::Clusters clusters;
  for (auto& cluster_pair : server_.clusterManager().clusters()) {
    const Upstream::Cluster& cluster = cluster_pair.second.get();
    Upstream::ClusterInfoConstSharedPtr cluster_info = cluster.info();

    envoy::admin::v3::ClusterStatus& cluster_status = *clusters.add_cluster_statuses();
    cluster_status.set_name(cluster_info->name());

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

        for (const auto& named_counter : host->counters()) {
          auto& metric = *host_status.add_stats();
          metric.set_name(std::string(named_counter.first));
          metric.set_value(named_counter.second.get().value());
          metric.set_type(envoy::admin::v3::SimpleMetric::COUNTER);
        }

        for (const auto& named_gauge : host->gauges()) {
          auto& metric = *host_status.add_stats();
          metric.set_name(std::string(named_gauge.first));
          metric.set_value(named_gauge.second.get().value());
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
  response.add(MessageUtil::getJsonStringFromMessage(clusters, true)); // pretty-print
}

// TODO(efimki): Add support of text readouts stats.
void AdminImpl::writeClustersAsText(Buffer::Instance& response) {
  for (auto& cluster : server_.clusterManager().clusters()) {
    addOutlierInfo(cluster.second.get().info()->name(), cluster.second.get().outlierDetector(),
                   response);

    addCircuitSettings(
        cluster.second.get().info()->name(), "default",
        cluster.second.get().info()->resourceManager(Upstream::ResourcePriority::Default),
        response);
    addCircuitSettings(
        cluster.second.get().info()->name(), "high",
        cluster.second.get().info()->resourceManager(Upstream::ResourcePriority::High), response);

    response.add(fmt::format("{}::added_via_api::{}\n", cluster.second.get().info()->name(),
                             cluster.second.get().info()->addedViaApi()));
    for (auto& host_set : cluster.second.get().prioritySet().hostSetsPerPriority()) {
      for (auto& host : host_set->hosts()) {
        std::map<absl::string_view, uint64_t> all_stats;
        for (const auto& counter : host->counters()) {
          all_stats[counter.first] = counter.second.get().value();
        }

        for (const auto& gauge : host->gauges()) {
          all_stats[gauge.first] = gauge.second.get().value();
        }

        for (const auto& stat : all_stats) {
          response.add(fmt::format("{}::{}::{}::{}\n", cluster.second.get().info()->name(),
                                   host->address()->asString(), stat.first, stat.second));
        }

        response.add(fmt::format("{}::{}::hostname::{}\n", cluster.second.get().info()->name(),
                                 host->address()->asString(), host->hostname()));
        response.add(fmt::format("{}::{}::health_flags::{}\n", cluster.second.get().info()->name(),
                                 host->address()->asString(),
                                 Upstream::HostUtility::healthFlagsToString(*host)));
        response.add(fmt::format("{}::{}::weight::{}\n", cluster.second.get().info()->name(),
                                 host->address()->asString(), host->weight()));
        response.add(fmt::format("{}::{}::region::{}\n", cluster.second.get().info()->name(),
                                 host->address()->asString(), host->locality().region()));
        response.add(fmt::format("{}::{}::zone::{}\n", cluster.second.get().info()->name(),
                                 host->address()->asString(), host->locality().zone()));
        response.add(fmt::format("{}::{}::sub_zone::{}\n", cluster.second.get().info()->name(),
                                 host->address()->asString(), host->locality().sub_zone()));
        response.add(fmt::format("{}::{}::canary::{}\n", cluster.second.get().info()->name(),
                                 host->address()->asString(), host->canary()));
        response.add(fmt::format("{}::{}::priority::{}\n", cluster.second.get().info()->name(),
                                 host->address()->asString(), host->priority()));
        response.add(fmt::format(
            "{}::{}::success_rate::{}\n", cluster.second.get().info()->name(),
            host->address()->asString(),
            host->outlierDetector().successRate(
                Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin)));
        response.add(fmt::format(
            "{}::{}::local_origin_success_rate::{}\n", cluster.second.get().info()->name(),
            host->address()->asString(),
            host->outlierDetector().successRate(
                Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin)));
      }
    }
  }
}

Http::Code AdminImpl::handlerClusters(absl::string_view url,
                                      Http::ResponseHeaderMap& response_headers,
                                      Buffer::Instance& response, AdminStream&) {
  Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);
  const auto format_value = Utility::formatParam(query_params);

  if (format_value.has_value() && format_value.value() == "json") {
    writeClustersAsJson(response);
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  } else {
    writeClustersAsText(response);
  }

  return Http::Code::OK;
}

void AdminImpl::addAllConfigToDump(envoy::admin::v3::ConfigDump& dump,
                                   const absl::optional<std::string>& mask,
                                   bool include_eds) const {
  Envoy::Server::ConfigTracker::CbsMap callbacks_map = config_tracker_.getCallbacksMap();
  if (include_eds) {
    if (!server_.clusterManager().clusters().empty()) {
      callbacks_map.emplace("endpoint", [this] { return dumpEndpointConfigs(); });
    }
  }

  for (const auto& key_callback_pair : callbacks_map) {
    ProtobufTypes::MessagePtr message = key_callback_pair.second();
    ASSERT(message);

    if (mask.has_value()) {
      Protobuf::FieldMask field_mask;
      ProtobufUtil::FieldMaskUtil::FromString(mask.value(), &field_mask);
      // We don't use trimMessage() above here since masks don't support
      // indexing through repeated fields.
      ProtobufUtil::FieldMaskUtil::TrimMessage(field_mask, message.get());
    }

    auto* config = dump.add_configs();
    config->PackFrom(*message);
  }
}

absl::optional<std::pair<Http::Code, std::string>>
AdminImpl::addResourceToDump(envoy::admin::v3::ConfigDump& dump,
                             const absl::optional<std::string>& mask, const std::string& resource,
                             bool include_eds) const {
  Envoy::Server::ConfigTracker::CbsMap callbacks_map = config_tracker_.getCallbacksMap();
  if (include_eds) {
    if (!server_.clusterManager().clusters().empty()) {
      callbacks_map.emplace("endpoint", [this] { return dumpEndpointConfigs(); });
    }
  }

  for (const auto& key_callback_pair : callbacks_map) {
    ProtobufTypes::MessagePtr message = key_callback_pair.second();
    ASSERT(message);

    auto field_descriptor = message->GetDescriptor()->FindFieldByName(resource);
    const Protobuf::Reflection* reflection = message->GetReflection();
    if (!field_descriptor) {
      continue;
    } else if (!field_descriptor->is_repeated()) {
      return absl::optional<std::pair<Http::Code, std::string>>{std::make_pair(
          Http::Code::BadRequest,
          fmt::format("{} is not a repeated field. Use ?mask={} to get only this field",
                      field_descriptor->name(), field_descriptor->name()))};
    }

    auto repeated = reflection->GetRepeatedPtrField<Protobuf::Message>(*message, field_descriptor);
    for (Protobuf::Message& msg : repeated) {
      if (mask.has_value()) {
        Protobuf::FieldMask field_mask;
        ProtobufUtil::FieldMaskUtil::FromString(mask.value(), &field_mask);
        trimResourceMessage(field_mask, msg);
      }
      auto* config = dump.add_configs();
      config->PackFrom(msg);
    }

    // We found the desired resource so there is no need to continue iterating over
    // the other keys.
    return absl::nullopt;
  }

  return absl::optional<std::pair<Http::Code, std::string>>{
      std::make_pair(Http::Code::NotFound, fmt::format("{} not found in config dump", resource))};
}

void AdminImpl::addLbEndpoint(
    const Upstream::HostSharedPtr& host,
    envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint) const {
  auto& lb_endpoint = *locality_lb_endpoint.mutable_lb_endpoints()->Add();
  if (host->metadata() != nullptr) {
    lb_endpoint.mutable_metadata()->MergeFrom(*host->metadata());
  }
  lb_endpoint.mutable_load_balancing_weight()->set_value(host->weight());

  switch (host->health()) {
  case Upstream::Host::Health::Healthy:
    lb_endpoint.set_health_status(envoy::config::core::v3::HealthStatus::HEALTHY);
    break;
  case Upstream::Host::Health::Unhealthy:
    lb_endpoint.set_health_status(envoy::config::core::v3::HealthStatus::UNHEALTHY);
    break;
  case Upstream::Host::Health::Degraded:
    lb_endpoint.set_health_status(envoy::config::core::v3::HealthStatus::DEGRADED);
    break;
  default:
    lb_endpoint.set_health_status(envoy::config::core::v3::HealthStatus::UNKNOWN);
  }

  auto& endpoint = *lb_endpoint.mutable_endpoint();
  endpoint.set_hostname(host->hostname());
  Network::Utility::addressToProtobufAddress(*host->address(), *endpoint.mutable_address());
  auto& health_check_config = *endpoint.mutable_health_check_config();
  health_check_config.set_hostname(host->hostnameForHealthChecks());
  if (host->healthCheckAddress()->asString() != host->address()->asString()) {
    health_check_config.set_port_value(host->healthCheckAddress()->ip()->port());
  }
}

ProtobufTypes::MessagePtr AdminImpl::dumpEndpointConfigs() const {
  auto endpoint_config_dump = std::make_unique<envoy::admin::v3::EndpointsConfigDump>();

  for (auto& cluster_pair : server_.clusterManager().clusters()) {
    const Upstream::Cluster& cluster = cluster_pair.second.get();
    Upstream::ClusterInfoConstSharedPtr cluster_info = cluster.info();
    envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;

    if (cluster_info->edsServiceName().has_value()) {
      cluster_load_assignment.set_cluster_name(cluster_info->edsServiceName().value());
    } else {
      cluster_load_assignment.set_cluster_name(cluster_info->name());
    }
    auto& policy = *cluster_load_assignment.mutable_policy();

    for (auto& host_set : cluster.prioritySet().hostSetsPerPriority()) {
      policy.mutable_overprovisioning_factor()->set_value(host_set->overprovisioningFactor());

      if (!host_set->hostsPerLocality().get().empty()) {
        for (int index = 0; index < static_cast<int>(host_set->hostsPerLocality().get().size());
             index++) {
          auto locality_host_set = host_set->hostsPerLocality().get()[index];

          if (!locality_host_set.empty()) {
            auto& locality_lb_endpoint = *cluster_load_assignment.mutable_endpoints()->Add();
            locality_lb_endpoint.mutable_locality()->MergeFrom(locality_host_set[0]->locality());
            locality_lb_endpoint.set_priority(locality_host_set[0]->priority());
            if (host_set->localityWeights() != nullptr && !host_set->localityWeights()->empty()) {
              locality_lb_endpoint.mutable_load_balancing_weight()->set_value(
                  (*host_set->localityWeights())[index]);
            }

            for (auto& host : locality_host_set) {
              addLbEndpoint(host, locality_lb_endpoint);
            }
          }
        }
      } else {
        for (auto& host : host_set->hosts()) {
          auto& locality_lb_endpoint = *cluster_load_assignment.mutable_endpoints()->Add();
          locality_lb_endpoint.mutable_locality()->MergeFrom(host->locality());
          locality_lb_endpoint.set_priority(host->priority());
          addLbEndpoint(host, locality_lb_endpoint);
        }
      }
    }

    if (cluster_info->addedViaApi()) {
      auto& dynamic_endpoint = *endpoint_config_dump->mutable_dynamic_endpoint_configs()->Add();
      dynamic_endpoint.mutable_endpoint_config()->PackFrom(cluster_load_assignment);
    } else {
      auto& static_endpoint = *endpoint_config_dump->mutable_static_endpoint_configs()->Add();
      static_endpoint.mutable_endpoint_config()->PackFrom(cluster_load_assignment);
    }
  }
  return endpoint_config_dump;
}

Http::Code AdminImpl::handlerConfigDump(absl::string_view url,
                                        Http::ResponseHeaderMap& response_headers,
                                        Buffer::Instance& response, AdminStream&) const {
  Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);
  const auto resource = resourceParam(query_params);
  const auto mask = maskParam(query_params);
  const bool include_eds = shouldIncludeEdsInDump(query_params);

  envoy::admin::v3::ConfigDump dump;

  if (resource.has_value()) {
    auto err = addResourceToDump(dump, mask, resource.value(), include_eds);
    if (err.has_value()) {
      response.add(err.value().second);
      return err.value().first;
    }
  } else {
    addAllConfigToDump(dump, mask, include_eds);
  }
  MessageUtil::redact(dump);

  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  response.add(MessageUtil::getJsonStringFromMessage(dump, true)); // pretty-print
  return Http::Code::OK;
}

ConfigTracker& AdminImpl::getConfigTracker() { return config_tracker_; }

AdminImpl::NullRouteConfigProvider::NullRouteConfigProvider(TimeSource& time_source)
    : config_(new Router::NullConfigImpl()), time_source_(time_source) {}

void AdminImpl::startHttpListener(const std::string& access_log_path,
                                  const std::string& address_out_path,
                                  Network::Address::InstanceConstSharedPtr address,
                                  const Network::Socket::OptionsSharedPtr& socket_options,
                                  Stats::ScopePtr&& listener_scope) {
  // TODO(mattklein123): Allow admin to use normal access logger extension loading and avoid the
  // hard dependency here.
  access_logs_.emplace_back(new Extensions::AccessLoggers::File::FileAccessLog(
      access_log_path, {}, Formatter::SubstitutionFormatUtils::defaultSubstitutionFormatter(),
      server_.accessLogManager()));
  socket_ = std::make_shared<Network::TcpListenSocket>(address, socket_options, true);
  socket_factory_ = std::make_shared<AdminListenSocketFactory>(socket_);
  listener_ = std::make_unique<AdminListener>(*this, std::move(listener_scope));
  if (!address_out_path.empty()) {
    std::ofstream address_out_file(address_out_path);
    if (!address_out_file) {
      ENVOY_LOG(critical, "cannot open admin address output file {} for writing.",
                address_out_path);
    } else {
      address_out_file << socket_->localAddress()->asString();
    }
  }
}

AdminImpl::AdminImpl(const std::string& profile_path, Server::Instance& server)
    : server_(server),
      request_id_extension_(Http::RequestIDExtensionFactory::defaultInstance(server_.random())),
      profile_path_(profile_path),
      stats_(Http::ConnectionManagerImpl::generateStats("http.admin.", server_.stats())),
      tracing_stats_(
          Http::ConnectionManagerImpl::generateTracingStats("http.admin.", no_op_store_)),
      route_config_provider_(server.timeSource()),
      scoped_route_config_provider_(server.timeSource()), stats_handler_(server),
      logs_handler_(server), profiling_handler_(profile_path), runtime_handler_(server),
      listeners_handler_(server), server_cmd_handler_(server), server_info_handler_(server),
      // TODO(jsedgwick) add /runtime_reset endpoint that removes all admin-set values
      handlers_{
          {"/", "Admin home page", MAKE_ADMIN_HANDLER(handlerAdminHome), false, false},
          {"/certs", "print certs on machine",
           MAKE_ADMIN_HANDLER(server_info_handler_.handlerCerts), false, false},
          {"/clusters", "upstream cluster status", MAKE_ADMIN_HANDLER(handlerClusters), false,
           false},
          {"/config_dump", "dump current Envoy configs (experimental)",
           MAKE_ADMIN_HANDLER(handlerConfigDump), false, false},
          {"/contention", "dump current Envoy mutex contention stats (if enabled)",
           MAKE_ADMIN_HANDLER(stats_handler_.handlerContention), false, false},
          {"/cpuprofiler", "enable/disable the CPU profiler",
           MAKE_ADMIN_HANDLER(profiling_handler_.handlerCpuProfiler), false, true},
          {"/heapprofiler", "enable/disable the heap profiler",
           MAKE_ADMIN_HANDLER(profiling_handler_.handlerHeapProfiler), false, true},
          {"/healthcheck/fail", "cause the server to fail health checks",
           MAKE_ADMIN_HANDLER(server_cmd_handler_.handlerHealthcheckFail), false, true},
          {"/healthcheck/ok", "cause the server to pass health checks",
           MAKE_ADMIN_HANDLER(server_cmd_handler_.handlerHealthcheckOk), false, true},
          {"/help", "print out list of admin commands", MAKE_ADMIN_HANDLER(handlerHelp), false,
           false},
          {"/hot_restart_version", "print the hot restart compatibility version",
           MAKE_ADMIN_HANDLER(server_info_handler_.handlerHotRestartVersion), false, false},
          {"/logging", "query/change logging levels",
           MAKE_ADMIN_HANDLER(logs_handler_.handlerLogging), false, true},
          {"/memory", "print current allocation/heap usage",
           MAKE_ADMIN_HANDLER(server_info_handler_.handlerMemory), false, false},
          {"/quitquitquit", "exit the server",
           MAKE_ADMIN_HANDLER(server_cmd_handler_.handlerQuitQuitQuit), false, true},
          {"/reset_counters", "reset all counters to zero",
           MAKE_ADMIN_HANDLER(stats_handler_.handlerResetCounters), false, true},
          {"/drain_listeners", "drain listeners",
           MAKE_ADMIN_HANDLER(listeners_handler_.handlerDrainListeners), false, true},
          {"/server_info", "print server version/status information",
           MAKE_ADMIN_HANDLER(server_info_handler_.handlerServerInfo), false, false},
          {"/ready", "print server state, return 200 if LIVE, otherwise return 503",
           MAKE_ADMIN_HANDLER(server_info_handler_.handlerReady), false, false},
          {"/stats", "print server stats", MAKE_ADMIN_HANDLER(stats_handler_.handlerStats), false,
           false},
          {"/stats/prometheus", "print server stats in prometheus format",
           MAKE_ADMIN_HANDLER(stats_handler_.handlerPrometheusStats), false, false},
          {"/stats/recentlookups", "Show recent stat-name lookups",
           MAKE_ADMIN_HANDLER(stats_handler_.handlerStatsRecentLookups), false, false},
          {"/stats/recentlookups/clear", "clear list of stat-name lookups and counter",
           MAKE_ADMIN_HANDLER(stats_handler_.handlerStatsRecentLookupsClear), false, true},
          {"/stats/recentlookups/disable", "disable recording of reset stat-name lookup names",
           MAKE_ADMIN_HANDLER(stats_handler_.handlerStatsRecentLookupsDisable), false, true},
          {"/stats/recentlookups/enable", "enable recording of reset stat-name lookup names",
           MAKE_ADMIN_HANDLER(stats_handler_.handlerStatsRecentLookupsEnable), false, true},
          {"/listeners", "print listener info",
           MAKE_ADMIN_HANDLER(listeners_handler_.handlerListenerInfo), false, false},
          {"/runtime", "print runtime values", MAKE_ADMIN_HANDLER(runtime_handler_.handlerRuntime),
           false, false},
          {"/runtime_modify", "modify runtime values",
           MAKE_ADMIN_HANDLER(runtime_handler_.handlerRuntimeModify), false, true},
          {"/reopen_logs", "reopen access logs",
           MAKE_ADMIN_HANDLER(logs_handler_.handlerReopenLogs), false, true},
      },
      date_provider_(server.dispatcher().timeSource()),
      admin_filter_chain_(std::make_shared<AdminFilterChain>()),
      local_reply_(LocalReply::Factory::createDefault()) {}

Http::ServerConnectionPtr AdminImpl::createCodec(Network::Connection& connection,
                                                 const Buffer::Instance& data,
                                                 Http::ServerConnectionCallbacks& callbacks) {
  return Http::ConnectionManagerUtility::autoCreateCodec(
      connection, data, callbacks, server_.stats(), http1_codec_stats_, http2_codec_stats_,
      Http::Http1Settings(),
      ::Envoy::Http2::Utility::initializeAndValidateOptions(
          envoy::config::core::v3::Http2ProtocolOptions()),
      maxRequestHeadersKb(), maxRequestHeadersCount(), headersWithUnderscoresAction());
}

bool AdminImpl::createNetworkFilterChain(Network::Connection& connection,
                                         const std::vector<Network::FilterFactoryCb>&) {
  // Don't pass in the overload manager so that the admin interface is accessible even when
  // the envoy is overloaded.
  connection.addReadFilter(Network::ReadFilterSharedPtr{new Http::ConnectionManagerImpl(
      *this, server_.drainManager(), server_.random(), server_.httpContext(), server_.runtime(),
      server_.localInfo(), server_.clusterManager(), nullptr, server_.timeSource())});
  return true;
}

void AdminImpl::createFilterChain(Http::FilterChainFactoryCallbacks& callbacks) {
  callbacks.addStreamFilter(std::make_shared<AdminFilter>(createCallbackFunction()));
}

Http::Code AdminImpl::runCallback(absl::string_view path_and_query,
                                  Http::ResponseHeaderMap& response_headers,
                                  Buffer::Instance& response, AdminStream& admin_stream) {

  Http::Code code = Http::Code::OK;
  bool found_handler = false;

  std::string::size_type query_index = path_and_query.find('?');
  if (query_index == std::string::npos) {
    query_index = path_and_query.size();
  }

  for (const UrlHandler& handler : handlers_) {
    if (path_and_query.compare(0, query_index, handler.prefix_) == 0) {
      found_handler = true;
      if (handler.mutates_server_state_) {
        const absl::string_view method = admin_stream.getRequestHeaders().getMethodValue();
        if (method != Http::Headers::get().MethodValues.Post) {
          ENVOY_LOG(error, "admin path \"{}\" mutates state, method={} rather than POST",
                    handler.prefix_, method);
          code = Http::Code::MethodNotAllowed;
          response.add(fmt::format("Method {} not allowed, POST required.", method));
          break;
        }
      }
      code = handler.handler_(path_and_query, response_headers, response, admin_stream);
      Memory::Utils::tryShrinkHeap();
      break;
    }
  }

  if (!found_handler) {
    // Extra space is emitted below to have "invalid path." be a separate sentence in the
    // 404 output from "admin commands are:" in handlerHelp.
    response.add("invalid path. ");
    handlerHelp(path_and_query, response_headers, response, admin_stream);
    code = Http::Code::NotFound;
  }

  return code;
}

std::vector<const AdminImpl::UrlHandler*> AdminImpl::sortedHandlers() const {
  std::vector<const UrlHandler*> sorted_handlers;
  for (const UrlHandler& handler : handlers_) {
    sorted_handlers.push_back(&handler);
  }
  // Note: it's generally faster to sort a vector with std::vector than to construct a std::map.
  std::sort(sorted_handlers.begin(), sorted_handlers.end(),
            [](const UrlHandler* h1, const UrlHandler* h2) { return h1->prefix_ < h2->prefix_; });
  return sorted_handlers;
}

Http::Code AdminImpl::handlerHelp(absl::string_view, Http::ResponseHeaderMap&,
                                  Buffer::Instance& response, AdminStream&) {
  response.add("admin commands are:\n");

  // Prefix order is used during searching, but for printing do them in alpha order.
  for (const UrlHandler* handler : sortedHandlers()) {
    response.add(fmt::format("  {}: {}\n", handler->prefix_, handler->help_text_));
  }
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerAdminHome(absl::string_view, Http::ResponseHeaderMap& response_headers,
                                       Buffer::Instance& response, AdminStream&) {
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Html);

  response.add(absl::StrReplaceAll(AdminHtmlStart, {{"@FAVICON@", EnvoyFavicon}}));

  // Prefix order is used during searching, but for printing do them in alpha order.
  for (const UrlHandler* handler : sortedHandlers()) {
    absl::string_view path = handler->prefix_;

    if (path == "/") {
      continue; // No need to print self-link to index page.
    }

    // Remove the leading slash from the link, so that the admin page can be
    // rendered as part of another console, on a sub-path.
    //
    // E.g. consider a downstream dashboard that embeds the Envoy admin console.
    // In that case, the "/stats" endpoint would be at
    // https://DASHBOARD/envoy_admin/stats. If the links we present on the home
    // page are absolute (e.g. "/stats") they won't work in the context of the
    // dashboard. Removing the leading slash, they will work properly in both
    // the raw admin console and when embedded in another page and URL
    // hierarchy.
    ASSERT(!path.empty());
    ASSERT(path[0] == '/');
    path = path.substr(1);

    // For handlers that mutate state, render the link as a button in a POST form,
    // rather than an anchor tag. This should discourage crawlers that find the /
    // page from accidentally mutating all the server state by GETting all the hrefs.
    const char* link_format =
        handler->mutates_server_state_
            ? "<form action='{}' method='post' class='home-form'><button>{}</button></form>"
            : "<a href='{}'>{}</a>";
    const std::string link = fmt::format(link_format, path, path);

    // Handlers are all specified by statically above, and are thus trusted and do
    // not require escaping.
    response.add(fmt::format("<tr class='home-row'><td class='home-data'>{}</td>"
                             "<td class='home-data'>{}</td></tr>\n",
                             link, Html::Utility::sanitize(handler->help_text_)));
  }
  response.add(AdminHtmlEnd);
  return Http::Code::OK;
}

const Network::Address::Instance& AdminImpl::localAddress() {
  return *server_.localInfo().address();
}

bool AdminImpl::addHandler(const std::string& prefix, const std::string& help_text,
                           HandlerCb callback, bool removable, bool mutates_state) {
  ASSERT(prefix.size() > 1);
  ASSERT(prefix[0] == '/');

  // Sanitize prefix and help_text to ensure no XSS can be injected, as
  // we are injecting these strings into HTML that runs in a domain that
  // can mutate Envoy server state. Also rule out some characters that
  // make no sense as part of a URL path: ? and :.
  const std::string::size_type pos = prefix.find_first_of("&\"'<>?:");
  if (pos != std::string::npos) {
    ENVOY_LOG(error, "filter \"{}\" contains invalid character '{}'", prefix, prefix[pos]);
    return false;
  }

  auto it = std::find_if(handlers_.cbegin(), handlers_.cend(),
                         [&prefix](const UrlHandler& entry) { return prefix == entry.prefix_; });
  if (it == handlers_.end()) {
    handlers_.push_back({prefix, help_text, callback, removable, mutates_state});
    return true;
  }
  return false;
}

bool AdminImpl::removeHandler(const std::string& prefix) {
  const size_t size_before_removal = handlers_.size();
  handlers_.remove_if(
      [&prefix](const UrlHandler& entry) { return prefix == entry.prefix_ && entry.removable_; });
  if (handlers_.size() != size_before_removal) {
    return true;
  }
  return false;
}

Http::Code AdminImpl::request(absl::string_view path_and_query, absl::string_view method,
                              Http::ResponseHeaderMap& response_headers, std::string& body) {
  AdminFilter filter(createCallbackFunction());

  auto request_headers = Http::RequestHeaderMapImpl::create();
  request_headers->setMethod(method);
  filter.decodeHeaders(*request_headers, false);
  Buffer::OwnedImpl response;

  Http::Code code = runCallback(path_and_query, response_headers, response, filter);
  Utility::populateFallbackResponseHeaders(code, response_headers);
  body = response.toString();
  return code;
}

void AdminImpl::closeSocket() {
  if (socket_) {
    socket_->close();
  }
}

void AdminImpl::addListenerToHandler(Network::ConnectionHandler* handler) {
  if (listener_) {
    handler->addListener(absl::nullopt, *listener_);
  }
}

} // namespace Server
} // namespace Envoy
