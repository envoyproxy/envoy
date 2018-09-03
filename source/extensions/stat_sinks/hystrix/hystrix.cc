#include "extensions/stat_sinks/hystrix/hystrix.h"

#include <chrono>
#include <ctime>
#include <iostream>
#include <sstream>

#include "envoy/stats/scope.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"
#include "common/config/well_known_names.h"
#include "common/http/headers.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "fmt/printf.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Hystrix {

const uint64_t HystrixSink::DEFAULT_NUM_BUCKETS;
ClusterStatsCache::ClusterStatsCache(const std::string& cluster_name)
    : cluster_name_(cluster_name) {}

void ClusterStatsCache::printToStream(std::stringstream& out_str) {
  const std::string cluster_name_prefix = absl::StrCat(cluster_name_, ".");

  printRollingWindow(absl::StrCat(cluster_name_prefix, "success"), success_, out_str);
  printRollingWindow(absl::StrCat(cluster_name_prefix, "errors"), errors_, out_str);
  printRollingWindow(absl::StrCat(cluster_name_prefix, "timeouts"), timeouts_, out_str);
  printRollingWindow(absl::StrCat(cluster_name_prefix, "rejected"), rejected_, out_str);
  printRollingWindow(absl::StrCat(cluster_name_prefix, "total"), total_, out_str);
}

void ClusterStatsCache::printRollingWindow(absl::string_view name, RollingWindow rolling_window,
                                           std::stringstream& out_str) {
  out_str << name << " | ";
  for (auto specific_stat_vec_itr = rolling_window.begin();
       specific_stat_vec_itr != rolling_window.end(); ++specific_stat_vec_itr) {
    out_str << *specific_stat_vec_itr << " | ";
  }
  out_str << std::endl;
}

void HystrixSink::addHistogramToStream(const QuantileLatencyMap& latency_map, absl::string_view key,
                                       std::stringstream& ss) {
  // TODO: Consider if we better use join here
  ss << ", \"" << key << "\": {";
  bool is_first = true;
  for (const std::pair<double, double>& element : latency_map) {
    const std::string quantile = fmt::sprintf("%g", element.first * 100);
    HystrixSink::addDoubleToStream(quantile, element.second, ss, is_first);
    is_first = false;
  }
  ss << "}";
}

// Add new value to rolling window, in place of oldest one.
void HystrixSink::pushNewValue(RollingWindow& rolling_window, uint64_t value) {
  if (rolling_window.empty()) {
    rolling_window.resize(window_size_, value);
  } else {
    rolling_window[current_index_] = value;
  }
}

uint64_t HystrixSink::getRollingValue(RollingWindow rolling_window) {

  if (rolling_window.empty()) {
    return 0;
  }
  // If the counter was reset, the result is negative
  // better return 0, will be back to normal once one rolling window passes.
  if (rolling_window[current_index_] < rolling_window[(current_index_ + 1) % window_size_]) {
    return 0;
  } else {
    return rolling_window[current_index_] - rolling_window[(current_index_ + 1) % window_size_];
  }
}

