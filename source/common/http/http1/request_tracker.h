#pragma once

#include <chrono>
#include <memory>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Http {
namespace Http1 {

/**
 * Tracks the number of requests handled per individual HTTP/1.1 upstream connection
 * and maintains a running average that is emitted as a metric when connections close.
 */
class Http1RequestTracker : public Logger::Loggable<Logger::Id::http> {
public:
  Http1RequestTracker(Upstream::ClusterTrafficStats& traffic_stats);

  ~Http1RequestTracker();

  /**
   * Called when a new HTTP/1.1 connection is established.
   * @param connection_id unique identifier for the connection
   */
  void onConnectionEstablished(uint64_t connection_id);

  /**
   * Called when a request is initiated on an HTTP/1.1 connection.
   * @param connection_id unique identifier for the connection
   */
  void onRequestStarted(uint64_t connection_id);

  /**
   * Called when an HTTP/1.1 connection is closed.
   * @param connection_id unique identifier for the connection
   */
  void onConnectionClosed(uint64_t connection_id);

private:
  struct ConnectionData {
    uint32_t request_count = 0;
    std::chrono::steady_clock::time_point created_time;
  };

  void calculateAndEmitRunningAverage();

  Upstream::ClusterTrafficStats& traffic_stats_;

  // Active connections being tracked
  absl::flat_hash_map<uint64_t, ConnectionData> active_connections_;

  // Historical data for calculating running average
  std::vector<uint32_t> completed_connection_request_counts_;
  static constexpr size_t kMaxHistoricalEntries = 1000;

  // Running statistics
  double running_average_ = 0.0;
  uint64_t total_connections_tracked_ = 0;
  uint64_t total_requests_tracked_ = 0;
};

using Http1RequestTrackerPtr = std::unique_ptr<Http1RequestTracker>;

} // namespace Http1
} // namespace Http
} // namespace Envoy
