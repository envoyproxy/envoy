#include "server/http/admin.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "envoy/admin/v2alpha/certs.pb.h"
#include "envoy/admin/v2alpha/clusters.pb.h"
#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/admin/v2alpha/listeners.pb.h"
#include "envoy/admin/v2alpha/memory.pb.h"
#include "envoy/admin/v2alpha/mutex_stats.pb.h"
#include "envoy/admin/v2alpha/server_info.pb.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/hot_restart.h"
#include "envoy/server/instance.h"
#include "envoy/server/options.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/access_log/access_log_formatter.h"
#include "common/access_log/access_log_impl.h"
#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/mutex_tracer_impl.h"
#include "common/common/utility.h"
#include "common/common/version.h"
#include "common/html/utility.h"
#include "common/http/codes.h"
#include "common/http/conn_manager_utility.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"
#include "common/memory/stats.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/utility.h"
#include "common/profiler/profiler.h"
#include "common/router/config_impl.h"
#include "common/stats/histogram_impl.h"
#include "common/upstream/host_utility.h"

#include "extensions/access_loggers/file/file_access_log_impl.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"

// TODO(mattklein123): Switch to JSON interface methods and remove rapidjson dependency.
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/reader.h"
#include "rapidjson/schema.h"
#include "rapidjson/stream.h"
#include "rapidjson/stringbuffer.h"
#include "spdlog/spdlog.h"

using namespace rapidjson;

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

const std::regex PromRegex("[^a-zA-Z0-9_]");

const uint64_t RecentLookupsCapacity = 100;

void populateFallbackResponseHeaders(Http::Code code, Http::HeaderMap& header_map) {
  header_map.insertStatus().value(std::to_string(enumToInt(code)));
  const auto& headers = Http::Headers::get();
  if (header_map.ContentType() == nullptr) {
    // Default to text-plain if unset.
    header_map.insertContentType().value().setReference(headers.ContentTypeValues.TextUtf8);
  }
  // Default to 'no-cache' if unset, but not 'no-store' which may break the back button.
  if (header_map.CacheControl() == nullptr) {
    header_map.insertCacheControl().value().setReference(headers.CacheControlValues.NoCacheMaxAge0);
  }

  // Under no circumstance should browsers sniff content-type.
  header_map.addReference(headers.XContentTypeOptions, headers.XContentTypeOptionValues.Nosniff);
}

// Helper method to get filter parameter, or report an error for an invalid regex.
bool filterParam(Http::Utility::QueryParams params, Buffer::Instance& response,
                 absl::optional<std::regex>& regex) {
  auto p = params.find("filter");
  if (p != params.end()) {
    const std::string& pattern = p->second;
    try {
      regex = std::regex(pattern);
    } catch (std::regex_error& error) {
      // Include the offending pattern in the log, but not the error message.
      response.add(fmt::format("Invalid regex: \"{}\"\n", error.what()));
      ENVOY_LOG_MISC(error, "admin: Invalid regex: \"{}\": {}", error.what(), pattern);
      return false;
    }
  }
  return true;
}

// Helper method to get the format parameter
absl::optional<std::string> formatParam(Http::Utility::QueryParams params) {
  return (params.find("format") != params.end()) ? absl::optional<std::string>{params.at("format")}
                                                 : absl::nullopt;
}

// Helper method that ensures that we've setting flags based on all the health flag values on the
// host.
void setHealthFlag(Upstream::Host::HealthFlag flag, const Upstream::Host& host,
                   envoy::admin::v2alpha::HostHealthStatus& health_status) {
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
      health_status.set_eds_health_status(envoy::api::v2::core::HealthStatus::UNHEALTHY);
    } else if (host.healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_EDS_HEALTH)) {
      health_status.set_eds_health_status(envoy::api::v2::core::HealthStatus::DEGRADED);
    } else {
      health_status.set_eds_health_status(envoy::api::v2::core::HealthStatus::HEALTHY);
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
} // namespace

AdminFilter::AdminFilter(AdminImpl& parent) : parent_(parent) {}

Http::FilterHeadersStatus AdminFilter::decodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  request_headers_ = &headers;
  if (end_stream) {
    onComplete();
  }

  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus AdminFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  // Currently we generically buffer all admin request data in case a handler wants to use it.
  // If we ever support streaming admin requests we may need to revisit this. Note, we must use
  // addDecodedData() here since we might need to perform onComplete() processing if end_stream is
  // true.
  callbacks_->addDecodedData(data, false);

  if (end_stream) {
    onComplete();
  }

  return Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterTrailersStatus AdminFilter::decodeTrailers(Http::HeaderMap&) {
  onComplete();
  return Http::FilterTrailersStatus::StopIteration;
}

void AdminFilter::onDestroy() {
  for (const auto& callback : on_destroy_callbacks_) {
    callback();
  }
}

void AdminFilter::addOnDestroyCallback(std::function<void()> cb) {
  on_destroy_callbacks_.push_back(std::move(cb));
}

Http::StreamDecoderFilterCallbacks& AdminFilter::getDecoderFilterCallbacks() const {
  ASSERT(callbacks_ != nullptr);
  return *callbacks_;
}

const Buffer::Instance* AdminFilter::getRequestBody() const { return callbacks_->decodingBuffer(); }

const Http::HeaderMap& AdminFilter::getRequestHeaders() const {
  ASSERT(request_headers_ != nullptr);
  return *request_headers_;
}

