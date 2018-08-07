#include "server/http/admin.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "envoy/admin/v2alpha/clusters.pb.h"
#include "envoy/admin/v2alpha/config_dump.pb.h"
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
#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/common/version.h"
#include "common/html/utility.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/http1/codec_impl.h"
#include "common/json/json_loader.h"
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

} // namespace

AdminFilter::AdminFilter(AdminImpl& parent) : parent_(parent) {}

Http::FilterHeadersStatus AdminFilter::decodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  request_headers_ = &headers;
  if (end_stream) {
    onComplete();
  }

  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus AdminFilter::decodeData(Buffer::Instance&, bool end_stream) {
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
  for (size_t i = 0; i < ARRAY_SIZE(spdlog::level::level_names); i++) {
    if (level == spdlog::level::level_names[i]) {
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
    response.add(fmt::format("{}::outlier::success_rate_average::{}\n", cluster_name,
                             outlier_detector->successRateAverage()));
    response.add(fmt::format("{}::outlier::success_rate_ejection_threshold::{}\n", cluster_name,
                             outlier_detector->successRateEjectionThreshold()));
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
    if (outlier_detector != nullptr && outlier_detector->successRateEjectionThreshold() > 0.0) {
      cluster_status.mutable_success_rate_ejection_threshold()->set_value(
          outlier_detector->successRateEjectionThreshold());
    }

    cluster_status.set_added_via_api(cluster_info->addedViaApi());

    for (auto& host_set : cluster.prioritySet().hostSetsPerPriority()) {
      for (auto& host : host_set->hosts()) {
        envoy::admin::v2alpha::HostStatus& host_status = *cluster_status.add_host_statuses();
        Network::Utility::addressToProtobufAddress(*host->address(),
                                                   *host_status.mutable_address());

        for (const Stats::CounterSharedPtr& counter : host->counters()) {
          auto& metric = (*host_status.mutable_stats())[counter->name()];
          metric.set_type(envoy::admin::v2alpha::SimpleMetric::COUNTER);
          metric.set_value(counter->value());
        }

        for (const Stats::GaugeSharedPtr& gauge : host->gauges()) {
          auto& metric = (*host_status.mutable_stats())[gauge->name()];
          metric.set_type(envoy::admin::v2alpha::SimpleMetric::GAUGE);
          metric.set_value(gauge->value());
        }

        envoy::admin::v2alpha::HostHealthStatus& health_status =
            *host_status.mutable_health_status();
        health_status.set_failed_active_health_check(
            host->healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
        health_status.set_failed_outlier_check(
            host->healthFlagGet(Upstream::Host::HealthFlag::FAILED_OUTLIER_CHECK));
        health_status.set_eds_health_status(
            host->healthFlagGet(Upstream::Host::HealthFlag::FAILED_EDS_HEALTH)
                ? envoy::api::v2::core::HealthStatus::UNHEALTHY
                : envoy::api::v2::core::HealthStatus::HEALTHY);
        double success_rate = host->outlierDetector().successRate();
        if (success_rate >= 0.0) {
          host_status.mutable_success_rate()->set_value(success_rate);
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
        std::map<std::string, uint64_t> all_stats;
        for (const Stats::CounterSharedPtr& counter : host->counters()) {
          all_stats[counter->name()] = counter->value();
        }

        for (const Stats::GaugeSharedPtr& gauge : host->gauges()) {
          all_stats[gauge->name()] = gauge->value();
        }

        for (auto stat : all_stats) {
          response.add(fmt::format("{}::{}::{}::{}\n", cluster.second.get().info()->name(),
                                   host->address()->asString(), stat.first, stat.second));
        }

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
        response.add(fmt::format("{}::{}::success_rate::{}\n", cluster.second.get().info()->name(),
                                 host->address()->asString(),
                                 host->outlierDetector().successRate()));
      }
    }
  }
}

Http::Code AdminImpl::handlerClusters(absl::string_view url, Http::HeaderMap& response_headers,
                                      Buffer::Instance& response, AdminStream&) {
  Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);
  auto it = query_params.find("format");

  if (it != query_params.end() && it->second == "json") {
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
  auto& config_dump_map = *(dump.mutable_configs());
  for (const auto& key_callback_pair : config_tracker_.getCallbacksMap()) {
    ProtobufTypes::MessagePtr message = key_callback_pair.second();
    RELEASE_ASSERT(message, "");
    ProtobufWkt::Any any_message;
    any_message.PackFrom(*message);
    config_dump_map[key_callback_pair.first] = any_message;
  }

  response_headers.insertContentType().value().setReference(
      Http::Headers::get().ContentTypeValues.Json);
  response.add(MessageUtil::getJsonStringFromMessage(dump, true)); // pretty-print
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
  if (!changeLogLevel(query_params)) {
    response.add("usage: /logging?<name>=<level> (change single level)\n");
    response.add("usage: /logging?level=<level> (change all levels)\n");
    response.add("levels: ");
    for (size_t i = 0; i < ARRAY_SIZE(spdlog::level::level_names); i++) {
      response.add(fmt::format("{} ", spdlog::level::level_names[i]));
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

Http::Code AdminImpl::handlerResetCounters(absl::string_view, Http::HeaderMap&,
                                           Buffer::Instance& response, AdminStream&) {
  for (const Stats::CounterSharedPtr& counter : server_.stats().counters()) {
    counter->reset();
  }

  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerServerInfo(absl::string_view, Http::HeaderMap&,
                                        Buffer::Instance& response, AdminStream&) {
  time_t current_time = time(nullptr);
  response.add(fmt::format("envoy {} {} {} {} {}\n", VersionInfo::version(),
                           server_.healthCheckFailed() ? "draining" : "live",
                           current_time - server_.startTimeCurrentEpoch(),
                           current_time - server_.startTimeFirstEpoch(),
                           server_.options().restartEpoch()));
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerStats(absl::string_view url, Http::HeaderMap& response_headers,
                                   Buffer::Instance& response, AdminStream& admin_stream) {
  Http::Code rc = Http::Code::OK;
  const Http::Utility::QueryParams params = Http::Utility::parseQueryString(url);

  const bool show_all = params.find("usedonly") == params.end();
  const bool has_format = !(params.find("format") == params.end());

  std::map<std::string, uint64_t> all_stats;
  for (const Stats::CounterSharedPtr& counter : server_.stats().counters()) {
    if (show_all || counter->used()) {
      all_stats.emplace(counter->name(), counter->value());
    }
  }

  for (const Stats::GaugeSharedPtr& gauge : server_.stats().gauges()) {
    if (show_all || gauge->used()) {
      all_stats.emplace(gauge->name(), gauge->value());
    }
  }

  if (has_format) {
    const std::string format_value = params.at("format");
    if (format_value == "json") {
      response_headers.insertContentType().value().setReference(
          Http::Headers::get().ContentTypeValues.Json);
      response.add(AdminImpl::statsAsJson(all_stats, server_.stats().histograms(), show_all));
    } else if (format_value == "prometheus") {
      return handlerPrometheusStats(url, response_headers, response, admin_stream);
    } else {
      response.add("usage: /stats?format=json  or /stats?format=prometheus \n");
      response.add("\n");
      rc = Http::Code::NotFound;
    }
  } else { // Display plain stats if format query param is not there.
    for (auto stat : all_stats) {
      response.add(fmt::format("{}: {}\n", stat.first, stat.second));
    }
    // TOOD(ramaraochavali): See the comment in ThreadLocalStoreImpl::histograms() for why we use a
    // multimap here. This makes sure that duplicate histograms get output. When shared storage is
    // implemented this can be switched back to a normal map.
    std::multimap<std::string, std::string> all_histograms;
    for (const Stats::ParentHistogramSharedPtr& histogram : server_.stats().histograms()) {
      if (show_all || histogram->used()) {
        all_histograms.emplace(histogram->name(), histogram->summary());
      }
    }
    for (auto histogram : all_histograms) {
      response.add(fmt::format("{}: {}\n", histogram.first, histogram.second));
    }
  }
  return rc;
}

Http::Code AdminImpl::handlerPrometheusStats(absl::string_view, Http::HeaderMap&,
                                             Buffer::Instance& response, AdminStream&) {
  PrometheusStatsFormatter::statsAsPrometheus(server_.stats().counters(), server_.stats().gauges(),
                                              response);
  return Http::Code::OK;
}

std::string PrometheusStatsFormatter::sanitizeName(const std::string& name) {
  std::string stats_name = name;
  std::replace(stats_name.begin(), stats_name.end(), '.', '_');
  std::replace(stats_name.begin(), stats_name.end(), '-', '_');
  return stats_name;
}

std::string PrometheusStatsFormatter::formattedTags(const std::vector<Stats::Tag>& tags) {
  std::vector<std::string> buf;
  for (const Stats::Tag& tag : tags) {
    buf.push_back(fmt::format("{}=\"{}\"", sanitizeName(tag.name_), tag.value_));
  }
  return StringUtil::join(buf, ",");
}

std::string PrometheusStatsFormatter::metricName(const std::string& extractedName) {
  // Add namespacing prefix to avoid conflicts, as per best practice:
  // https://prometheus.io/docs/practices/naming/#metric-names
  // Also, naming conventions on https://prometheus.io/docs/concepts/data_model/
  return fmt::format("envoy_{0}", sanitizeName(extractedName));
}

// TODO(ramaraochavali): Add summary histogram output for Prometheus.
uint64_t
PrometheusStatsFormatter::statsAsPrometheus(const std::vector<Stats::CounterSharedPtr>& counters,
                                            const std::vector<Stats::GaugeSharedPtr>& gauges,
                                            Buffer::Instance& response) {
  std::unordered_set<std::string> metric_type_tracker;
  for (const auto& counter : counters) {
    const std::string tags = formattedTags(counter->tags());
    const std::string metric_name = metricName(counter->tagExtractedName());
    if (metric_type_tracker.find(metric_name) == metric_type_tracker.end()) {
      metric_type_tracker.insert(metric_name);
      response.add(fmt::format("# TYPE {0} counter\n", metric_name));
    }
    response.add(fmt::format("{0}{{{1}}} {2}\n", metric_name, tags, counter->value()));
  }

  for (const auto& gauge : gauges) {
    const std::string tags = formattedTags(gauge->tags());
    const std::string metric_name = metricName(gauge->tagExtractedName());
    if (metric_type_tracker.find(metric_name) == metric_type_tracker.end()) {
      metric_type_tracker.insert(metric_name);
      response.add(fmt::format("# TYPE {0} gauge\n", metric_name));
    }
    response.add(fmt::format("{0}{{{1}}} {2}\n", metric_name, tags, gauge->value()));
  }
  return metric_type_tracker.size();
}

std::string
AdminImpl::statsAsJson(const std::map<std::string, uint64_t>& all_stats,
                       const std::vector<Stats::ParentHistogramSharedPtr>& all_histograms,
                       const bool show_all, const bool pretty_print) {
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Value stats_array(rapidjson::kArrayType);
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
  for (auto stat : all_stats) {
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
    if (show_all || histogram->used()) {
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

Http::Code AdminImpl::handlerListenerInfo(absl::string_view, Http::HeaderMap& response_headers,
                                          Buffer::Instance& response, AdminStream&) {
  response_headers.insertContentType().value().setReference(
      Http::Headers::get().ContentTypeValues.Json);
  std::list<std::string> listeners;
  for (auto listener : server_.listenerManager().listeners()) {
    listeners.push_back(listener.get().socket().localAddress()->asString());
  }
  response.add(Json::Factory::listAsJsonString(listeners));
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerCerts(absl::string_view, Http::HeaderMap&, Buffer::Instance& response,
                                   AdminStream&) {
  // This set is used to track distinct certificates. We may have multiple listeners, upstreams, etc
  // using the same cert.
  std::unordered_set<std::string> context_info_set;
  std::string context_format = "{{\n\t\"ca_cert\": \"{}\",\n\t\"cert_chain\": \"{}\"\n}}\n";
  server_.sslContextManager().iterateContexts([&](const Ssl::Context& context) -> void {
    context_info_set.insert(fmt::format(context_format, context.getCaCertInformation(),
                                        context.getCertChainInformation()));
  });

  std::string cert_result_string;
  for (const std::string& context_info : context_info_set) {
    cert_result_string += context_info;
  }
  response.add(cert_result_string);
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
      const auto& entry_value = it == layer->values().end() ? "" : it->second.string_value_;
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

std::string AdminImpl::runtimeAsJson(
    const std::vector<std::pair<std::string, Runtime::Snapshot::Entry>>& entries) {
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Value entries_array(rapidjson::kArrayType);
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
  for (const auto& entry : entries) {
    Value entry_obj;
    entry_obj.SetObject();

    entry_obj.AddMember("name", {entry.first.c_str(), allocator}, allocator);

    Value entry_value;
    if (entry.second.uint_value_) {
      entry_value.SetUint64(entry.second.uint_value_.value());
    } else {
      entry_value.SetString(entry.second.string_value_.c_str(), allocator);
    }
    entry_obj.AddMember("value", entry_value, allocator);

    entries_array.PushBack(entry_obj, allocator);
  }
  document.AddMember("runtime", entries_array, allocator);
  rapidjson::StringBuffer strbuf;
  rapidjson::PrettyWriter<StringBuffer> writer(strbuf);
  document.Accept(writer);
  return strbuf.GetString();
}

Http::Code AdminImpl::handlerRuntimeModify(absl::string_view url, Http::HeaderMap&,
                                           Buffer::Instance& response, AdminStream&) {
  const Http::Utility::QueryParams params = Http::Utility::parseQueryString(url);
  if (params.empty()) {
    response.add("usage: /runtime_modify?key1=value1&key2=value2&keyN=valueN\n");
    response.add("use an empty value to remove a previously added override");
    return Http::Code::BadRequest;
  }
  std::unordered_map<std::string, std::string> overrides;
  overrides.insert(params.begin(), params.end());
  server_.runtime().mergeValues(overrides);
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

AdminImpl::NullRouteConfigProvider::NullRouteConfigProvider()
    : config_(new Router::NullConfigImpl()) {}

AdminImpl::AdminImpl(const std::string& access_log_path, const std::string& profile_path,
                     const std::string& address_out_path,
                     Network::Address::InstanceConstSharedPtr address, Server::Instance& server,
                     Stats::ScopePtr&& listener_scope)
    : server_(server), profile_path_(profile_path),
      socket_(new Network::TcpListenSocket(address, nullptr, true)),
      stats_(Http::ConnectionManagerImpl::generateStats("http.admin.", server_.stats())),
      tracing_stats_(
          Http::ConnectionManagerImpl::generateTracingStats("http.admin.", no_op_store_)),
      handlers_{
          {"/", "Admin home page", MAKE_ADMIN_HANDLER(handlerAdminHome), false, false},
          {"/certs", "print certs on machine", MAKE_ADMIN_HANDLER(handlerCerts), false, false},
          {"/clusters", "upstream cluster status", MAKE_ADMIN_HANDLER(handlerClusters), false,
           false},
          {"/config_dump", "dump current Envoy configs (experimental)",
           MAKE_ADMIN_HANDLER(handlerConfigDump), false, false},
          {"/cpuprofiler", "enable/disable the CPU profiler",
           MAKE_ADMIN_HANDLER(handlerCpuProfiler), false, true},
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
          {"/quitquitquit", "exit the server", MAKE_ADMIN_HANDLER(handlerQuitQuitQuit), false,
           true},
          {"/reset_counters", "reset all counters to zero",
           MAKE_ADMIN_HANDLER(handlerResetCounters), false, true},
          {"/server_info", "print server version/status information",
           MAKE_ADMIN_HANDLER(handlerServerInfo), false, false},
          {"/stats", "print server stats", MAKE_ADMIN_HANDLER(handlerStats), false, false},
          {"/stats/prometheus", "print server stats in prometheus format",
           MAKE_ADMIN_HANDLER(handlerPrometheusStats), false, false},
          {"/listeners", "print listener addresses", MAKE_ADMIN_HANDLER(handlerListenerInfo), false,
           false},
          {"/runtime", "print runtime values", MAKE_ADMIN_HANDLER(handlerRuntime), false, false},
          {"/runtime_modify", "modify runtime values", MAKE_ADMIN_HANDLER(handlerRuntimeModify),
           false, true},
      },

      // TODO(jsedgwick) add /runtime_reset endpoint that removes all admin-set values
      listener_(*this, std::move(listener_scope)),
      admin_filter_chain_(std::make_shared<AdminFilterChain>()) {

  if (!address_out_path.empty()) {
    std::ofstream address_out_file(address_out_path);
    if (!address_out_file) {
      ENVOY_LOG(critical, "cannot open admin address output file {} for writing.",
                address_out_path);
    } else {
      address_out_file << socket_->localAddress()->asString();
    }
  }

  // TODO(mattklein123): Allow admin to use normal access logger extension loading and avoid the
  // hard dependency here.
  access_logs_.emplace_back(new Extensions::AccessLoggers::File::FileAccessLog(
      access_log_path, {}, AccessLog::AccessLogFormatUtils::defaultAccessLogFormatter(),
      server.accessLogManager()));
}

Http::ServerConnectionPtr AdminImpl::createCodec(Network::Connection& connection,
                                                 const Buffer::Instance&,
                                                 Http::ServerConnectionCallbacks& callbacks) {
  return Http::ServerConnectionPtr{
      new Http::Http1::ServerConnectionImpl(connection, callbacks, Http::Http1Settings())};
}

bool AdminImpl::createNetworkFilterChain(Network::Connection& connection,
                                         const std::vector<Network::FilterFactoryCb>&) {
  connection.addReadFilter(Network::ReadFilterSharedPtr{new Http::ConnectionManagerImpl(
      *this, server_.drainManager(), server_.random(), server_.httpTracer(), server_.runtime(),
      server_.localInfo(), server_.clusterManager())});
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
          code = Http::Code::BadRequest;
          response.add("Invalid request; POST required");
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
    const std::string& url = handler->prefix_;

    // For handlers that mutate state, render the link as a button in a POST form,
    // rather than an anchor tag. This should discourage crawlers that find the /
    // page from accidentally mutating all the server state by GETting all the hrefs.
    const char* link_format =
        handler->mutates_server_state_
            ? "<form action='{}' method='post' class='home-form'><button>{}</button></form>"
            : "<a href='{}'>{}</a>";
    const std::string link = fmt::format(link_format, url, url);

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

Http::Code AdminImpl::request(absl::string_view path, const Http::Utility::QueryParams& params,
                              absl::string_view method, Http::HeaderMap& response_headers,
                              std::string& body) {
  AdminFilter filter(*this);
  Http::HeaderMapImpl request_headers;
  request_headers.insertMethod().value(method.data(), method.size());
  filter.decodeHeaders(request_headers, false);
  std::string path_and_query = absl::StrCat(path, Http::Utility::queryParamsToString(params));
  Buffer::OwnedImpl response;

  // TODO(jmarantz): rather than serializing params here and then re-parsing in the handler,
  // change the callback signature to take the query-params separately.
  Http::Code code = runCallback(path_and_query, response_headers, response, filter);
  populateFallbackResponseHeaders(code, response_headers);
  body = response.toString();
  return code;
}

} // namespace Server
} // namespace Envoy
