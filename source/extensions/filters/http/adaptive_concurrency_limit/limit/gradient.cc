#include "extensions/filters/http/adaptive_concurrency_limit/limit/gradient.h"

#include <cmath>

#include "envoy/registry/registry.h"

#include "common/access_log/access_log_formatter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrencyLimit {
namespace Limit {

Gradient::Gradient(
    const envoy::config::filter::http::adaptive_concurrency_limit::v2alpha::
        AdaptiveConcurrencyLimit::Limit::CommonConfig& common_config,
    const envoy::config::filter::http::adaptive_concurrency_limit::v2alpha::GradientLimitConfig&
        limit_specific_config,
    Runtime::RandomGenerator& random, const std::string& cluster_name)
    : random_(random), cluster_name_(cluster_name),
      min_limit_(PROTOBUF_GET_WRAPPED_REQUIRED(common_config, min_limit)),
      max_limit_(PROTOBUF_GET_WRAPPED_REQUIRED(common_config, max_limit)),
      smoothing_(PROTOBUF_GET_WRAPPED_REQUIRED(limit_specific_config, smoothing)),
      rtt_tolerance_(PROTOBUF_GET_WRAPPED_REQUIRED(limit_specific_config, rtt_tolerance)),
      probe_interval_(limit_specific_config.has_probe_interval()
                          ? absl::optional<uint32_t>{limit_specific_config.probe_interval().value()}
                          : absl::nullopt),
      probe_countdown_(nextProbeCountdown()),
      estimated_limit_((PROTOBUF_GET_WRAPPED_REQUIRED(common_config, initial_limit))) {}

double Gradient::getQueueSize(double estimated_limit) {
  // The square root of the limit is used to get queue size
  // because it is better than a fixed queue size that becomes too
  // small for large limits. Moreover, it prevents the limit from growing
  // too much by slowing down growth as the limit grows.
  return sqrt(estimated_limit);
}

absl::optional<uint32_t> Gradient::nextProbeCountdown() {
  if (probe_interval_.has_value()) {
    return probe_interval_.value() + (random_.random() % probe_interval_.value());
  }
  return absl::nullopt;
}

void Gradient::update(const Common::SampleWindow& sample) {
  if (sample.getSampleCount() == 0) {
    ENVOY_LOG(debug, "Received SampleWindow with 0 samples for '{}' for its Gradient limit update",
              cluster_name_);
    return;
  }

  const std::chrono::nanoseconds rtt = sample.getAverageRtt();
  const double queue_size = getQueueSize(estimated_limit_);

  // Reduce the limit to reduce traffic and probe for a new min_rtt_.
  if (probe_interval_.has_value() && probe_countdown_.has_value()) {
    probe_countdown_ = absl::optional<uint32_t>{probe_countdown_.value() - 1};

    if (probe_countdown_.value() <= 0) {
      probe_countdown_ = nextProbeCountdown();
      estimated_limit_ = std::max(min_limit_, static_cast<uint32_t>(queue_size));
      min_rtt_.clear();
      std::cerr << "Probe min rtt, estimated limit: " << estimated_limit_ << std::endl;
      ENVOY_LOG(debug, "Probe min rtt for '{}', estimated limit: {}", cluster_name_,
                estimated_limit_);
      return;
    }
  }

  min_rtt_.set(rtt);
  const std::chrono::nanoseconds min_rtt = min_rtt_.get();

  // The gradient is bounded between 0.5 and 1.0. 1.0 means that there is no queueing in the
  // upstream within the configured rtt_tolerance, so the limit can be expanded.
  // Anything less than 1.0 indicates that there is
  // queueing, and thus the limit has to shrink. The lower bound is 0.5 to prevent
  // aggressive load shedding due to outliers.
  //
  // For example, lets pretend that the min_rtt_ is 10ms, the rtt_tolerance_ is 2.0,
  // and rtt for the sample is 15ms. This means that the gradient is going to be 1.0, and
  // the estimated limit will increase. On the other hand if the rtt for the sample is greater
  // than min_rtt_ * rtt_tolerance, then the gradient will be less than 1.0 and the limit
  // will be reduced.
  const double gradient = std::max(0.5, std::min(1.0, rtt_tolerance_ * min_rtt / rtt));

  uint32_t new_limit;
  // Reduce the limit aggressively if there was a request failure.
  if (sample.didDrop()) {
    new_limit = estimated_limit_ / 2;
    // There is no need to grow the limit if less than half of the current limit is being used.
  } else if (sample.getMaxInFlightRequests() < estimated_limit_ / 2) {
    return;
    // Normal update to the limit.
  } else {
    new_limit = estimated_limit_ * gradient + queue_size;
  }

  // If the limit is shrinking, smoothing is used to control how aggresive the shrinking of the
  // limit actually is.
  if (new_limit < estimated_limit_) {
    new_limit = std::max(min_limit_, static_cast<uint32_t>(estimated_limit_ * (1 - smoothing_) +
                                                           smoothing_ * new_limit));
  }
  new_limit = std::max(static_cast<uint32_t>(queue_size),
                       std::min(max_limit_, static_cast<uint32_t>(new_limit)));

  ENVOY_LOG(debug,
            "New estimated_limit for '{}'={} min_rtt={} ms win_rtt={} ms queue_size={} gradient={} "
            "probe_countdown={}",
            cluster_name_, estimated_limit_,
            AccessLog::AccessLogFormatUtils::durationToString(min_rtt_.get()),
            AccessLog::AccessLogFormatUtils::durationToString(rtt), queue_size, gradient,
            probe_countdown_.value_or(-1));
  estimated_limit_ = new_limit;
}

std::unique_ptr<Upstream::Limit<Common::SampleWindow>> GradientFactory::createLimitFromProtoTyped(
    const envoy::config::filter::http::adaptive_concurrency_limit::v2alpha::
        AdaptiveConcurrencyLimit::Limit::CommonConfig& common_config,
    const envoy::config::filter::http::adaptive_concurrency_limit::v2alpha::GradientLimitConfig&
        limit_specific_config,
    Runtime::RandomGenerator& random, const std::string& cluster_name) {
  return std::make_unique<Gradient>(common_config, limit_specific_config, random, cluster_name);
}

/**
 * Static registration for the gradient limit factory. @see RegistryFactory.
 */
static Registry::RegisterFactory<
    GradientFactory,
    FactoryBase<
        envoy::config::filter::http::adaptive_concurrency_limit::v2alpha::GradientLimitConfig,
        Common::SampleWindow>>
    registered_;

} // namespace Limit
} // namespace AdaptiveConcurrencyLimit
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy