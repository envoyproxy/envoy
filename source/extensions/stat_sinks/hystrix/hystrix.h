#pragma once

#include <map>
#include <memory>
#include <vector>

#include "envoy/server/admin.h"
#include "envoy/server/instance.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/sink.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Hystrix {

using RollingWindow = std::vector<uint64_t>;
using RollingStatsMap = std::map<const std::string, RollingWindow>;

using QuantileLatencyMap = std::unordered_map<double, double>;
static const std::vector<double> hystrix_quantiles = {0,    0.25, 0.5,   0.75, 0.90,
                                                      0.95, 0.99, 0.995, 1};

struct {
  const std::string AllowHeadersHystrix{"Accept, Cache-Control, X-Requested-With, Last-Event-ID"};
} AccessControlAllowHeadersValue;

struct ClusterStatsCache {
  ClusterStatsCache(const std::string& cluster_name);

  void printToStream(std::stringstream& out_str);
  void printRollingWindow(absl::string_view name, RollingWindow rolling_window,
                          std::stringstream& out_str);
  std::string cluster_name_;

  // Rolling windows
  RollingWindow errors_;
  RollingWindow success_;
  RollingWindow total_;
  RollingWindow timeouts_;
  RollingWindow rejected_;
};

using ClusterStatsCachePtr = std::unique_ptr<ClusterStatsCache>;

class HystrixSink : public Stats::Sink, public Logger::Loggable<Logger::Id::hystrix> {
public:
  HystrixSink(Server::Instance& server, uint64_t num_buckets);
  Http::Code handlerHystrixEventStream(absl::string_view, Http::ResponseHeaderMap& response_headers,
                                       Buffer::Instance&, Server::AdminStream& admin_stream);
  void flush(Stats::MetricSnapshot& snapshot) override;
  void onHistogramComplete(const Stats::Histogram&, uint64_t) override{};

  /**
   * Register a new connection.
   */
  void registerConnection(Http::StreamDecoderFilterCallbacks* callbacks_to_register);

  /**
   * Remove registered connection.
   */
  void unregisterConnection(Http::StreamDecoderFilterCallbacks* callbacks_to_remove);

  /**
   * Add new value to top of rolling window, pushing out the oldest value.
   */
  void pushNewValue(RollingWindow& rolling_window, uint64_t value);

  /**
   * Increment pointer of next value to add to rolling window.
   */
  void incCounter() { current_index_ = (current_index_ + 1) % window_size_; }

  /**
   * Generate the streams to be sent to hystrix dashboard.
   */
  void addClusterStatsToStream(ClusterStatsCache& cluster_stats_cache,
                               absl::string_view cluster_name, uint64_t max_concurrent_requests,
                               uint64_t reporting_hosts,
                               std::chrono::milliseconds rolling_window_ms,
                               const QuantileLatencyMap& histogram, std::stringstream& ss);

  /**
   * Calculate values needed to create the stream and write into the map.
   */
  void updateRollingWindowMap(const Upstream::ClusterInfo& cluster_info,
                              ClusterStatsCache& cluster_stats_cache);
  /**
   * Clear map.
   */
  void resetRollingWindow();

  /**
   * Return string representing current state of the map. for DEBUG.
   */
  const std::string printRollingWindows();

  /**
   * Get the statistic's value change over the rolling window time frame.
   */
  uint64_t getRollingValue(RollingWindow rolling_window);

  /**
   * Format the given key and value to "key"=value, and adding to the stringstream.
   */
  static void addInfoToStream(absl::string_view key, absl::string_view value,
                              std::stringstream& info, bool is_first = false);

  /**
   * Format the given key and double value to "key"=<string of uint64_t>, and adding to the
   * stringstream.
   */
  static void addDoubleToStream(absl::string_view key, double value, std::stringstream& info,
                                bool is_first);

  /**
   * Format the given key and absl::string_view value to "key"="value", and adding to the
   * stringstream.
   */
  static void addStringToStream(absl::string_view key, absl::string_view value,
                                std::stringstream& info, bool is_first = false);

  /**
   * Format the given key and uint64_t value to "key"=<string of uint64_t>, and adding to the
   * stringstream.
   */
  static void addIntToStream(absl::string_view key, uint64_t value, std::stringstream& info,
                             bool is_first = false);

  static void addHistogramToStream(const QuantileLatencyMap& latency_map, absl::string_view key,
                                   std::stringstream& ss);

private:
  /**
   * Generate HystrixCommand event stream.
   */
  void addHystrixCommand(ClusterStatsCache& cluster_stats_cache, absl::string_view cluster_name,
                         uint64_t max_concurrent_requests, uint64_t reporting_hosts,
                         std::chrono::milliseconds rolling_window_ms,
                         const QuantileLatencyMap& histogram, std::stringstream& ss);

  /**
   * Generate HystrixThreadPool event stream.
   */
  void addHystrixThreadPool(absl::string_view cluster_name, uint64_t queue_size,
                            uint64_t reporting_hosts, std::chrono::milliseconds rolling_window_ms,
                            std::stringstream& ss);

  std::vector<Http::StreamDecoderFilterCallbacks*> callbacks_list_;
  Server::Instance& server_;
  uint64_t current_index_;
  const uint64_t window_size_;
  static const uint64_t DEFAULT_NUM_BUCKETS = 10;

  // Map from cluster names to a struct of all of that cluster's stat windows.
  std::unordered_map<std::string, ClusterStatsCachePtr> cluster_stats_cache_map_;

  // Saved StatNames for fast comparisons in loop.
  Stats::StatNamePool stat_name_pool_;
  const Stats::StatName cluster_name_;
  const Stats::StatName cluster_upstream_rq_time_;
  const Stats::StatName membership_total_;
  const Stats::StatName retry_upstream_rq_4xx_;
  const Stats::StatName retry_upstream_rq_5xx_;
  const Stats::StatName upstream_rq_2xx_;
  const Stats::StatName upstream_rq_4xx_;
  const Stats::StatName upstream_rq_5xx_;
};

using HystrixSinkPtr = std::unique_ptr<HystrixSink>;

} // namespace Hystrix
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