void HystrixSink::updateRollingWindowMap(const Upstream::ClusterInfo& cluster_info,
                                         ClusterStatsCache& cluster_stats_cache) {
  const std::string cluster_name = cluster_info.name();
  Upstream::ClusterStats& cluster_stats = cluster_info.stats();
  Stats::Scope& cluster_stats_scope = cluster_info.statsScope();

  // Combining timeouts+retries - retries are counted  as separate requests
  // (alternative: each request including the retries counted as 1).
  uint64_t timeouts = cluster_stats.upstream_rq_timeout_.value() +
                      cluster_stats.upstream_rq_per_try_timeout_.value();

  pushNewValue(cluster_stats_cache.timeouts_, timeouts);

  // Combining errors+retry errors - retries are counted as separate requests
  // (alternative: each request including the retries counted as 1)
  // since timeouts are 504 (or 408), deduce them from here ("-" sign).
  // Timeout retries were not counted here anyway.
  uint64_t errors = cluster_stats_scope.counter("upstream_rq_5xx").value() +
                    cluster_stats_scope.counter("retry.upstream_rq_5xx").value() +
                    cluster_stats_scope.counter("upstream_rq_4xx").value() +
                    cluster_stats_scope.counter("retry.upstream_rq_4xx").value() -
                    cluster_stats.upstream_rq_timeout_.value();

  pushNewValue(cluster_stats_cache.errors_, errors);

  uint64_t success = cluster_stats_scope.counter("upstream_rq_2xx").value();
  pushNewValue(cluster_stats_cache.success_, success);

  uint64_t rejected = cluster_stats.upstream_rq_pending_overflow_.value();
  pushNewValue(cluster_stats_cache.rejected_, rejected);

  // should not take from upstream_rq_total since it is updated before its components,
  // leading to wrong results such as error percentage higher than 100%
  uint64_t total = errors + timeouts + success + rejected;
  pushNewValue(cluster_stats_cache.total_, total);

  ENVOY_LOG(trace, "{}", printRollingWindows());
}

void HystrixSink::resetRollingWindow() { cluster_stats_cache_map_.clear(); }

void HystrixSink::addStringToStream(absl::string_view key, absl::string_view value,
                                    std::stringstream& info, bool is_first) {
  std::string quoted_value = absl::StrCat("\"", value, "\"");
  addInfoToStream(key, quoted_value, info, is_first);
}

void HystrixSink::addIntToStream(absl::string_view key, uint64_t value, std::stringstream& info,
                                 bool is_first) {
  addInfoToStream(key, std::to_string(value), info, is_first);
}

void HystrixSink::addDoubleToStream(absl::string_view key, double value, std::stringstream& info,
                                    bool is_first) {
  addInfoToStream(key, std::to_string(value), info, is_first);
}

void HystrixSink::addInfoToStream(absl::string_view key, absl::string_view value,
                                  std::stringstream& info, bool is_first) {
  if (!is_first) {
    info << ", ";
  }
  std::string added_info = absl::StrCat("\"", key, "\": ", value);
  info << added_info;
}

