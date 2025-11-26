#include "contrib/peak_ewma/load_balancing_policies/source/host_data.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PeakEwma {

PeakEwmaHostLbPolicyData::PeakEwmaHostLbPolicyData(size_t max_samples)
    : max_samples_(max_samples), rtt_samples_(max_samples), timestamps_(max_samples) {
  // Vectors are initialized with max_samples atomic elements, each default-initialized to 0
}

void PeakEwmaHostLbPolicyData::recordRttSample(double rtt_ms, uint64_t timestamp_ns) {
  size_t index = write_index_.fetch_add(1) % max_samples_; // Use dynamic size
  rtt_samples_[index].store(rtt_ms);
  timestamps_[index].store(timestamp_ns);
}

std::pair<size_t, size_t> PeakEwmaHostLbPolicyData::getNewSampleRange() const {
  size_t current_write = write_index_.load();
  size_t last_processed = last_processed_index_.load();
  return {last_processed, current_write};
}

void PeakEwmaHostLbPolicyData::markSamplesProcessed(size_t processed_index) {
  last_processed_index_.store(processed_index);
}

void PeakEwmaHostLbPolicyData::updateEwma(double ewma_ms, uint64_t timestamp_ns) {
  current_ewma_ms_.store(ewma_ms);
  last_update_timestamp_.store(timestamp_ns);
}

} // namespace PeakEwma
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