bool AdminImpl::changeLogLevel(const Http::Utility::QueryParams& params) {
  if (params.size() != 1) {
    return false;
  }

  std::string name = params.begin()->first;
  std::string level = params.begin()->second;

  // First see if the level is valid.
  size_t level_to_use = std::numeric_limits<size_t>::max();
  for (size_t i = 0; i < ARRAY_SIZE(spdlog::level::level_string_views); i++) {
    if (level == spdlog::level::level_string_views[i]) {
      level_to_use = i;
      break;
    }
  }

  if (level_to_use == std::numeric_limits<size_t>::max()) {
    return false;
  }

  // Now either change all levels or a single level.
  if (name == "level") {
    ENVOY_LOG(debug, "change all log levels: level='{}'", level);
    for (Logger::Logger& logger : Logger::Registry::loggers()) {
      logger.setLevel(static_cast<spdlog::level::level_enum>(level_to_use));
    }
  } else {
    ENVOY_LOG(debug, "change log level: name='{}' level='{}'", name, level);
    Logger::Logger* logger_to_change = nullptr;
    for (Logger::Logger& logger : Logger::Registry::loggers()) {
      if (logger.name() == name) {
        logger_to_change = &logger;
        break;
      }
    }

    if (!logger_to_change) {
      return false;
    }

    logger_to_change->setLevel(static_cast<spdlog::level::level_enum>(level_to_use));
  }

  return true;
}

