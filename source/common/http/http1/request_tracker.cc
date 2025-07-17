#include "source/common/http/http1/request_tracker.h"

#include "envoy/common/time.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Http {
namespace Http1 {

Http1RequestTracker::Http1RequestTracker(Upstream::ClusterTrafficStats& traffic_stats)
    : traffic_stats_(traffic_stats) {
  ENVOY_LOG(debug, "HTTP/1.1 request tracker initialized with event-driven metric emission");
}

Http1RequestTracker::~Http1RequestTracker() = default;

void Http1RequestTracker::onConnectionEstablished(uint64_t connection_id) {
  auto& connection_data = active_connections_[connection_id];
  connection_data.request_count = 0;
  connection_data.created_time = std::chrono::steady_clock::now();
  
  ENVOY_LOG(trace, "HTTP/1.1 connection {} established", connection_id);
}

void Http1RequestTracker::onRequestStarted(uint64_t connection_id) {
  auto it = active_connections_.find(connection_id);
  if (it != active_connections_.end()) {
    it->second.request_count++;
    total_requests_tracked_++;
    
    ENVOY_LOG(trace, "HTTP/1.1 connection {} handled request {} (total requests tracked: {})",
              connection_id, it->second.request_count, total_requests_tracked_);
  } else {
    ENVOY_LOG(warn, "Request started on unknown HTTP/1.1 connection {}", connection_id);
  }
}

void Http1RequestTracker::onConnectionClosed(uint64_t connection_id) {
  auto it = active_connections_.find(connection_id);
  if (it != active_connections_.end()) {
    const uint32_t request_count = it->second.request_count;
    
    // Add to historical data for running average calculation
    completed_connection_request_counts_.push_back(request_count);
    
    // Limit historical data size to prevent unbounded growth
    if (completed_connection_request_counts_.size() > kMaxHistoricalEntries) {
      completed_connection_request_counts_.erase(completed_connection_request_counts_.begin());
    }
    
    total_connections_tracked_++;
    active_connections_.erase(it);
    
    ENVOY_LOG(trace, "HTTP/1.1 connection {} closed with {} requests (total connections tracked: {})",
              connection_id, request_count, total_connections_tracked_);
    
    // Calculate and emit running average immediately when connection closes
    calculateAndEmitRunningAverage();
  } else {
    ENVOY_LOG(warn, "Connection closed for unknown HTTP/1.1 connection {}", connection_id);
  }
}

void Http1RequestTracker::calculateAndEmitRunningAverage() {
  if (completed_connection_request_counts_.empty()) {
    // No completed connections yet, emit 0
    running_average_ = 0.0;
  } else {
    // Calculate simple moving average of completed connections
    uint64_t total_requests = 0;
    for (uint32_t count : completed_connection_request_counts_) {
      total_requests += count;
    }
    
    running_average_ = static_cast<double>(total_requests) / 
                      static_cast<double>(completed_connection_request_counts_.size());
  }
  
  // Emit the metric immediately
  traffic_stats_.upstream_rq_per_cx_http1_.set(
      static_cast<uint64_t>(running_average_ * 100)); // Multiply by 100 for precision
  
  ENVOY_LOG(trace, "Emitted HTTP/1.1 requests per connection metric: running average {:.2f} "
                   "(gauge value: {}), based on {} completed connections", 
            running_average_, static_cast<uint64_t>(running_average_ * 100),
            completed_connection_request_counts_.size());
}

} // namespace Http1
} // namespace Http
} // namespace Envoy
