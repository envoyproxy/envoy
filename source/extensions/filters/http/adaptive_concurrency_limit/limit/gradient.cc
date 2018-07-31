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
  // FIX: lookup table for smaller estimated limits;
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

  // Reset or probe for a new noload RTT and a new estimatedLimit. It's necessary to cut the limit
  // in half to avoid having the limit drift upwards when the RTT is probed during heavy load.
  // To avoid decreasing the limit too much we don't allow it to go lower than the queueSize.
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

  // Rtt could be higher than rtt_noload because of smoothing rtt noload updates
  // so set to 1.0 to indicate no queuing. Otherwise calculate the slope and don't
  // allow it to be reduced by more than half to avoid aggressive load-sheding due to
  // outliers.
  const double gradient = std::max(0.5, std::min(1.0, rtt_tolerance_ * min_rtt / rtt));

  std::cerr << "Calculating new limit, min rtt " << AccessLog::AccessLogFormatUtils::durationToString(min_rtt) << " rtt " << AccessLog::AccessLogFormatUtils::durationToString(rtt) << " gradient " << gradient <<  " queue size " << queue_size << std::endl; 
  uint32_t new_limit;
  // Reduce the limit aggressively if there was a drop.
  if (sample.didDrop()) {
    std::cerr << "dropped sample? " << sample.didDrop() << std::endl;
    new_limit = estimated_limit_ / 2;
    // Don't grow the limit if we are app limited.
    std::cerr << "Update (drop): estimated limit " << estimated_limit_ << " new_limit " << new_limit << std::endl;
  } else if (sample.getMaxInFlightRequests() < estimated_limit_ / 2) {
    std::cerr << "Update (app limited): estimated limit " << estimated_limit_ << " max in flight " << sample.getMaxInFlightRequests() << std::endl;
    return;
    // Normal update to the limit.
  } else {
    new_limit = estimated_limit_ * gradient + queue_size;
    std::cerr << "Update (normal): estimated limit " << estimated_limit_ << " new limit " << new_limit << std::endl;
  }

  // TODO: rethink this.
  if (new_limit < estimated_limit_) {
    new_limit = std::max(min_limit_, static_cast<uint32_t>(estimated_limit_ * (1 - smoothing_) +
                                                           smoothing_ * new_limit));
    std::cerr << "shrinking limit, compared min_limit with " << static_cast<uint32_t>(estimated_limit_ * (1 - smoothing_) + smoothing_ * new_limit) << std::endl;
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