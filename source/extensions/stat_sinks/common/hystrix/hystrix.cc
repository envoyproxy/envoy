#include "extensions/stat_sinks/common/hystrix/hystrix.h"

#include <chrono>
#include <ctime>
#include <iostream>
#include <sstream>

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

#include "server/http/admin.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Common {

const uint64_t Hystrix::DEFAULT_NUM_OF_BUCKETS;

// Add new value to rolling window, in place of oldest one.
void Hystrix::pushNewValue(const std::string& key, uint64_t value) {
  // Create vector if do not exist.
  // TODO trabetti: why resize + value param didn't work without the if?
  if (rolling_stats_map_.find(key) == rolling_stats_map_.end()) {
    rolling_stats_map_[key].resize(num_of_buckets_, value);
  } else {
    rolling_stats_map_[key][current_index_] = value;
  }
}

uint64_t Hystrix::getRollingValue(absl::string_view cluster_name, absl::string_view stats) {
  std::string key;
  key = absl::StrCat("cluster.", cluster_name, ".", stats);
  if (rolling_stats_map_.find(key) != rolling_stats_map_.end()) {
    // If the counter was reset, the result is negative
    // better return 0, will be back to normal once one rolling window passes.
    if (rolling_stats_map_[key][current_index_] <
        rolling_stats_map_[key][(current_index_ + 1) % num_of_buckets_]) {
      return 0;
    } else {
      return rolling_stats_map_[key][current_index_] -
             rolling_stats_map_[key][(current_index_ + 1) % num_of_buckets_];
    }
  } else {
    return 0;
  }
}

void Hystrix::CreateCounterNameLookupForCluster(const std::string& cluster_name) {
  // Building lookup name map for all specific cluster values.
  // Every call to the updateRollingWindowMap function should get the appropriate name from the map.
  std::string cluster_name_with_prefix = absl::StrCat("cluster.", cluster_name, ".");
  counter_name_lookup[cluster_name]["upstream_rq_timeout"] =
      absl::StrCat(cluster_name_with_prefix, "upstream_rq_timeout");
  counter_name_lookup[cluster_name]["upstream_rq_per_try_timeout"] =
      absl::StrCat(cluster_name_with_prefix, "upstream_rq_per_try_timeout");
  counter_name_lookup[cluster_name]["timeouts"] =
      absl::StrCat(cluster_name_with_prefix, "timeouts");
  counter_name_lookup[cluster_name]["upstream_rq_5xx"] =
      absl::StrCat(cluster_name_with_prefix, "upstream_rq_5xx");
  counter_name_lookup[cluster_name]["retry.upstream_rq_5xx"] =
      absl::StrCat(cluster_name_with_prefix, "retry.upstream_rq_5xx");
  counter_name_lookup[cluster_name]["upstream_rq_4xx"] =
      absl::StrCat(cluster_name_with_prefix, "upstream_rq_4xx");
  counter_name_lookup[cluster_name]["retry.upstream_rq_4xx"] =
      absl::StrCat(cluster_name_with_prefix, "retry.upstream_rq_4xx");
  counter_name_lookup[cluster_name]["errors"] = absl::StrCat(cluster_name_with_prefix, "errors");
  counter_name_lookup[cluster_name]["upstream_rq_2xx"] =
      absl::StrCat(cluster_name_with_prefix, "upstream_rq_2xx");
  counter_name_lookup[cluster_name]["success"] = absl::StrCat(cluster_name_with_prefix, "success");
  counter_name_lookup[cluster_name]["upstream_rq_pending_overflow"] =
      absl::StrCat(cluster_name_with_prefix, "upstream_rq_pending_overflow");
  counter_name_lookup[cluster_name]["rejected"] =
      absl::StrCat(cluster_name_with_prefix, "rejected");
  counter_name_lookup[cluster_name]["total"] = absl::StrCat(cluster_name_with_prefix, "total");
}

