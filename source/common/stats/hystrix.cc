#include "common/stats/hystrix.h"

#include <chrono>
#include <ctime>
#include <iostream>
#include <sstream>

#include "common/common/logger.h"

namespace Envoy {
namespace Stats {

const uint64_t Hystrix::DEFAULT_NUM_OF_BUCKETS;
const uint64_t Hystrix::ROLLING_WINDOW_IN_MS;
const uint64_t Hystrix::PING_INTERVAL_IN_MS;

// add new value to rolling window, in place of oldest one
void Hystrix::pushNewValue(std::string key, uint64_t value) {
  // create vector if do not exist
  if (rolling_stats_map_.find(key) == rolling_stats_map_.end()) {
    rolling_stats_map_[key].resize(num_of_buckets_, value);
  } else {
    rolling_stats_map_[key][current_index_] = value;
  }
}

uint64_t Hystrix::getRollingValue(std::string cluster_name, std::string stats) {
  std::string key = "cluster." + cluster_name + "." + stats;
  if (rolling_stats_map_.find(key) != rolling_stats_map_.end()) {
    // if the counter was reset, the result is negative
    // better return 0, will be back to normal once one rolling window passes
    // better idea what to return? could change the algorithm to keep last valid delta,
    // updated with pushNewValue and kept stable when the update is negative.
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

void Hystrix::updateRollingWindowMap(Stats::Store& stats, std::string cluster_name) {
  std::string prefix = "cluster." + cluster_name + ".";

  // combining timeouts+retries - retries are counted  as separate requests
  // (alternative: each request including the retries counted as 1)
  uint64_t timeouts = stats.counter(prefix + "upstream_rq_timeout").value() +
                      stats.counter(prefix + "upstream_rq_per_try_timeout").value();

  pushNewValue(prefix + "timeouts", timeouts);

  // combining errors+retry errors - retries are counted as separate requests
  // (alternative: each request including the retries counted as 1)
  // since timeouts are 504 (or 408), deduce them from here.
  // timeout retries were not counted here anyway.
  uint64_t errors = stats.counter(prefix + "upstream_rq_5xx").value() +
                    stats.counter(prefix + "retry.upstream_rq_5xx").value() +
                    stats.counter(prefix + "upstream_rq_4xx").value() +
                    stats.counter(prefix + "retry.upstream_rq_4xx").value() -
                    stats.counter(prefix + "upstream_rq_timeout").value();

  pushNewValue(prefix + "errors", errors);

  uint64_t success = stats.counter(prefix + "upstream_rq_2xx").value();
  pushNewValue(prefix + "success", success);

  uint64_t rejected = stats.counter(prefix + "upstream_rq_pending_overflow").value();
  pushNewValue(prefix + "rejected", rejected);

  // should not take from upstream_rq_total since it is updated before its components,
  // leading to wrong results such as error percentage higher than 100%
  uint64_t total = errors + timeouts + success + rejected;
  pushNewValue(prefix + "total", total);

  // TODO (@trabetti) : why does it fail compilation?
  // ENVOY_LOG(trace, "{}", printRollingWindow());
}

void Hystrix::resetRollingWindow() { rolling_stats_map_.clear(); }

void Hystrix::addStringToStream(std::string key, std::string value, std::stringstream& info) {
  addInfoToStream(key, "\"" + value + "\"", info);
}

void Hystrix::addIntToStream(std::string key, uint64_t value, std::stringstream& info) {
  addInfoToStream(key, std::to_string(value), info);
}

void Hystrix::addInfoToStream(std::string key, std::string value, std::stringstream& info) {
  if (!info.str().empty()) {
    info << ", ";
  }
  info << "\"" + key + "\": " + value;
}

void Hystrix::addHystrixCommand(std::stringstream& ss, std::string cluster_name,
                                uint64_t max_concurrent_requests, uint64_t reporting_hosts) {
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

  // Hystrix's short circuit is not similar to Envoy's since it is trrigered by 503 responses
  // there is no parallel counter in Envoy since as a result of errors (outlier detection)
  // requests are not rejected, but rather the node is removed from load balancer healthy pool
  addIntToStream("rollingCountShortCircuited", 0, cluster_info);
  addIntToStream("rollingCountSuccess", getRollingValue(cluster_name, "success"), cluster_info);
  addIntToStream("rollingCountThreadPoolRejected", 0, cluster_info);
  addIntToStream("rollingCountTimeout", timeouts, cluster_info);
  addIntToStream("rollingCountBadRequests", 0, cluster_info);
  addIntToStream("currentConcurrentExecutionCount", 0, cluster_info);
  addIntToStream("latencyExecute_mean", 0, cluster_info);

  // latency information can be  taken rom hystogram, which is only available to sinks
  // we should consider make this a sink so we can get this information
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
  addIntToStream("propertyValue_metricsRollingStatisticalWindowInMilliseconds",
                 ROLLING_WINDOW_IN_MS, cluster_info);

  ss << "data: {" << cluster_info.str() << "}" << std::endl << std::endl;
}

void Hystrix::addHystrixThreadPool(std::stringstream& ss, std::string cluster_name,
                                   uint64_t queue_size, uint64_t reporting_hosts) {
  std::stringstream cluster_info;

  addIntToStream("currentPoolSize", 0, cluster_info);
  addIntToStream("rollingMaxActiveThreads", 0, cluster_info);
  addIntToStream("currentActiveCount", 0, cluster_info);
  addIntToStream("currentCompletedTaskCount", 0, cluster_info);
  addIntToStream("propertyValue_queueSizeRejectionThreshold", queue_size, cluster_info);
  addStringToStream("type", "HystrixThreadPool", cluster_info);
  addIntToStream("reportingHosts", reporting_hosts, cluster_info);
  addIntToStream("propertyValue_metricsRollingStatisticalWindowInMilliseconds",
                 ROLLING_WINDOW_IN_MS, cluster_info);
  addStringToStream("name", cluster_name, cluster_info);
  addIntToStream("currentLargestPoolSize", 0, cluster_info);
  addIntToStream("currentCorePoolSize", 0, cluster_info);
  addIntToStream("currentQueueSize", 0, cluster_info);
  addIntToStream("currentTaskCount", 0, cluster_info);
  addIntToStream("rollingCountThreadsExecuted", 0, cluster_info);
  addIntToStream("currentMaximumPoolSize", 0, cluster_info);

  ss << "data: {" << cluster_info.str() << "}" << std::endl << std::endl;
}

void Hystrix::getClusterStats(std::stringstream& ss, std::string cluster_name,
                              uint64_t max_concurrent_requests, uint64_t reporting_hosts) {
  addHystrixCommand(ss, cluster_name, max_concurrent_requests, reporting_hosts);
  addHystrixThreadPool(ss, cluster_name, max_concurrent_requests, reporting_hosts);
}

std::string Hystrix::printRollingWindow() {
  std::stringstream out_str;

  for (std::map<std::string, RollingStats>::const_iterator it = rolling_stats_map_.begin();
       it != rolling_stats_map_.end(); ++it) {
    out_str << it->first << " | ";
    RollingStats rolling_stats = it->second;
    for (uint64_t i = 0; i < rolling_stats.size(); i++) {
      out_str << rolling_stats[i] << " | ";
    }
    out_str << std::endl;
  }
  return out_str.str();
}

} // namespace Stats
} // namespace Envoy
