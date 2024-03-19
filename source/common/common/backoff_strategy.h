#pragma once

#include <cstdint>
#include <memory>

#include "envoy/common/backoff_strategy.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/core/v3/backoff.pb.h"
#include "envoy/config/core/v3/backoff.pb.validate.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {

/**
 * Implementation of BackOffStrategy that uses a fully jittered exponential backoff algorithm.
 */
class JitteredExponentialBackOffStrategy : public BackOffStrategy {

public:
  /**
   * Constructs fully jittered backoff strategy.
   * @param base_interval the base interval to be used for next backoff computation. It should be
   * greater than zero and less than or equal to max_interval.
   * @param max_interval the cap on the next backoff value.
   * @param random the random generator.
   */
  JitteredExponentialBackOffStrategy(uint64_t base_interval, uint64_t max_interval,
                                     Random::RandomGenerator& random);

  // BackOffStrategy methods
  uint64_t nextBackOffMs() override;
  void reset() override;
  void reset(uint64_t base_interval) override {
    base_interval_ = base_interval;
    reset();
  }

  /**
   * Checks if a time interval is greater than the maximum time interval configured for a backoff
   * strategy.
   * @param interval time interval to be checked.
   * @return returns true if interval is greater than the maximum time interval
   */
  bool isOverTimeLimit(uint64_t interval_ms) const override { return interval_ms > max_interval_; }

private:
  uint64_t base_interval_;
  const uint64_t max_interval_{};
  const uint64_t doubling_limit_{max_interval_ / 2u};
  uint64_t next_interval_;
  Random::RandomGenerator& random_;
};

using JitteredExponentialBackOffStrategyPtr = std::unique_ptr<JitteredExponentialBackOffStrategy>;

/**
 * Implementation of BackOffStrategy that returns random values in the range
 * [min_interval, 1.5 * min_interval).
 */
class JitteredLowerBoundBackOffStrategy : public BackOffStrategy {
public:
  /**
   * Constructs fully jittered backoff strategy.
   * @param min_interval the lower bound on the next backoff value. It must be greater than one.
   * @param random the random generator.
   */
  JitteredLowerBoundBackOffStrategy(uint64_t min_interval, Random::RandomGenerator& random);

  // BackOffStrategy methods
  uint64_t nextBackOffMs() override;
  void reset() override {}
  void reset(uint64_t min_interval) override { min_interval_ = min_interval; }
  bool isOverTimeLimit(uint64_t) const override { return false; } // no max interval.

private:
  uint64_t min_interval_;
  Random::RandomGenerator& random_;
};

/**
 * Implementation of BackOffStrategy that uses a fixed backoff.
 */
class FixedBackOffStrategy : public BackOffStrategy {

public:
  /**
   * Constructs fixed backoff strategy.
   * @param interval_ms the fixed backoff duration. It should be greater than zero.
   */
  FixedBackOffStrategy(uint64_t interval_ms);

  // BackOffStrategy methods.
  uint64_t nextBackOffMs() override;
  void reset() override {}
  void reset(uint64_t interval_ms) override { interval_ms_ = interval_ms; }
  bool isOverTimeLimit(uint64_t) const override { return false; } // no max interval.

private:
  uint64_t interval_ms_;
};

class BackOffStrategyUtils {
public:
  static absl::Status
  validateBackOffStrategyConfig(envoy::config::core::v3::BackoffStrategy backoff_strategy,
                                uint64_t default_base_interval_ms, uint64_t max_interval_factor) {
    uint64_t base_interval_ms =
        PROTOBUF_GET_MS_OR_DEFAULT(backoff_strategy, base_interval, default_base_interval_ms);
    uint64_t max_interval_ms = PROTOBUF_GET_MS_OR_DEFAULT(backoff_strategy, max_interval,
                                                          base_interval_ms * max_interval_factor);

    if (max_interval_ms < base_interval_ms) {
      return absl::InvalidArgumentError("max_interval must be greater or equal to base_interval");
    }

    return absl::OkStatus();
  }
};

} // namespace Envoy