void Hystrix::updateRollingWindowMap(std::map<std::string, uint64_t> current_stat_values,
                                     std::string cluster_name) {

  if (counter_name_lookup.find(cluster_name) == counter_name_lookup.end()) {
    CreateCounterNameLookupForCluster(cluster_name);
  }

  // Combining timeouts+retries - retries are counted  as separate requests
  // (alternative: each request including the retries counted as 1).
  uint64_t timeouts =
      current_stat_values[counter_name_lookup[cluster_name]["upstream_rq_timeout"]] +
      current_stat_values[counter_name_lookup[cluster_name]["upstream_rq_per_try_timeout"]];

  pushNewValue(counter_name_lookup[cluster_name]["timeouts"], timeouts);

  // Combining errors+retry errors - retries are counted as separate requests
  // (alternative: each request including the retries counted as 1)
  // since timeouts are 504 (or 408), deduce them from here ("-" sign).
  // Timeout retries were not counted here anyway.
  uint64_t errors =
      current_stat_values[counter_name_lookup[cluster_name]["upstream_rq_5xx"]] +
      current_stat_values[counter_name_lookup[cluster_name]["retry.upstream_rq_5xx"]] +
      current_stat_values[counter_name_lookup[cluster_name]["upstream_rq_4xx"]] +
      current_stat_values[counter_name_lookup[cluster_name]["retry.upstream_rq_4xx"]] -
      current_stat_values[counter_name_lookup[cluster_name]["upstream_rq_timeout"]];

  pushNewValue(counter_name_lookup[cluster_name]["errors"], errors);

  uint64_t success = current_stat_values[counter_name_lookup[cluster_name]["upstream_rq_2xx"]];
  pushNewValue(counter_name_lookup[cluster_name]["success"], success);

  uint64_t rejected =
      current_stat_values[counter_name_lookup[cluster_name]["upstream_rq_pending_overflow"]];
  pushNewValue(counter_name_lookup[cluster_name]["rejected"], rejected);

  // should not take from upstream_rq_total since it is updated before its components,
  // leading to wrong results such as error percentage higher than 100%
  uint64_t total = errors + timeouts + success + rejected;
  pushNewValue(counter_name_lookup[cluster_name]["total"], total);

  ENVOY_LOG(trace, "{}", printRollingWindow());
}

void Hystrix::resetRollingWindow() { rolling_stats_map_.clear(); }

void Hystrix::addStringToStream(absl::string_view key, absl::string_view value,
                                std::stringstream& info) {
  std::string quoted_value = absl::StrCat("\"", value, "\"");
  addInfoToStream(key, quoted_value, info);
}

void Hystrix::addIntToStream(absl::string_view key, uint64_t value, std::stringstream& info) {
  addInfoToStream(key, std::to_string(value), info);
}

void Hystrix::addInfoToStream(absl::string_view key, absl::string_view value,
                              std::stringstream& info) {
  if (!info.str().empty()) {
    info << ", ";
  }
  std::string added_info = absl::StrCat("\"", key, "\": ", value);
  info << added_info;
}

