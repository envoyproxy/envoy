#pragma once

#include <chrono>
#include <string>

#include "envoy/runtime/runtime.h"
#include "envoy/upstream/adaptive_concurrency_limit.h"

#include "extensions/filters/http/adaptive_concurrency_limit/common/common.h"
#include "extensions/filters/http/adaptive_concurrency_limit/limit/factory_base.h"
#include "extensions/filters/http/adaptive_concurrency_limit/limit/well_known_names.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrencyLimit {
namespace Limit {

/**
 * Concurrency limit algorithm that adjust the limit based on the gradient of change in the
 * samples minimum Round Trip Time (RTT) and absolute minimum RTT allowing for a queue of
 * square root of the current limit.
 */
class Gradient : public Upstream::Limit<Common::SampleWindow>,
                 Logger::Loggable<Logger::Id::upstream> {
public:
  Gradient(
      const envoy::config::filter::http::adaptive_concurrency_limit::v2alpha::
          AdaptiveConcurrencyLimit::Limit::CommonConfig& common_config,
      const envoy::config::filter::http::adaptive_concurrency_limit::v2alpha::GradientLimitConfig&
          config,
      Runtime::RandomGenerator& random, const std::string& cluster_name);

  // Upstream::Limit
  uint32_t getLimit() override { return estimated_limit_; }
  void update(const Common::SampleWindow&) override;

private:
  static double getQueueSize(double estimated_limit);
  absl::optional<uint32_t> nextProbeCountdown();

  Runtime::RandomGenerator& random_;
  // The name of the cluster this Limit is being estimated for.
  const std::string cluster_name_;
  const uint32_t min_limit_;
  const uint32_t max_limit_;
  const double smoothing_;
  const double rtt_tolerance_;
  const absl::optional<uint32_t> probe_interval_;

  absl::optional<uint32_t> probe_countdown_;
  Common::MinimumMeasurement<std::chrono::nanoseconds> min_rtt_;
  uint32_t estimated_limit_;
};

class GradientFactory
    : public FactoryBase<
          envoy::config::filter::http::adaptive_concurrency_limit::v2alpha::GradientLimitConfig,
          Common::SampleWindow> {
public:
  GradientFactory() : FactoryBase(Names::get().GRADIENT) {}
  ~GradientFactory() {}

private:
  std::unique_ptr<Upstream::Limit<Common::SampleWindow>> createLimitFromProtoTyped(
      const envoy::config::filter::http::adaptive_concurrency_limit::v2alpha::
          AdaptiveConcurrencyLimit::Limit::CommonConfig& common_config,
      const envoy::config::filter::http::adaptive_concurrency_limit::v2alpha::GradientLimitConfig&
          limit_specific_config,
      Runtime::RandomGenerator& random, const std::string& cluster_name) override;
};

} // namespace Limit
} // namespace AdaptiveConcurrencyLimit
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