void AdminImpl::addOutlierInfo(const std::string& cluster_name,
                               const Upstream::Outlier::Detector* outlier_detector,
                               Buffer::Instance& response) {
  if (outlier_detector) {
    response.add(fmt::format(
        "{}::outlier::success_rate_average::{}\n", cluster_name,
        outlier_detector->successRateAverage(
            Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin)));
    response.add(fmt::format(
        "{}::outlier::success_rate_ejection_threshold::{}\n", cluster_name,
        outlier_detector->successRateEjectionThreshold(
            Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin)));
    response.add(fmt::format(
        "{}::outlier::local_origin_success_rate_average::{}\n", cluster_name,
        outlier_detector->successRateAverage(
            Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin)));
    response.add(fmt::format(
        "{}::outlier::local_origin_success_rate_ejection_threshold::{}\n", cluster_name,
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

void AdminImpl::writeClustersAsJson(Buffer::Instance& response) {
  envoy::admin::v2alpha::Clusters clusters;
  for (auto& cluster_pair : server_.clusterManager().clusters()) {
    const Upstream::Cluster& cluster = cluster_pair.second.get();
    Upstream::ClusterInfoConstSharedPtr cluster_info = cluster.info();

    envoy::admin::v2alpha::ClusterStatus& cluster_status = *clusters.add_cluster_statuses();
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
        envoy::admin::v2alpha::HostStatus& host_status = *cluster_status.add_host_statuses();
        Network::Utility::addressToProtobufAddress(*host->address(),
                                                   *host_status.mutable_address());
        host_status.set_hostname(host->hostname());

        for (const auto& named_counter : host->counters()) {
          auto& metric = *host_status.add_stats();
          metric.set_name(std::string(named_counter.first));
          metric.set_value(named_counter.second.get().value());
          metric.set_type(envoy::admin::v2alpha::SimpleMetric::COUNTER);
        }

        for (const auto& named_gauge : host->gauges()) {
          auto& metric = *host_status.add_stats();
          metric.set_name(std::string(named_gauge.first));
          metric.set_value(named_gauge.second.get().value());
          metric.set_type(envoy::admin::v2alpha::SimpleMetric::GAUGE);
        }

        envoy::admin::v2alpha::HostHealthStatus& health_status =
            *host_status.mutable_health_status();

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

void AdminImpl::writeListenersAsJson(Buffer::Instance& response) {
  envoy::admin::v2alpha::Listeners listeners;
  for (const auto& listener : server_.listenerManager().listeners()) {
    envoy::admin::v2alpha::ListenerStatus& listener_status = *listeners.add_listener_statuses();
    listener_status.set_name(listener.get().name());
    Network::Utility::addressToProtobufAddress(*listener.get().socket().localAddress(),
                                               *listener_status.mutable_local_address());
  }
  response.add(MessageUtil::getJsonStringFromMessage(listeners, true)); // pretty-print
}

void AdminImpl::writeListenersAsText(Buffer::Instance& response) {
  for (const auto& listener : server_.listenerManager().listeners()) {
    response.add(fmt::format("{}::{}\n", listener.get().name(),
                             listener.get().socket().localAddress()->asString()));
  }
}

Http::Code AdminImpl::handlerClusters(absl::string_view url, Http::HeaderMap& response_headers,
                                      Buffer::Instance& response, AdminStream&) {
  Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);
  const auto format_value = formatParam(query_params);

  if (format_value.has_value() && format_value.value() == "json") {
    writeClustersAsJson(response);
    response_headers.insertContentType().value().setReference(
        Http::Headers::get().ContentTypeValues.Json);
  } else {
    writeClustersAsText(response);
  }

  return Http::Code::OK;
}

// TODO(jsedgwick) Use query params to list available dumps, selectively dump, etc
Http::Code AdminImpl::handlerConfigDump(absl::string_view, Http::HeaderMap& response_headers,
                                        Buffer::Instance& response, AdminStream&) const {
  envoy::admin::v2alpha::ConfigDump dump;
  for (const auto& key_callback_pair : config_tracker_.getCallbacksMap()) {
    ProtobufTypes::MessagePtr message = key_callback_pair.second();
    RELEASE_ASSERT(message, "");
    auto& any_message = *(dump.add_configs());
    any_message.PackFrom(*message);
  }

  response_headers.insertContentType().value().setReference(
      Http::Headers::get().ContentTypeValues.Json);
  response.add(MessageUtil::getJsonStringFromMessage(dump, true)); // pretty-print
  return Http::Code::OK;
}

// TODO(ambuc) Export this as a server (?) stat for monitoring.
Http::Code AdminImpl::handlerContention(absl::string_view, Http::HeaderMap& response_headers,
                                        Buffer::Instance& response, AdminStream&) {

  if (server_.options().mutexTracingEnabled() && server_.mutexTracer() != nullptr) {
    response_headers.insertContentType().value().setReference(
        Http::Headers::get().ContentTypeValues.Json);

    envoy::admin::v2alpha::MutexStats mutex_stats;
    mutex_stats.set_num_contentions(server_.mutexTracer()->numContentions());
    mutex_stats.set_current_wait_cycles(server_.mutexTracer()->currentWaitCycles());
    mutex_stats.set_lifetime_wait_cycles(server_.mutexTracer()->lifetimeWaitCycles());
    response.add(MessageUtil::getJsonStringFromMessage(mutex_stats, true, true));
  } else {
    response.add("Mutex contention tracing is not enabled. To enable, run Envoy with flag "
                 "--enable-mutex-tracing.");
  }
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerCpuProfiler(absl::string_view url, Http::HeaderMap&,
                                         Buffer::Instance& response, AdminStream&) {
  Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);
  if (query_params.size() != 1 || query_params.begin()->first != "enable" ||
      (query_params.begin()->second != "y" && query_params.begin()->second != "n")) {
    response.add("?enable=<y|n>\n");
    return Http::Code::BadRequest;
  }

  bool enable = query_params.begin()->second == "y";
  if (enable && !Profiler::Cpu::profilerEnabled()) {
    if (!Profiler::Cpu::startProfiler(profile_path_)) {
      response.add("failure to start the profiler");
      return Http::Code::InternalServerError;
    }

  } else if (!enable && Profiler::Cpu::profilerEnabled()) {
    Profiler::Cpu::stopProfiler();
  }

  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerHeapProfiler(absl::string_view url, Http::HeaderMap&,
                                          Buffer::Instance& response, AdminStream&) {
  if (!Profiler::Heap::profilerEnabled()) {
    response.add("The current build does not support heap profiler");
    return Http::Code::NotImplemented;
  }

  Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);
  if (query_params.size() != 1 || query_params.begin()->first != "enable" ||
      (query_params.begin()->second != "y" && query_params.begin()->second != "n")) {
    response.add("?enable=<y|n>\n");
    return Http::Code::BadRequest;
  }

  Http::Code res = Http::Code::OK;
  bool enable = query_params.begin()->second == "y";
  if (enable) {
    if (Profiler::Heap::isProfilerStarted()) {
      response.add("Fail to start heap profiler: already started");
      res = Http::Code::BadRequest;
    } else if (!Profiler::Heap::startProfiler(profile_path_)) {
      // GCOVR_EXCL_START
      // TODO(silentdai) remove the GCOVR when startProfiler is better implemented
      response.add("Fail to start the heap profiler");
      res = Http::Code::InternalServerError;
      // GCOVR_EXCL_STOP
    } else {
      response.add("Starting heap profiler");
      res = Http::Code::OK;
    }
  } else {
    // !enable
    if (!Profiler::Heap::isProfilerStarted()) {
      response.add("Fail to stop heap profiler: not started");
      res = Http::Code::BadRequest;
    } else {
      Profiler::Heap::stopProfiler();
      response.add(
          fmt::format("Heap profiler stopped and data written to {}. See "
                      "http://goog-perftools.sourceforge.net/doc/heap_profiler.html for details.",
                      profile_path_));
      res = Http::Code::OK;
    }
  }
  return res;
}

Http::Code AdminImpl::handlerHealthcheckFail(absl::string_view, Http::HeaderMap&,
                                             Buffer::Instance& response, AdminStream&) {
  server_.failHealthcheck(true);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerHealthcheckOk(absl::string_view, Http::HeaderMap&,
                                           Buffer::Instance& response, AdminStream&) {
  server_.failHealthcheck(false);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerHotRestartVersion(absl::string_view, Http::HeaderMap&,
                                               Buffer::Instance& response, AdminStream&) {
  response.add(server_.hotRestart().version());
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerLogging(absl::string_view url, Http::HeaderMap&,
                                     Buffer::Instance& response, AdminStream&) {
  Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);

  Http::Code rc = Http::Code::OK;
  if (!query_params.empty() && !changeLogLevel(query_params)) {
    response.add("usage: /logging?<name>=<level> (change single level)\n");
    response.add("usage: /logging?level=<level> (change all levels)\n");
    response.add("levels: ");
    for (auto level_string_view : spdlog::level::level_string_views) {
      response.add(fmt::format("{} ", level_string_view));
    }

    response.add("\n");
    rc = Http::Code::NotFound;
  }

  response.add("active loggers:\n");
  for (const Logger::Logger& logger : Logger::Registry::loggers()) {
    response.add(fmt::format("  {}: {}\n", logger.name(), logger.levelString()));
  }

  response.add("\n");
  return rc;
}

// TODO(ambuc): Add more tcmalloc stats, export proto details based on allocator.
Http::Code AdminImpl::handlerMemory(absl::string_view, Http::HeaderMap& response_headers,
                                    Buffer::Instance& response, AdminStream&) {
  response_headers.insertContentType().value().setReference(
      Http::Headers::get().ContentTypeValues.Json);
  envoy::admin::v2alpha::Memory memory;
  memory.set_allocated(Memory::Stats::totalCurrentlyAllocated());
  memory.set_heap_size(Memory::Stats::totalCurrentlyReserved());
  memory.set_total_thread_cache(Memory::Stats::totalThreadCacheBytes());
  memory.set_pageheap_unmapped(Memory::Stats::totalPageHeapUnmapped());
  memory.set_pageheap_free(Memory::Stats::totalPageHeapFree());
  response.add(MessageUtil::getJsonStringFromMessage(memory, true, true)); // pretty-print
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerDrainListeners(absl::string_view url, Http::HeaderMap&,
                                            Buffer::Instance& response, AdminStream&) {
  const Http::Utility::QueryParams params = Http::Utility::parseQueryString(url);
  ListenerManager::StopListenersType stop_listeners_type =
      params.find("inboundonly") != params.end() ? ListenerManager::StopListenersType::InboundOnly
                                                 : ListenerManager::StopListenersType::All;
  server_.listenerManager().stopListeners(stop_listeners_type);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerResetCounters(absl::string_view, Http::HeaderMap&,
                                           Buffer::Instance& response, AdminStream&) {
  for (const Stats::CounterSharedPtr& counter : server_.stats().counters()) {
    counter->reset();
  }
  server_.stats().symbolTable().clearRecentLookups();
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerStatsRecentLookups(absl::string_view, Http::HeaderMap&,
                                                Buffer::Instance& response, AdminStream&) {
  Stats::SymbolTable& symbol_table = server_.stats().symbolTable();
  std::string table;
  const uint64_t total =
      symbol_table.getRecentLookups([&table](absl::string_view name, uint64_t count) {
        table += fmt::format("{:8d} {}\n", count, name);
      });
  if (table.empty() && symbol_table.recentLookupCapacity() == 0) {
    table = "Lookup tracking is not enabled. Use /stats/recentlookups/enable to enable.\n";
  } else {
    response.add("   Count Lookup\n");
  }
  response.add(absl::StrCat(table, "\ntotal: ", total, "\n"));
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerStatsRecentLookupsClear(absl::string_view, Http::HeaderMap&,
                                                     Buffer::Instance& response, AdminStream&) {
  server_.stats().symbolTable().clearRecentLookups();
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerStatsRecentLookupsDisable(absl::string_view, Http::HeaderMap&,
                                                       Buffer::Instance& response, AdminStream&) {
  server_.stats().symbolTable().setRecentLookupCapacity(0);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerStatsRecentLookupsEnable(absl::string_view, Http::HeaderMap&,
                                                      Buffer::Instance& response, AdminStream&) {
  server_.stats().symbolTable().setRecentLookupCapacity(RecentLookupsCapacity);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerServerInfo(absl::string_view, Http::HeaderMap& headers,
                                        Buffer::Instance& response, AdminStream&) {
  time_t current_time = time(nullptr);
  envoy::admin::v2alpha::ServerInfo server_info;
  server_info.set_version(VersionInfo::version());
  server_info.set_hot_restart_version(server_.hotRestart().version());
  server_info.set_state(
      Utility::serverState(server_.initManager().state(), server_.healthCheckFailed()));

  server_info.mutable_uptime_current_epoch()->set_seconds(current_time -
                                                          server_.startTimeCurrentEpoch());
  server_info.mutable_uptime_all_epochs()->set_seconds(current_time -
                                                       server_.startTimeFirstEpoch());
  envoy::admin::v2alpha::CommandLineOptions* command_line_options =
      server_info.mutable_command_line_options();
  *command_line_options = *server_.options().toCommandLineOptions();
  response.add(MessageUtil::getJsonStringFromMessage(server_info, true, true));
  headers.insertContentType().value().setReference(Http::Headers::get().ContentTypeValues.Json);
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerReady(absl::string_view, Http::HeaderMap&, Buffer::Instance& response,
                                   AdminStream&) {
  const envoy::admin::v2alpha::ServerInfo::State state =
      Utility::serverState(server_.initManager().state(), server_.healthCheckFailed());

  response.add(envoy::admin::v2alpha::ServerInfo_State_Name(state) + "\n");
  Http::Code code = state == envoy::admin::v2alpha::ServerInfo_State_LIVE
                        ? Http::Code::OK
                        : Http::Code::ServiceUnavailable;
  return code;
}

Http::Code AdminImpl::handlerStats(absl::string_view url, Http::HeaderMap& response_headers,
                                   Buffer::Instance& response, AdminStream& admin_stream) {
  Http::Code rc = Http::Code::OK;
  const Http::Utility::QueryParams params = Http::Utility::parseQueryString(url);

  const bool used_only = params.find("usedonly") != params.end();
  absl::optional<std::regex> regex;
  if (!filterParam(params, response, regex)) {
    return Http::Code::BadRequest;
  }

  std::map<std::string, uint64_t> all_stats;
  for (const Stats::CounterSharedPtr& counter : server_.stats().counters()) {
    if (shouldShowMetric(*counter, used_only, regex)) {
      all_stats.emplace(counter->name(), counter->value());
    }
  }

  for (const Stats::GaugeSharedPtr& gauge : server_.stats().gauges()) {
    if (shouldShowMetric(*gauge, used_only, regex)) {
      ASSERT(gauge->importMode() != Stats::Gauge::ImportMode::Uninitialized);
      all_stats.emplace(gauge->name(), gauge->value());
    }
  }

  if (const auto format_value = formatParam(params)) {
    if (format_value.value() == "json") {
      response_headers.insertContentType().value().setReference(
          Http::Headers::get().ContentTypeValues.Json);
      response.add(
          AdminImpl::statsAsJson(all_stats, server_.stats().histograms(), used_only, regex));
    } else if (format_value.value() == "prometheus") {
      return handlerPrometheusStats(url, response_headers, response, admin_stream);
    } else {
      response.add("usage: /stats?format=json  or /stats?format=prometheus \n");
      response.add("\n");
      rc = Http::Code::NotFound;
    }
  } else { // Display plain stats if format query param is not there.
    for (const auto& stat : all_stats) {
      response.add(fmt::format("{}: {}\n", stat.first, stat.second));
    }
    // TODO(ramaraochavali): See the comment in ThreadLocalStoreImpl::histograms() for why we use a
    // multimap here. This makes sure that duplicate histograms get output. When shared storage is
    // implemented this can be switched back to a normal map.
    std::multimap<std::string, std::string> all_histograms;
    for (const Stats::ParentHistogramSharedPtr& histogram : server_.stats().histograms()) {
      if (shouldShowMetric(*histogram, used_only, regex)) {
        all_histograms.emplace(histogram->name(), histogram->quantileSummary());
      }
    }
    for (const auto& histogram : all_histograms) {
      response.add(fmt::format("{}: {}\n", histogram.first, histogram.second));
    }
  }
  return rc;
}

Http::Code AdminImpl::handlerPrometheusStats(absl::string_view path_and_query, Http::HeaderMap&,
                                             Buffer::Instance& response, AdminStream&) {
  const Http::Utility::QueryParams params = Http::Utility::parseQueryString(path_and_query);
  const bool used_only = params.find("usedonly") != params.end();
  absl::optional<std::regex> regex;
  if (!filterParam(params, response, regex)) {
    return Http::Code::BadRequest;
  }
  PrometheusStatsFormatter::statsAsPrometheus(server_.stats().counters(), server_.stats().gauges(),
                                              server_.stats().histograms(), response, used_only,
                                              regex);
  return Http::Code::OK;
}

std::string PrometheusStatsFormatter::sanitizeName(const std::string& name) {
  // The name must match the regex [a-zA-Z_][a-zA-Z0-9_]* as required by
  // prometheus. Refer to https://prometheus.io/docs/concepts/data_model/.
  std::string stats_name = std::regex_replace(name, PromRegex, "_");
  if (stats_name[0] >= '0' && stats_name[0] <= '9') {
    return fmt::format("_{}", stats_name);
  } else {
    return stats_name;
  }
}

std::string PrometheusStatsFormatter::formattedTags(const std::vector<Stats::Tag>& tags) {
  std::vector<std::string> buf;
  buf.reserve(tags.size());
  for (const Stats::Tag& tag : tags) {
    buf.push_back(fmt::format("{}=\"{}\"", sanitizeName(tag.name_), tag.value_));
  }
  return StringUtil::join(buf, ",");
}

std::string PrometheusStatsFormatter::metricName(const std::string& extracted_name) {
  // Add namespacing prefix to avoid conflicts, as per best practice:
  // https://prometheus.io/docs/practices/naming/#metric-names
  // Also, naming conventions on https://prometheus.io/docs/concepts/data_model/
  return sanitizeName(fmt::format("envoy_{0}", extracted_name));
}

uint64_t PrometheusStatsFormatter::statsAsPrometheus(
    const std::vector<Stats::CounterSharedPtr>& counters,
    const std::vector<Stats::GaugeSharedPtr>& gauges,
    const std::vector<Stats::ParentHistogramSharedPtr>& histograms, Buffer::Instance& response,
    const bool used_only, const absl::optional<std::regex>& regex) {
  std::unordered_set<std::string> metric_type_tracker;
  for (const auto& counter : counters) {
    if (!shouldShowMetric(*counter, used_only, regex)) {
      continue;
    }

    const std::string tags = formattedTags(counter->tags());
    const std::string metric_name = metricName(counter->tagExtractedName());
    if (metric_type_tracker.find(metric_name) == metric_type_tracker.end()) {
      metric_type_tracker.insert(metric_name);
      response.add(fmt::format("# TYPE {0} counter\n", metric_name));
    }
    response.add(fmt::format("{0}{{{1}}} {2}\n", metric_name, tags, counter->value()));
  }

  for (const auto& gauge : gauges) {
    if (!shouldShowMetric(*gauge, used_only, regex)) {
      continue;
    }

    const std::string tags = formattedTags(gauge->tags());
    const std::string metric_name = metricName(gauge->tagExtractedName());
    if (metric_type_tracker.find(metric_name) == metric_type_tracker.end()) {
      metric_type_tracker.insert(metric_name);
      response.add(fmt::format("# TYPE {0} gauge\n", metric_name));
    }
    response.add(fmt::format("{0}{{{1}}} {2}\n", metric_name, tags, gauge->value()));
  }

  for (const auto& histogram : histograms) {
    if (!shouldShowMetric(*histogram, used_only, regex)) {
      continue;
    }

    const std::string tags = formattedTags(histogram->tags());
    const std::string hist_tags = histogram->tags().empty() ? EMPTY_STRING : (tags + ",");

    const std::string metric_name = metricName(histogram->tagExtractedName());
    if (metric_type_tracker.find(metric_name) == metric_type_tracker.end()) {
      metric_type_tracker.insert(metric_name);
      response.add(fmt::format("# TYPE {0} histogram\n", metric_name));
    }

    const Stats::HistogramStatistics& stats = histogram->cumulativeStatistics();
    const std::vector<double>& supported_buckets = stats.supportedBuckets();
    const std::vector<uint64_t>& computed_buckets = stats.computedBuckets();
    for (size_t i = 0; i < supported_buckets.size(); ++i) {
      double bucket = supported_buckets[i];
      uint64_t value = computed_buckets[i];
      // We want to print the bucket in a fixed point (non-scientific) format. The fmt library
      // doesn't have a specific modifier to format as a fixed-point value only so we use the
      // 'g' operator which prints the number in general fixed point format or scientific format
      // with precision 50 to round the number up to 32 significant digits in fixed point format
      // which should cover pretty much all cases
      response.add(fmt::format("{0}_bucket{{{1}le=\"{2:.32g}\"}} {3}\n", metric_name, hist_tags,
                               bucket, value));
    }

    response.add(fmt::format("{0}_bucket{{{1}le=\"+Inf\"}} {2}\n", metric_name, hist_tags,
                             stats.sampleCount()));
    response.add(fmt::format("{0}_sum{{{1}}} {2:.32g}\n", metric_name, tags, stats.sampleSum()));
    response.add(fmt::format("{0}_count{{{1}}} {2}\n", metric_name, tags, stats.sampleCount()));
  }

  return metric_type_tracker.size();
}

std::string
AdminImpl::statsAsJson(const std::map<std::string, uint64_t>& all_stats,
                       const std::vector<Stats::ParentHistogramSharedPtr>& all_histograms,
                       const bool used_only, const absl::optional<std::regex> regex,
                       const bool pretty_print) {
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Value stats_array(rapidjson::kArrayType);
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
  for (const auto& stat : all_stats) {
    Value stat_obj;
    stat_obj.SetObject();
    Value stat_name;
    stat_name.SetString(stat.first.c_str(), allocator);
    stat_obj.AddMember("name", stat_name, allocator);
    Value stat_value;
    stat_value.SetInt(stat.second);
    stat_obj.AddMember("value", stat_value, allocator);
    stats_array.PushBack(stat_obj, allocator);
  }

  Value histograms_container_obj;
  histograms_container_obj.SetObject();

  Value histograms_obj;
  histograms_obj.SetObject();

  bool found_used_histogram = false;
  rapidjson::Value histogram_array(rapidjson::kArrayType);

  for (const Stats::ParentHistogramSharedPtr& histogram : all_histograms) {
    if (shouldShowMetric(*histogram, used_only, regex)) {
      if (!found_used_histogram) {
        // It is not possible for the supported quantiles to differ across histograms, so it is ok
        // to send them once.
        Stats::HistogramStatisticsImpl empty_statistics;
        rapidjson::Value supported_quantile_array(rapidjson::kArrayType);
        for (double quantile : empty_statistics.supportedQuantiles()) {
          Value quantile_type;
          quantile_type.SetDouble(quantile * 100);
          supported_quantile_array.PushBack(quantile_type, allocator);
        }
        histograms_obj.AddMember("supported_quantiles", supported_quantile_array, allocator);
        found_used_histogram = true;
      }
      Value histogram_obj;
      histogram_obj.SetObject();
      Value histogram_name;
      histogram_name.SetString(histogram->name().c_str(), allocator);
      histogram_obj.AddMember("name", histogram_name, allocator);

      rapidjson::Value computed_quantile_array(rapidjson::kArrayType);

      for (size_t i = 0; i < histogram->intervalStatistics().supportedQuantiles().size(); ++i) {
        Value quantile_obj;
        quantile_obj.SetObject();
        Value interval_value;
        if (!std::isnan(histogram->intervalStatistics().computedQuantiles()[i])) {
          interval_value.SetDouble(histogram->intervalStatistics().computedQuantiles()[i]);
        }
        quantile_obj.AddMember("interval", interval_value, allocator);
        Value cumulative_value;
        // We skip nan entries to put in the {null, null} entry to keep other data aligned.
        if (!std::isnan(histogram->cumulativeStatistics().computedQuantiles()[i])) {
          cumulative_value.SetDouble(histogram->cumulativeStatistics().computedQuantiles()[i]);
        }
        quantile_obj.AddMember("cumulative", cumulative_value, allocator);
        computed_quantile_array.PushBack(quantile_obj, allocator);
      }
      histogram_obj.AddMember("values", computed_quantile_array, allocator);
      histogram_array.PushBack(histogram_obj, allocator);
    }
  }
  if (found_used_histogram) {
    histograms_obj.AddMember("computed_quantiles", histogram_array, allocator);
    histograms_container_obj.AddMember("histograms", histograms_obj, allocator);
    stats_array.PushBack(histograms_container_obj, allocator);
  }
  document.AddMember("stats", stats_array, allocator);
  rapidjson::StringBuffer strbuf;
  if (pretty_print) {
    rapidjson::PrettyWriter<StringBuffer> writer(strbuf);
    document.Accept(writer);
  } else {
    rapidjson::Writer<StringBuffer> writer(strbuf);
    document.Accept(writer);
  }
  return strbuf.GetString();
}

Http::Code AdminImpl::handlerQuitQuitQuit(absl::string_view, Http::HeaderMap&,
                                          Buffer::Instance& response, AdminStream&) {
  server_.shutdown();
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerListenerInfo(absl::string_view url, Http::HeaderMap& response_headers,
                                          Buffer::Instance& response, AdminStream&) {
  const Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);
  const auto format_value = formatParam(query_params);

  if (format_value.has_value() && format_value.value() == "json") {
    writeListenersAsJson(response);
    response_headers.insertContentType().value().setReference(
        Http::Headers::get().ContentTypeValues.Json);
  } else {
    writeListenersAsText(response);
  }
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerCerts(absl::string_view, Http::HeaderMap& response_headers,
                                   Buffer::Instance& response, AdminStream&) {
  // This set is used to track distinct certificates. We may have multiple listeners, upstreams, etc
  // using the same cert.
  response_headers.insertContentType().value().setReference(
      Http::Headers::get().ContentTypeValues.Json);
  envoy::admin::v2alpha::Certificates certificates;
  server_.sslContextManager().iterateContexts([&](const Ssl::Context& context) -> void {
    envoy::admin::v2alpha::Certificate& certificate = *certificates.add_certificates();
    if (context.getCaCertInformation() != nullptr) {
      envoy::admin::v2alpha::CertificateDetails* ca_certificate = certificate.add_ca_cert();
      *ca_certificate = *context.getCaCertInformation();
    }
    for (const auto& cert_details : context.getCertChainInformation()) {
      envoy::admin::v2alpha::CertificateDetails* cert_chain = certificate.add_cert_chain();
      *cert_chain = *cert_details;
    }
  });
  response.add(MessageUtil::getJsonStringFromMessage(certificates, true, true));
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerRuntime(absl::string_view url, Http::HeaderMap& response_headers,
                                     Buffer::Instance& response, AdminStream&) {
  const Http::Utility::QueryParams params = Http::Utility::parseQueryString(url);
  response_headers.insertContentType().value().setReference(
      Http::Headers::get().ContentTypeValues.Json);

  // TODO(jsedgwick) Use proto to structure this output instead of arbitrary JSON
  rapidjson::Document document;
  document.SetObject();
  auto& allocator = document.GetAllocator();
  std::map<std::string, rapidjson::Value> entry_objects;
  rapidjson::Value layer_names{rapidjson::kArrayType};
  const auto& layers = server_.runtime().snapshot().getLayers();

  for (const auto& layer : layers) {
    rapidjson::Value layer_name;
    layer_name.SetString(layer->name().c_str(), allocator);
    layer_names.PushBack(std::move(layer_name), allocator);
    for (const auto& kv : layer->values()) {
      rapidjson::Value entry_object{rapidjson::kObjectType};
      const auto it = entry_objects.find(kv.first);
      if (it == entry_objects.end()) {
        rapidjson::Value entry_object{rapidjson::kObjectType};
        entry_object.AddMember("layer_values", rapidjson::Value{kArrayType}, allocator);
        entry_object.AddMember("final_value", "", allocator);
        entry_objects.emplace(kv.first, std::move(entry_object));
      }
    }
  }
  document.AddMember("layers", std::move(layer_names), allocator);

  for (const auto& layer : layers) {
    for (auto& kv : entry_objects) {
      const auto it = layer->values().find(kv.first);
      const auto& entry_value = it == layer->values().end() ? "" : it->second.raw_string_value_;
      rapidjson::Value entry_value_object;
      entry_value_object.SetString(entry_value.c_str(), allocator);
      if (!entry_value.empty()) {
        kv.second["final_value"] = rapidjson::Value{entry_value_object, allocator};
      }
      kv.second["layer_values"].PushBack(entry_value_object, allocator);
    }
  }

  rapidjson::Value value_arrays_obj{rapidjson::kObjectType};
  for (auto& kv : entry_objects) {
    value_arrays_obj.AddMember(rapidjson::StringRef(kv.first.c_str()), std::move(kv.second),
                               allocator);
  }

  document.AddMember("entries", std::move(value_arrays_obj), allocator);

  rapidjson::StringBuffer strbuf;
  rapidjson::PrettyWriter<StringBuffer> writer(strbuf);
  document.Accept(writer);
  response.add(strbuf.GetString());
  return Http::Code::OK;
}

bool AdminImpl::isFormUrlEncoded(const Http::HeaderEntry* content_type) const {
  if (content_type == nullptr) {
    return false;
  }

  return content_type->value().getStringView() ==
         Http::Headers::get().ContentTypeValues.FormUrlEncoded;
}

Http::Code AdminImpl::handlerRuntimeModify(absl::string_view url, Http::HeaderMap&,
                                           Buffer::Instance& response, AdminStream& admin_stream) {
  Http::Utility::QueryParams params = Http::Utility::parseQueryString(url);
  if (params.empty()) {
    // Check if the params are in the request's body.
    if (admin_stream.getRequestBody() != nullptr &&
        isFormUrlEncoded(admin_stream.getRequestHeaders().ContentType())) {
      params = Http::Utility::parseFromBody(admin_stream.getRequestBody()->toString());
    }

    if (params.empty()) {
      response.add("usage: /runtime_modify?key1=value1&key2=value2&keyN=valueN\n");
      response.add("       or send the parameters as form values\n");
      response.add("use an empty value to remove a previously added override");
      return Http::Code::BadRequest;
    }
  }
  std::unordered_map<std::string, std::string> overrides;
  overrides.insert(params.begin(), params.end());
  try {
    server_.runtime().mergeValues(overrides);
  } catch (const EnvoyException& e) {
    response.add(e.what());
    return Http::Code::ServiceUnavailable;
  }
  response.add("OK\n");
  return Http::Code::OK;
}

ConfigTracker& AdminImpl::getConfigTracker() { return config_tracker_; }

void AdminFilter::onComplete() {
  absl::string_view path = request_headers_->Path()->value().getStringView();
  ENVOY_STREAM_LOG(debug, "request complete: path: {}", *callbacks_, path);

  Buffer::OwnedImpl response;
  Http::HeaderMapPtr header_map{new Http::HeaderMapImpl};
  RELEASE_ASSERT(request_headers_, "");
  Http::Code code = parent_.runCallback(path, *header_map, response, *this);
  populateFallbackResponseHeaders(code, *header_map);
  callbacks_->encodeHeaders(std::move(header_map),
                            end_stream_on_complete_ && response.length() == 0);

  if (response.length() > 0) {
    callbacks_->encodeData(response, end_stream_on_complete_);
  }
}

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
      access_log_path, {}, AccessLog::AccessLogFormatUtils::defaultAccessLogFormatter(),
      server_.accessLogManager()));
  socket_ = std::make_unique<Network::TcpListenSocket>(address, socket_options, true);
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
    : server_(server), profile_path_(profile_path),
      stats_(Http::ConnectionManagerImpl::generateStats("http.admin.", server_.stats())),
      tracing_stats_(
          Http::ConnectionManagerImpl::generateTracingStats("http.admin.", no_op_store_)),
      route_config_provider_(server.timeSource()),
      scoped_route_config_provider_(server.timeSource()),
      // TODO(jsedgwick) add /runtime_reset endpoint that removes all admin-set values
      handlers_{
          {"/", "Admin home page", MAKE_ADMIN_HANDLER(handlerAdminHome), false, false},
          {"/certs", "print certs on machine", MAKE_ADMIN_HANDLER(handlerCerts), false, false},
          {"/clusters", "upstream cluster status", MAKE_ADMIN_HANDLER(handlerClusters), false,
           false},
          {"/config_dump", "dump current Envoy configs (experimental)",
           MAKE_ADMIN_HANDLER(handlerConfigDump), false, false},
          {"/contention", "dump current Envoy mutex contention stats (if enabled)",
           MAKE_ADMIN_HANDLER(handlerContention), false, false},
          {"/cpuprofiler", "enable/disable the CPU profiler",
           MAKE_ADMIN_HANDLER(handlerCpuProfiler), false, true},
          {"/heapprofiler", "enable/disable the heap profiler",
           MAKE_ADMIN_HANDLER(handlerHeapProfiler), false, true},
          {"/healthcheck/fail", "cause the server to fail health checks",
           MAKE_ADMIN_HANDLER(handlerHealthcheckFail), false, true},
          {"/healthcheck/ok", "cause the server to pass health checks",
           MAKE_ADMIN_HANDLER(handlerHealthcheckOk), false, true},
          {"/help", "print out list of admin commands", MAKE_ADMIN_HANDLER(handlerHelp), false,
           false},
          {"/hot_restart_version", "print the hot restart compatibility version",
           MAKE_ADMIN_HANDLER(handlerHotRestartVersion), false, false},
          {"/logging", "query/change logging levels", MAKE_ADMIN_HANDLER(handlerLogging), false,
           true},
          {"/memory", "print current allocation/heap usage", MAKE_ADMIN_HANDLER(handlerMemory),
           false, false},
          {"/quitquitquit", "exit the server", MAKE_ADMIN_HANDLER(handlerQuitQuitQuit), false,
           true},
          {"/reset_counters", "reset all counters to zero",
           MAKE_ADMIN_HANDLER(handlerResetCounters), false, true},
          {"/drain_listeners", "drain listeners", MAKE_ADMIN_HANDLER(handlerDrainListeners), false,
           true},
          {"/server_info", "print server version/status information",
           MAKE_ADMIN_HANDLER(handlerServerInfo), false, false},
          {"/ready", "print server state, return 200 if LIVE, otherwise return 503",
           MAKE_ADMIN_HANDLER(handlerReady), false, false},
          {"/stats", "print server stats", MAKE_ADMIN_HANDLER(handlerStats), false, false},
          {"/stats/prometheus", "print server stats in prometheus format",
           MAKE_ADMIN_HANDLER(handlerPrometheusStats), false, false},
          {"/stats/recentlookups", "Show recent stat-name lookups",
           MAKE_ADMIN_HANDLER(handlerStatsRecentLookups), false, false},
          {"/stats/recentlookups/clear", "clear list of stat-name lookups and counter",
           MAKE_ADMIN_HANDLER(handlerStatsRecentLookupsClear), false, true},
          {"/stats/recentlookups/disable", "disable recording of reset stat-name lookup names",
           MAKE_ADMIN_HANDLER(handlerStatsRecentLookupsDisable), false, true},
          {"/stats/recentlookups/enable", "enable recording of reset stat-name lookup names",
           MAKE_ADMIN_HANDLER(handlerStatsRecentLookupsEnable), false, true},
          {"/listeners", "print listener info", MAKE_ADMIN_HANDLER(handlerListenerInfo), false,
           false},
          {"/runtime", "print runtime values", MAKE_ADMIN_HANDLER(handlerRuntime), false, false},
          {"/runtime_modify", "modify runtime values", MAKE_ADMIN_HANDLER(handlerRuntimeModify),
           false, true},
      },
      date_provider_(server.dispatcher().timeSource()),
      admin_filter_chain_(std::make_shared<AdminFilterChain>()) {}

Http::ServerConnectionPtr AdminImpl::createCodec(Network::Connection& connection,
                                                 const Buffer::Instance& data,
                                                 Http::ServerConnectionCallbacks& callbacks) {
  return Http::ConnectionManagerUtility::autoCreateCodec(
      connection, data, callbacks, server_.stats(), Http::Http1Settings(), Http::Http2Settings(),
      maxRequestHeadersKb(), maxRequestHeadersCount());
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
  callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr{new AdminFilter(*this)});
}

Http::Code AdminImpl::runCallback(absl::string_view path_and_query,
                                  Http::HeaderMap& response_headers, Buffer::Instance& response,
                                  AdminStream& admin_stream) {
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
        const absl::string_view method =
            admin_stream.getRequestHeaders().Method()->value().getStringView();
        if (method != Http::Headers::get().MethodValues.Post) {
          ENVOY_LOG(error, "admin path \"{}\" mutates state, method={} rather than POST",
                    handler.prefix_, method);
          code = Http::Code::MethodNotAllowed;
          response.add(fmt::format("Method {} not allowed, POST required.", method));
          break;
        }
      }
      code = handler.handler_(path_and_query, response_headers, response, admin_stream);
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

Http::Code AdminImpl::handlerHelp(absl::string_view, Http::HeaderMap&, Buffer::Instance& response,
                                  AdminStream&) {
  response.add("admin commands are:\n");

  // Prefix order is used during searching, but for printing do them in alpha order.
  for (const UrlHandler* handler : sortedHandlers()) {
    response.add(fmt::format("  {}: {}\n", handler->prefix_, handler->help_text_));
  }
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerAdminHome(absl::string_view, Http::HeaderMap& response_headers,
                                       Buffer::Instance& response, AdminStream&) {
  response_headers.insertContentType().value().setReference(
      Http::Headers::get().ContentTypeValues.Html);

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
  const uint size_before_removal = handlers_.size();
  handlers_.remove_if(
      [&prefix](const UrlHandler& entry) { return prefix == entry.prefix_ && entry.removable_; });
  if (handlers_.size() != size_before_removal) {
    return true;
  }
  return false;
}

Http::Code AdminImpl::request(absl::string_view path_and_query, absl::string_view method,
                              Http::HeaderMap& response_headers, std::string& body) {
  AdminFilter filter(*this);
  Http::HeaderMapImpl request_headers;
  request_headers.insertMethod().value(method.data(), method.size());
  filter.decodeHeaders(request_headers, false);
  Buffer::OwnedImpl response;

  Http::Code code = runCallback(path_and_query, response_headers, response, filter);
  populateFallbackResponseHeaders(code, response_headers);
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
    handler->addListener(*listener_);
  }
}

envoy::admin::v2alpha::ServerInfo::State Utility::serverState(Init::Manager::State state,
                                                              bool health_check_failed) {
  switch (state) {
  case Init::Manager::State::Uninitialized:
    return envoy::admin::v2alpha::ServerInfo::PRE_INITIALIZING;
  case Init::Manager::State::Initializing:
    return envoy::admin::v2alpha::ServerInfo::INITIALIZING;
  case Init::Manager::State::Initialized:
    return health_check_failed ? envoy::admin::v2alpha::ServerInfo::DRAINING
                               : envoy::admin::v2alpha::ServerInfo::LIVE;
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

} // namespace Server
} // namespace Envoy