void Hystrix::addHystrixCommand(std::stringstream& ss, absl::string_view cluster_name,
                                uint64_t max_concurrent_requests, uint64_t reporting_hosts,
                                uint64_t rolling_window) {
  std::stringstream cluster_info;
  std::time_t currentTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  addStringToStream("type", "HystrixCommand", cluster_info);
  addStringToStream("name", cluster_name, cluster_info);
  addStringToStream("group", "NA", cluster_info);
  addIntToStream("currentTime", static_cast<uint64_t>(currentTime), cluster_info);
  addInfoToStream("isCircuitBreakerOpen", "false", cluster_info);

  uint64_t errors = getRollingValue(cluster_name, "errors");
  uint64_t timeouts = getRollingValue(cluster_name, "timeouts");
  uint64_t rejected = getRollingValue(cluster_name, "rejected");
  uint64_t total = getRollingValue(cluster_name, "total");

  uint64_t error_rate =
      total == 0
          ? 0
          : (static_cast<double>(errors + timeouts + rejected) / static_cast<double>(total)) * 100;

  addIntToStream("errorPercentage", error_rate, cluster_info);
  addIntToStream("errorCount", errors, cluster_info);
  addIntToStream("requestCount", total, cluster_info);
  addIntToStream("rollingCountCollapsedRequests", 0, cluster_info);
  addIntToStream("rollingCountExceptionsThrown", 0, cluster_info);
  addIntToStream("rollingCountFailure", errors, cluster_info);
  addIntToStream("rollingCountFallbackFailure", 0, cluster_info);
  addIntToStream("rollingCountFallbackRejection", 0, cluster_info);
  addIntToStream("rollingCountFallbackSuccess", 0, cluster_info);
  addIntToStream("rollingCountResponsesFromCache", 0, cluster_info);

  // Envoy's "circuit breaker" has similar meaning to hystrix's isolation
  // so we count upstream_rq_pending_overflow and present it as rejected
  addIntToStream("rollingCountSemaphoreRejected", rejected, cluster_info);

  // Hystrix's short circuit is not similar to Envoy's since it is triggered by 503 responses
  // there is no parallel counter in Envoy since as a result of errors (outlier detection)
  // requests are not rejected, but rather the node is removed from load balancer healthy pool.
  addIntToStream("rollingCountShortCircuited", 0, cluster_info);
  addIntToStream("rollingCountSuccess", getRollingValue(cluster_name, "success"), cluster_info);
  addIntToStream("rollingCountThreadPoolRejected", 0, cluster_info);
  addIntToStream("rollingCountTimeout", timeouts, cluster_info);
  addIntToStream("rollingCountBadRequests", 0, cluster_info);
  addIntToStream("currentConcurrentExecutionCount", 0, cluster_info);
  addIntToStream("latencyExecute_mean", 0, cluster_info);

  // TODO trabetti : add histogram information once available by PR #2932
  addInfoToStream(
      "latencyExecute",
      "{\"0\":0,\"25\":0,\"50\":0,\"75\":0,\"90\":0,\"95\":0,\"99\":0,\"99.5\":0,\"100\":0}",
      cluster_info);
  addIntToStream("propertyValue_circuitBreakerRequestVolumeThreshold", 0, cluster_info);
  addIntToStream("propertyValue_circuitBreakerSleepWindowInMilliseconds", 0, cluster_info);
  addIntToStream("propertyValue_circuitBreakerErrorThresholdPercentage", 0, cluster_info);
  addInfoToStream("propertyValue_circuitBreakerForceOpen", "false", cluster_info);
  addInfoToStream("propertyValue_circuitBreakerForceClosed", "true", cluster_info);
  addStringToStream("propertyValue_executionIsolationStrategy", "SEMAPHORE", cluster_info);
  addIntToStream("propertyValue_executionIsolationThreadTimeoutInMilliseconds", 0, cluster_info);
  addInfoToStream("propertyValue_executionIsolationThreadInterruptOnTimeout", "false",
                  cluster_info);
  addIntToStream("propertyValue_executionIsolationSemaphoreMaxConcurrentRequests",
                 max_concurrent_requests, cluster_info);
  addIntToStream("propertyValue_fallbackIsolationSemaphoreMaxConcurrentRequests", 0, cluster_info);
  addInfoToStream("propertyValue_requestCacheEnabled", "false", cluster_info);
  addInfoToStream("propertyValue_requestLogEnabled", "true", cluster_info);
  addIntToStream("reportingHosts", reporting_hosts, cluster_info);
  addIntToStream("propertyValue_metricsRollingStatisticalWindowInMilliseconds", rolling_window,
                 cluster_info);

  ss << "data: {" << cluster_info.str() << "}" << std::endl << std::endl;
}

void Hystrix::addHystrixThreadPool(std::stringstream& ss, absl::string_view cluster_name,
                                   uint64_t queue_size, uint64_t reporting_hosts,
                                   uint64_t rolling_window) {
  std::stringstream cluster_info;

  addIntToStream("currentPoolSize", 0, cluster_info);
  addIntToStream("rollingMaxActiveThreads", 0, cluster_info);
  addIntToStream("currentActiveCount", 0, cluster_info);
  addIntToStream("currentCompletedTaskCount", 0, cluster_info);
  addIntToStream("propertyValue_queueSizeRejectionThreshold", queue_size, cluster_info);
  addStringToStream("type", "HystrixThreadPool", cluster_info);
  addIntToStream("reportingHosts", reporting_hosts, cluster_info);
  addIntToStream("propertyValue_metricsRollingStatisticalWindowInMilliseconds", rolling_window,
                 cluster_info);
  addStringToStream("name", cluster_name, cluster_info);
  addIntToStream("currentLargestPoolSize", 0, cluster_info);
  addIntToStream("currentCorePoolSize", 0, cluster_info);
  addIntToStream("currentQueueSize", 0, cluster_info);
  addIntToStream("currentTaskCount", 0, cluster_info);
  addIntToStream("rollingCountThreadsExecuted", 0, cluster_info);
  addIntToStream("currentMaximumPoolSize", 0, cluster_info);

  ss << "data: {" << cluster_info.str() << "}" << std::endl << std::endl;
}

