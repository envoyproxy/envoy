#include "contrib/peak_ewma/load_balancing_policies/source/host_data.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PeakEwma {

class HostDataTest : public ::testing::Test {
protected:
  static constexpr size_t kTestMaxSamples = 100; // Test with default buffer size
  PeakEwmaHostLbPolicyData host_data_{kTestMaxSamples};
};

TEST_F(HostDataTest, InitialState) {
  // Verify initial state
  EXPECT_EQ(host_data_.getEwmaRtt(), 0.0);

  // Verify indices start at 0
  auto range = host_data_.getNewSampleRange();
  EXPECT_EQ(range.first, 0);  // last_processed_index
  EXPECT_EQ(range.second, 0); // write_index
}

TEST_F(HostDataTest, RecordSingleSample) {
  const double rtt_ms = 25.5;
  const uint64_t timestamp_ns = 1234567890ULL;

  host_data_.recordRttSample(rtt_ms, timestamp_ns);

  // Verify range shows one new sample
  auto range = host_data_.getNewSampleRange();
  EXPECT_EQ(range.first, 0);  // last_processed
  EXPECT_EQ(range.second, 1); // write_index incremented

  // Verify sample data is stored correctly
  EXPECT_EQ(host_data_.rtt_samples_[0].load(), rtt_ms);
  EXPECT_EQ(host_data_.timestamps_[0].load(), timestamp_ns);
}

TEST_F(HostDataTest, RecordMultipleSamples) {
  const std::vector<std::pair<double, uint64_t>> samples = {
      {10.0, 1000}, {20.0, 2000}, {30.0, 3000}, {15.0, 4000}};

  // Record all samples
  for (const auto& [rtt, timestamp] : samples) {
    host_data_.recordRttSample(rtt, timestamp);
  }

  // Verify range
  auto range = host_data_.getNewSampleRange();
  EXPECT_EQ(range.first, 0);
  EXPECT_EQ(range.second, 4);

  // Verify all samples are stored correctly
  for (size_t i = 0; i < samples.size(); ++i) {
    EXPECT_EQ(host_data_.rtt_samples_[i].load(), samples[i].first);
    EXPECT_EQ(host_data_.timestamps_[i].load(), samples[i].second);
  }
}

TEST_F(HostDataTest, RingBufferWraparound) {
  // Fill buffer beyond capacity
  const size_t total_samples = kTestMaxSamples + 10;

  for (size_t i = 0; i < total_samples; ++i) {
    host_data_.recordRttSample(i * 1.0, i * 1000);
  }

  // Verify write_index wrapped around
  auto range = host_data_.getNewSampleRange();
  EXPECT_EQ(range.second, total_samples); // write_index continues incrementing

  // Verify latest samples overwrote old ones
  // Sample at index 0 should be from iteration (kTestMaxSamples)
  const size_t expected_value_at_0 = kTestMaxSamples;
  EXPECT_EQ(host_data_.rtt_samples_[0].load(), expected_value_at_0 * 1.0);
  EXPECT_EQ(host_data_.timestamps_[0].load(), expected_value_at_0 * 1000);
}

TEST_F(HostDataTest, SampleProcessing) {
  // Record some samples
  host_data_.recordRttSample(10.0, 1000);
  host_data_.recordRttSample(20.0, 2000);
  host_data_.recordRttSample(30.0, 3000);

  // Get samples to process
  auto range = host_data_.getNewSampleRange();
  EXPECT_EQ(range.first, 0);
  EXPECT_EQ(range.second, 3);

  // Mark first two samples as processed
  host_data_.markSamplesProcessed(2);

  // Check updated range
  range = host_data_.getNewSampleRange();
  EXPECT_EQ(range.first, 2);  // Updated last_processed
  EXPECT_EQ(range.second, 3); // write_index unchanged

  // Add more samples
  host_data_.recordRttSample(40.0, 4000);

  // Verify range now shows one unprocessed sample
  range = host_data_.getNewSampleRange();
  EXPECT_EQ(range.first, 2);  // Still at processed position
  EXPECT_EQ(range.second, 4); // New write_index
}

TEST_F(HostDataTest, EwmaUpdates) {
  // Initial EWMA should be 0
  EXPECT_EQ(host_data_.getEwmaRtt(), 0.0);

  // Update EWMA
  const double ewma1 = 15.5;
  const uint64_t timestamp1 = 5000000;
  host_data_.updateEwma(ewma1, timestamp1);

  EXPECT_EQ(host_data_.getEwmaRtt(), ewma1);
  EXPECT_EQ(host_data_.last_update_timestamp_.load(), timestamp1);

  // Update again
  const double ewma2 = 22.3;
  const uint64_t timestamp2 = 6000000;
  host_data_.updateEwma(ewma2, timestamp2);

  EXPECT_EQ(host_data_.getEwmaRtt(), ewma2);
  EXPECT_EQ(host_data_.last_update_timestamp_.load(), timestamp2);
}