void HystrixSink::addHystrixCommand(ClusterStatsCache& cluster_stats_cache,
                                    absl::string_view cluster_name,
                                    uint64_t max_concurrent_requests, uint64_t reporting_hosts,
                                    std::chrono::milliseconds rolling_window_ms,
                                    const QuantileLatencyMap& histogram, std::stringstream& ss) {

  std::time_t currentTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

  ss << "data: {";
  addStringToStream("type", "HystrixCommand", ss, true);
  addStringToStream("name", cluster_name, ss);
  addStringToStream("group", "NA", ss);
  addIntToStream("currentTime", static_cast<uint64_t>(currentTime), ss);
  addInfoToStream("isCircuitBreakerOpen", "false", ss);

  uint64_t errors = getRollingValue(cluster_stats_cache.errors_);
  uint64_t timeouts = getRollingValue(cluster_stats_cache.timeouts_);
  uint64_t rejected = getRollingValue(cluster_stats_cache.rejected_);
  uint64_t total = getRollingValue(cluster_stats_cache.total_);

  uint64_t error_rate = total == 0 ? 0 : (100 * (errors + timeouts + rejected)) / total;

  addIntToStream("errorPercentage", error_rate, ss);
  addIntToStream("errorCount", errors, ss);
  addIntToStream("requestCount", total, ss);
  addIntToStream("rollingCountCollapsedRequests", 0, ss);
  addIntToStream("rollingCountExceptionsThrown", 0, ss);
  addIntToStream("rollingCountFailure", errors, ss);
  addIntToStream("rollingCountFallbackFailure", 0, ss);
  addIntToStream("rollingCountFallbackRejection", 0, ss);
  addIntToStream("rollingCountFallbackSuccess", 0, ss);
  addIntToStream("rollingCountResponsesFromCache", 0, ss);

  // Envoy's "circuit breaker" has similar meaning to hystrix's isolation
  // so we count upstream_rq_pending_overflow and present it as rollingCountSemaphoreRejected
  addIntToStream("rollingCountSemaphoreRejected", rejected, ss);

  // Hystrix's short circuit is not similar to Envoy's since it is triggered by 503 responses
  // there is no parallel counter in Envoy since as a result of errors (outlier detection)
  // requests are not rejected, but rather the node is removed from load balancer healthy pool.
  addIntToStream("rollingCountShortCircuited", 0, ss);
  addIntToStream("rollingCountSuccess", getRollingValue(cluster_stats_cache.success_), ss);
  addIntToStream("rollingCountThreadPoolRejected", 0, ss);
  addIntToStream("rollingCountTimeout", timeouts, ss);
  addIntToStream("rollingCountBadRequests", 0, ss);
  addIntToStream("currentConcurrentExecutionCount", 0, ss);
  addStringToStream("latencyExecute_mean", "null", ss);
  addHistogramToStream(histogram, "latencyExecute", ss);
  addIntToStream("propertyValue_circuitBreakerRequestVolumeThreshold", 0, ss);
  addIntToStream("propertyValue_circuitBreakerSleepWindowInMilliseconds", 0, ss);
  addIntToStream("propertyValue_circuitBreakerErrorThresholdPercentage", 0, ss);
  addInfoToStream("propertyValue_circuitBreakerForceOpen", "false", ss);
  addInfoToStream("propertyValue_circuitBreakerForceClosed", "true", ss);
  addStringToStream("propertyValue_executionIsolationStrategy", "SEMAPHORE", ss);
  addIntToStream("propertyValue_executionIsolationThreadTimeoutInMilliseconds", 0, ss);
  addInfoToStream("propertyValue_executionIsolationThreadInterruptOnTimeout", "false", ss);
  addIntToStream("propertyValue_executionIsolationSemaphoreMaxConcurrentRequests",
                 max_concurrent_requests, ss);
  addIntToStream("propertyValue_fallbackIsolationSemaphoreMaxConcurrentRequests", 0, ss);
  addInfoToStream("propertyValue_requestCacheEnabled", "false", ss);
  addInfoToStream("propertyValue_requestLogEnabled", "true", ss);
  addIntToStream("reportingHosts", reporting_hosts, ss);
  addIntToStream("propertyValue_metricsRollingStatisticalWindowInMilliseconds",
                 rolling_window_ms.count(), ss);

  ss << "}" << std::endl << std::endl;
}

void HystrixSink::addHystrixThreadPool(absl::string_view cluster_name, uint64_t queue_size,
                                       uint64_t reporting_hosts,
                                       std::chrono::milliseconds rolling_window_ms,
                                       std::stringstream& ss) {

  ss << "data: {";
  addIntToStream("currentPoolSize", 0, ss, true);
  addIntToStream("rollingMaxActiveThreads", 0, ss);
  addIntToStream("currentActiveCount", 0, ss);
  addIntToStream("currentCompletedTaskCount", 0, ss);
  addIntToStream("propertyValue_queueSizeRejectionThreshold", queue_size, ss);
  addStringToStream("type", "HystrixThreadPool", ss);
  addIntToStream("reportingHosts", reporting_hosts, ss);
  addIntToStream("propertyValue_metricsRollingStatisticalWindowInMilliseconds",
                 rolling_window_ms.count(), ss);
  addStringToStream("name", cluster_name, ss);
  addIntToStream("currentLargestPoolSize", 0, ss);
  addIntToStream("currentCorePoolSize", 0, ss);
  addIntToStream("currentQueueSize", 0, ss);
  addIntToStream("currentTaskCount", 0, ss);
  addIntToStream("rollingCountThreadsExecuted", 0, ss);
  addIntToStream("currentMaximumPoolSize", 0, ss);

  ss << "}" << std::endl << std::endl;
}