void Hystrix::getClusterStats(std::stringstream& ss, absl::string_view cluster_name,
                              uint64_t max_concurrent_requests, uint64_t reporting_hosts,
                              uint64_t rolling_window) {
  addHystrixCommand(ss, cluster_name, max_concurrent_requests, reporting_hosts, rolling_window);
  addHystrixThreadPool(ss, cluster_name, max_concurrent_requests, reporting_hosts, rolling_window);
}

const std::string Hystrix::printRollingWindow() const {
  std::stringstream out_str;

  for (auto stats_map_itr = rolling_stats_map_.begin(); stats_map_itr != rolling_stats_map_.end();
       ++stats_map_itr) {
    out_str << stats_map_itr->first << " | ";
    RollingStats rolling_stats = stats_map_itr->second;
    for (auto specific_stat_vec_itr = rolling_stats.begin();
         specific_stat_vec_itr != rolling_stats.end(); ++specific_stat_vec_itr) {
      out_str << *specific_stat_vec_itr << " | ";
    }
    out_str << std::endl;
  }
  return out_str.str();
}

namespace HystrixNameSpace {
HystrixSink::HystrixSink(Server::Instance& server) : stats_(new Hystrix()), server_(&server) {
  Server::Admin& admin = server_->admin();
  ENVOY_LOG(debug,
            "adding hystrix_event_stream endpoint to enable connection to hystrix dashboard");
  admin.addHandler("/hystrix_event_stream", "send hystrix event stream",
                   MAKE_ADMIN_HANDLER(handlerHystrixEventStream), false, false);
};

Http::Code HystrixSink::handlerHystrixEventStream(absl::string_view,
                                                  Http::HeaderMap& response_headers,
                                                  Buffer::Instance&,
                                                  Http::StreamDecoderFilterCallbacks* callbacks) {

  response_headers.insertContentType().value().setReference(
      Http::Headers::get().ContentTypeValues.TextEventStream);
  response_headers.insertCacheControl().value().setReference(
      Http::Headers::get().CacheControlValues.NoCache);
  response_headers.insertConnection().value().setReference(
      Http::Headers::get().ConnectionValues.Close);
  response_headers.insertAccessControlAllowHeaders().value().setReference(
      Http::Headers::get().AccessControlAllowHeadersValue.AccessControlAllowHeadersHystrix);
  response_headers.insertAccessControlAllowOrigin().value().setReference(
      Http::Headers::get().AccessControlAllowOriginValue.All);
  response_headers.insertNoChunks().value().setReference("0");

  registerConnection(callbacks);
  ENVOY_LOG(debug, "start sending data to hystrix dashboard on port {}",
            callbacks->connection()->localAddress()->asString());
  return Http::Code::OK;
}

void HystrixSink::beginFlush() { current_stat_values_.clear(); }

void HystrixSink::flushCounter(const Stats::Counter& counter, uint64_t) {
  if (callbacks_ == nullptr) {
    return;
  }
  if (counter.name().find("upstream_rq_") != std::string::npos) {
    current_stat_values_[counter.name()] = counter.value();
  }
}
// void HystrixSink::flushGauge(const Gauge& gauge, uint64_t value);
void HystrixSink::endFlush() {
  if (callbacks_ == nullptr)
    return;
  stats_->incCounter();
  for (auto& cluster : server_->clusterManager().clusters()) {
    stats_->updateRollingWindowMap(current_stat_values_, cluster.second.get().info()->name());
  }
  std::stringstream ss;
  for (auto& cluster : server_->clusterManager().clusters()) {
    stats_->getClusterStats(
        ss, cluster.second.get().info()->name(),
        cluster.second.get()
            .info()
            ->resourceManager(Upstream::ResourcePriority::Default)
            .pendingRequests()
            .max(),
        server_->stats()
            .gauge("cluster." + cluster.second.get().info()->name() + ".membership_total")
            .value(),
        server_->statsFlushInterval().count());
  }
  Buffer::OwnedImpl data;
  data.add(ss.str());
  callbacks_->encodeData(data, false);

  // send keep alive ping
  Buffer::OwnedImpl ping_data;
  ping_data.add(":\n\n");
  callbacks_->encodeData(ping_data, false);
}

void HystrixSink::registerConnection(Http::StreamDecoderFilterCallbacks* callbacks) {
  callbacks_ = callbacks;
}

// TODO (@trabetti) is this correct way - to set nullptr?
void HystrixSink::unregisterConnection() { callbacks_ = nullptr; }

} // namespace HystrixNameSpace
} // namespace Common
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