TEST_F(HostDataTest, ThreadSafetyScenario) {
  // Simulate concurrent access pattern
  // This test verifies the basic atomicity but doesn't guarantee
  // full thread safety (that would require more complex testing)

  // Multiple "writers" record samples
  std::vector<std::pair<double, uint64_t>> samples = {{5.0, 1000}, {10.0, 2000}, {15.0, 3000},
                                                      {7.5, 1500}, {12.5, 2500}, {20.0, 4000}};

  for (const auto& [rtt, timestamp] : samples) {
    host_data_.recordRttSample(rtt, timestamp);
  }

  // "Main thread" processes samples
  auto range = host_data_.getNewSampleRange();
  EXPECT_EQ(range.second, samples.size());

  // Process all samples
  host_data_.markSamplesProcessed(range.second);

  // Update EWMA
  host_data_.updateEwma(12.5, 5000);

  // Verify final state
  EXPECT_EQ(host_data_.getEwmaRtt(), 12.5);

  auto final_range = host_data_.getNewSampleRange();
  EXPECT_EQ(final_range.first, samples.size());
  EXPECT_EQ(final_range.second, samples.size());
}

TEST_F(HostDataTest, EdgeCaseValues) {
  // Test with edge case values
  host_data_.recordRttSample(0.0, 0);    // Zero RTT
  host_data_.recordRttSample(-1.0, 100); // Negative RTT (shouldn't happen but handle gracefully)
  host_data_.recordRttSample(999999.0, std::numeric_limits<uint64_t>::max()); // Large values

  // Verify samples were recorded
  auto range = host_data_.getNewSampleRange();
  EXPECT_EQ(range.second, 3);

  EXPECT_EQ(host_data_.rtt_samples_[0].load(), 0.0);
  EXPECT_EQ(host_data_.rtt_samples_[1].load(), -1.0);
  EXPECT_EQ(host_data_.rtt_samples_[2].load(), 999999.0);

  // Test EWMA edge cases
  host_data_.updateEwma(0.0, 0);
  EXPECT_EQ(host_data_.getEwmaRtt(), 0.0);

  host_data_.updateEwma(std::numeric_limits<double>::max(), std::numeric_limits<uint64_t>::max());
  EXPECT_EQ(host_data_.getEwmaRtt(), std::numeric_limits<double>::max());
}

TEST_F(HostDataTest, ProcessingWithRingBufferWraparound) {
  // Fill buffer completely
  for (size_t i = 0; i < kTestMaxSamples; ++i) {
    host_data_.recordRttSample(i * 1.0, i * 1000);
  }

  // Process half the samples
  const size_t half = kTestMaxSamples / 2;
  host_data_.markSamplesProcessed(half);

  // Add more samples (causing wraparound)
  for (size_t i = 0; i < 20; ++i) {
    host_data_.recordRttSample((i + 1000) * 1.0, (i + 1000) * 1000);
  }

  // Verify range calculation with wraparound
  auto range = host_data_.getNewSampleRange();
  EXPECT_EQ(range.first, half);
  EXPECT_EQ(range.second, kTestMaxSamples + 20);

  // The actual samples to process span across the wraparound
  size_t samples_to_process = range.second - range.first;
  EXPECT_EQ(samples_to_process, half + 20);
}

TEST_F(HostDataTest, ConfigurableBufferSizes) {
  // Test different buffer sizes to verify max_samples_per_host functionality

  // Small buffer (10 samples)
  {
    PeakEwmaHostLbPolicyData small_buffer_host_data{10};
    EXPECT_EQ(small_buffer_host_data.max_samples_, 10);

    // Fill small buffer
    for (size_t i = 0; i < 15; ++i) {
      small_buffer_host_data.recordRttSample(i * 1.0, i * 1000);
    }

    // Verify wraparound at smaller buffer size
    // Sample at index 0 should be from iteration 10 (overwrote initial 0)
    EXPECT_EQ(small_buffer_host_data.rtt_samples_[0].load(), 10.0);
    EXPECT_EQ(small_buffer_host_data.timestamps_[0].load(), 10000);
  }

  // Large buffer (500 samples)
  {
    PeakEwmaHostLbPolicyData large_buffer_host_data{500};
    EXPECT_EQ(large_buffer_host_data.max_samples_, 500);

    // Fill with many samples without wraparound
    for (size_t i = 0; i < 300; ++i) {
      large_buffer_host_data.recordRttSample(i * 2.0, i * 2000);
    }

    // Verify no wraparound occurred - sample 0 should still be original
    EXPECT_EQ(large_buffer_host_data.rtt_samples_[0].load(), 0.0);
    EXPECT_EQ(large_buffer_host_data.timestamps_[0].load(), 0);

    // Verify last sample is correctly placed
    EXPECT_EQ(large_buffer_host_data.rtt_samples_[299].load(), 598.0);
    EXPECT_EQ(large_buffer_host_data.timestamps_[299].load(), 598000);
  }

  // Edge case: single sample buffer
  {
    PeakEwmaHostLbPolicyData single_buffer_host_data{1};
    EXPECT_EQ(single_buffer_host_data.max_samples_, 1);

    // Each new sample should overwrite the single slot
    single_buffer_host_data.recordRttSample(1.0, 1000);
    EXPECT_EQ(single_buffer_host_data.rtt_samples_[0].load(), 1.0);

    single_buffer_host_data.recordRttSample(2.0, 2000);
    EXPECT_EQ(single_buffer_host_data.rtt_samples_[0].load(), 2.0);

    single_buffer_host_data.recordRttSample(3.0, 3000);
    EXPECT_EQ(single_buffer_host_data.rtt_samples_[0].load(), 3.0);
  }
}

} // namespace PeakEwma
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
