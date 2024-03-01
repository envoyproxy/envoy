#pragma once

#include <cstdint>
#include <string>

#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/sampler_config_provider.h"
#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/stream_summary.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * @brief Container for sampling exponent / multiplicity.
 * based on the "Space Saving algorithm", AKA "HeavyHitter"
 * See:
 * https://cse.hkust.edu.hk/~raywong/comp5331/References/EfficientComputationOfFrequentAndTop-kElementsInDataStreams.pdf
 *
 */
class SamplingState {
public:
  // Convert exponent to multiplicity
  [[nodiscard]] static uint32_t toMultiplicity(uint32_t exponent) { return 1 << exponent; }
  [[nodiscard]] uint32_t getExponent() const { return exponent_; }
  [[nodiscard]] uint32_t getMultiplicity() const { return toMultiplicity(exponent_); }
  void increaseExponent() { exponent_++; }
  void decreaseExponent() {
    if (exponent_ > 0) {
      exponent_--;
    }
  }

  explicit SamplingState(uint32_t exponent) : exponent_(exponent){};

  SamplingState() = default;

  /**
   * @brief Does a sampling decision based on random number attribute and multiplicity
   *
   * @param random_nr Random number used for sampling decision.
   * @return true if request should be sampled, false otherwise
   */
  bool shouldSample(const uint64_t random_nr) const { return (random_nr % getMultiplicity() == 0); }

private:
  uint32_t exponent_{0};
};

using StreamSummaryT = StreamSummary<std::string>;
using TopKListT = std::list<Counter<std::string>>;

/**
 * @brief Counts the requests per sampling key in the current period. Calculates the sampling
 * exponents based on the request count in the latest period.
 *
 */
class SamplingController : public Logger::Loggable<Logger::Id::tracing> {

public:
  explicit SamplingController(SamplerConfigProviderPtr sampler_config_provider)
      : stream_summary_(std::make_unique<StreamSummaryT>(STREAM_SUMMARY_SIZE)),
        sampler_config_provider_(std::move(sampler_config_provider)) {}

  /**
   * @brief Trigger calculating the sampling exponents based on the request count since last update
   *
   */
  void update();

  /**
   * @brief Get the Sampling State object for a sampling key
   *
   * @param sampling_key Sampling Key to search for
   * @return SamplingState Current Sampling State for key
   */
  SamplingState getSamplingState(const std::string& sampling_key) const;

  /**
   * @brief Returns the number of spans which would have been sampled in the last period using the
   * current sampling states
   *
   * @return effective count
   */
  uint64_t getEffectiveCount() const;

  /**
   * @brief Counts the occurrence of sampling_key
   *
   * @param sampling_key Sampling Key used to categorize the request
   */
  void offer(const std::string& sampling_key);

  /**
   * @brief Creates the Sampling Key which is used to categorize a request
   *
   * @param path_query The request path. May contain the query.
   * @param method The request method.
   * @return The sampling key.
   */
  static std::string getSamplingKey(const absl::string_view path_query,
                                    const absl::string_view method);

  static constexpr size_t STREAM_SUMMARY_SIZE{100};
  static constexpr uint32_t MAX_SAMPLING_EXPONENT = (1 << 4) - 1; // 15

private:
  using SamplingExponentsT = absl::flat_hash_map<std::string, SamplingState>;
  SamplingExponentsT sampling_exponents_;
  mutable absl::Mutex sampling_exponents_mutex_{};
  std::string rest_bucket_key_{};
  std::unique_ptr<StreamSummaryT> stream_summary_;
  uint64_t last_effective_count_{};
  mutable absl::Mutex stream_summary_mutex_{};
  SamplerConfigProviderPtr sampler_config_provider_;

  void logSamplingInfo(const TopKListT& top_k, const SamplingExponentsT& new_sampling_exponents,
                       uint64_t last_period_count, uint32_t total_wanted) const;

  static uint64_t calculateEffectiveCount(const TopKListT& top_k,
                                          const SamplingExponentsT& sampling_exponents);

  void calculateSamplingExponents(const TopKListT& top_k, uint32_t total_wanted,
                                  SamplingExponentsT& new_sampling_exponents) const;

  void update(const TopKListT& top_k, uint64_t last_period_count, uint32_t total_wanted);
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
