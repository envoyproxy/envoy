#pragma once

#include <atomic>
#include <cstdint>
#include <utility>
#include <vector>

#include "envoy/upstream/load_balancer.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PeakEwma {

/**
 * Host-attached atomic ring buffer for RTT samples.
 *
 * Stores RTT samples and EWMA state directly in Host objects using atomic variables
 * for thread-safe access. Workers write samples, main thread processes them.
 */
struct PeakEwmaHostLbPolicyData : public Upstream::HostLbPolicyData {
  // Constructor that accepts configurable buffer size
  explicit PeakEwmaHostLbPolicyData(size_t max_samples);

  // Configurable buffer size (replaces kMaxSamples constant)
  const size_t max_samples_;

  // Atomic ring buffer for RTT samples (lock-free writes from workers)
  std::vector<std::atomic<double>> rtt_samples_;
  std::vector<std::atomic<uint64_t>> timestamps_;

  // Index management (atomic for thread safety)
  std::atomic<size_t> write_index_{0};          // Workers increment atomically
  std::atomic<size_t> last_processed_index_{0}; // Main thread tracks processed

  // Current EWMA state (main thread writes, workers read)
  std::atomic<double> current_ewma_ms_{0.0};
  std::atomic<uint64_t> last_update_timestamp_{0};

  // Lock-free sample recording (called from worker threads)
  void recordRttSample(double rtt_ms, uint64_t timestamp_ns);

  // Get range of new samples to process (main thread only)
  std::pair<size_t, size_t> getNewSampleRange() const;

  // Mark samples as processed (main thread only)
  void markSamplesProcessed(size_t processed_index);

  // Update EWMA atomically (main thread only)
  void updateEwma(double ewma_ms, uint64_t timestamp_ns);

  // Get current EWMA (workers read)
  double getEwmaRtt() const { return current_ewma_ms_.load(); }
};

} // namespace PeakEwma
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