void HystrixSink::addClusterStatsToStream(ClusterStatsCache& cluster_stats_cache,
                                          absl::string_view cluster_name,
                                          uint64_t max_concurrent_requests,
                                          uint64_t reporting_hosts,
                                          std::chrono::milliseconds rolling_window_ms,
                                          const QuantileLatencyMap& histogram,
                                          std::stringstream& ss) {

  addHystrixCommand(cluster_stats_cache, cluster_name, max_concurrent_requests, reporting_hosts,
                    rolling_window_ms, histogram, ss);
  addHystrixThreadPool(cluster_name, max_concurrent_requests, reporting_hosts, rolling_window_ms,
                       ss);
}

const std::string HystrixSink::printRollingWindows() {
  std::stringstream out_str;

  for (auto& itr : cluster_stats_cache_map_) {
    ClusterStatsCache& cluster_stats_cache = *(itr.second);
    cluster_stats_cache.printToStream(out_str);
  }
  return out_str.str();
}

HystrixSink::HystrixSink(Server::Instance& server, const uint64_t num_buckets)
    : server_(server), current_index_(num_buckets > 0 ? num_buckets : DEFAULT_NUM_BUCKETS),
      window_size_(current_index_ + 1) {
  Server::Admin& admin = server_.admin();
  ENVOY_LOG(debug,
            "adding hystrix_event_stream endpoint to enable connection to hystrix dashboard");
  admin.addHandler("/hystrix_event_stream", "send hystrix event stream",
                   MAKE_ADMIN_HANDLER(handlerHystrixEventStream), false, false);
}

Http::Code HystrixSink::handlerHystrixEventStream(absl::string_view,
                                                  Http::HeaderMap& response_headers,
                                                  Buffer::Instance&,
                                                  Server::AdminStream& admin_stream) {

  response_headers.insertContentType().value().setReference(
      Http::Headers::get().ContentTypeValues.TextEventStream);
  response_headers.insertCacheControl().value().setReference(
      Http::Headers::get().CacheControlValues.NoCache);
  response_headers.insertConnection().value().setReference(
      Http::Headers::get().ConnectionValues.Close);
  response_headers.insertAccessControlAllowHeaders().value().setReference(
      AccessControlAllowHeadersValue.AllowHeadersHystrix);
  response_headers.insertAccessControlAllowOrigin().value().setReference(
      Http::Headers::get().AccessControlAllowOriginValue.All);
  response_headers.insertNoChunks().value().setReference("0");

  Http::StreamDecoderFilterCallbacks& stream_decoder_filter_callbacks =
      admin_stream.getDecoderFilterCallbacks();

  registerConnection(&stream_decoder_filter_callbacks);

  admin_stream.setEndStreamOnComplete(false); // set streaming

  // Separated out just so it's easier to understand
  auto on_destroy_callback = [this, &stream_decoder_filter_callbacks]() {
    ENVOY_LOG(debug, "stopped sending data to hystrix dashboard on port {}",
              stream_decoder_filter_callbacks.connection()->remoteAddress()->asString());

    // Unregister the callbacks from the sink so data is no longer encoded through them.
    unregisterConnection(&stream_decoder_filter_callbacks);
  };

  // Add the callback to the admin_filter list of callbacks
  admin_stream.addOnDestroyCallback(std::move(on_destroy_callback));

  ENVOY_LOG(debug, "started sending data to hystrix dashboard on port {}",
            stream_decoder_filter_callbacks.connection()->remoteAddress()->asString());
  return Http::Code::OK;
}

void HystrixSink::flush(Stats::Source& source) {
  if (callbacks_list_.empty()) {
    return;
  }
  incCounter();
  std::stringstream ss;
  Upstream::ClusterManager::ClusterInfoMap clusters = server_.clusterManager().clusters();

  // Save a map of the relevant histograms per cluster in a convenient format.
  std::unordered_map<std::string, QuantileLatencyMap> time_histograms;
  for (const Stats::ParentHistogramSharedPtr& histogram : source.cachedHistograms()) {
    if (histogram->tagExtractedName() == "cluster.upstream_rq_time") {
      // TODO(mrice32): add an Envoy utility function to look up and return a tag for a metric.
      auto it = std::find_if(histogram->tags().begin(), histogram->tags().end(),
                             [](const Stats::Tag& tag) {
                               return (tag.name_ == Config::TagNames::get().CLUSTER_NAME);
                             });

      // Make sure we found the cluster name tag
      ASSERT(it != histogram->tags().end());
      auto it_bool_pair = time_histograms.emplace(std::make_pair(it->value_, QuantileLatencyMap()));
      // Make sure histogram with this name was not already added
      ASSERT(it_bool_pair.second);
      QuantileLatencyMap& hist_map = it_bool_pair.first->second;

      const std::vector<double>& supported_quantiles =
          histogram->intervalStatistics().supportedQuantiles();
      for (size_t i = 0; i < supported_quantiles.size(); ++i) {
        // binary-search here is likely not worth it, as hystrix_quantiles has <10 elements.
        if (std::find(hystrix_quantiles.begin(), hystrix_quantiles.end(), supported_quantiles[i]) !=
            hystrix_quantiles.end()) {
          const double value = histogram->intervalStatistics().computedQuantiles()[i];
          if (!std::isnan(value)) {
            hist_map[supported_quantiles[i]] = value;
          }
        }
      }
    }
  }

  for (auto& cluster : clusters) {
    Upstream::ClusterInfoConstSharedPtr cluster_info = cluster.second.get().info();

    std::unique_ptr<ClusterStatsCache>& cluster_stats_cache_ptr =
        cluster_stats_cache_map_[cluster_info->name()];
    if (cluster_stats_cache_ptr == nullptr) {
      cluster_stats_cache_ptr = std::make_unique<ClusterStatsCache>(cluster_info->name());
    }

    // update rolling window with cluster stats
    updateRollingWindowMap(*cluster_info, *cluster_stats_cache_ptr);

    // append it to stream to be sent
    addClusterStatsToStream(
        *cluster_stats_cache_ptr, cluster_info->name(),
        cluster_info->resourceManager(Upstream::ResourcePriority::Default).pendingRequests().max(),
        cluster_info->statsScope().gauge("membership_total").value(), server_.statsFlushInterval(),
        time_histograms[cluster_info->name()], ss);
  }

  Buffer::OwnedImpl data;
  for (auto callbacks : callbacks_list_) {
    data.add(ss.str());
    callbacks->encodeData(data, false);
  }

  // send keep alive ping
  // TODO (@trabetti) : is it ok to send together with data?
  Buffer::OwnedImpl ping_data;
  for (auto callbacks : callbacks_list_) {
    ping_data.add(":\n\n");
    callbacks->encodeData(ping_data, false);
  }

  // check if any clusters were removed, and remove from cache
  if (clusters.size() < cluster_stats_cache_map_.size()) {
    for (auto it = cluster_stats_cache_map_.begin(); it != cluster_stats_cache_map_.end();) {
      if (clusters.find(it->first) == clusters.end()) {
        it = cluster_stats_cache_map_.erase(it);
      } else {
        ++it;
      }
    }
  }
}

void HystrixSink::registerConnection(Http::StreamDecoderFilterCallbacks* callbacks_to_register) {
  callbacks_list_.emplace_back(callbacks_to_register);
}

void HystrixSink::unregisterConnection(Http::StreamDecoderFilterCallbacks* callbacks_to_remove) {
  for (auto it = callbacks_list_.begin(); it != callbacks_list_.end(); ++it) {
    if ((*it)->streamId() == callbacks_to_remove->streamId()) {
      callbacks_list_.erase(it);
      break;
    }
  }
  // If there are no callbacks, clear the map to avoid stale values or having to keep updating the
  // map. When a new callback is assigned, the rollingWindow is initialized with current statistics
  // and within RollingWindow time, the results showed in the dashboard will be reliable
  if (callbacks_list_.empty()) {
    resetRollingWindow();
  }
}

} // namespace Hystrix
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
