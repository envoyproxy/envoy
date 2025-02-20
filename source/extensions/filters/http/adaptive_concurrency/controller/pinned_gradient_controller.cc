#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/adaptive_concurrency/controller/gradient_controller.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace Controller {

PinnedGradientControllerConfig::PinnedGradientControllerConfig(
    const envoy::extensions::filters::http::adaptive_concurrency::v3::
        PinnedGradientControllerConfig& proto_config,
    Runtime::Loader& runtime)
    : GradientControllerConfig::GradientControllerConfig(
          std::chrono::milliseconds(DurationUtil::durationToMilliseconds(
              proto_config.concurrency_limit_params().concurrency_update_interval())),
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config.concurrency_limit_params(),
                                          max_concurrency_limit, 1000),
          PROTOBUF_PERCENT_TO_DOUBLE_OR_DEFAULT(proto_config, sample_aggregate_percentile, 50),
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, min_concurrency, 3),
          0.0, // let user provide padding by tuning pinned minRTT
          runtime),
      min_rtt_(
          std::chrono::milliseconds(DurationUtil::durationToMilliseconds(proto_config.min_rtt()))) {
}

PinnedGradientController::PinnedGradientController(PinnedGradientControllerConfig config,
                                                   Event::Dispatcher& dispatcher, Runtime::Loader&,
                                                   const std::string& stats_prefix,
                                                   Stats::Scope& scope,
                                                   Random::RandomGenerator& random,
                                                   TimeSource& time_source)
    : config_(std::move(config)), dispatcher_(dispatcher), scope_(scope),
      stats_(GradientControllerStats::generateStats(scope_, stats_prefix)), random_(random),
      time_source_(time_source), num_rq_outstanding_(0),
      concurrency_limit_(config_.minConcurrency()),
      latency_sample_hist_(hist_fast_alloc(), hist_free) {
  sample_reset_timer_ = dispatcher_.createTimer([this]() -> void {
    {
      absl::MutexLock ml(&sample_mutation_mtx_);
      resetSampleWindow();
    }

    sample_reset_timer_->enableTimer(config_.sampleRTTCalcInterval());
  });

  sample_reset_timer_->enableTimer(config_.sampleRTTCalcInterval());
  stats_.concurrency_limit_.set(concurrency_limit_.load());
  stats_.min_rtt_msecs_.set(
      std::chrono::duration_cast<std::chrono::milliseconds>(config_.minRTT()).count());
}

void PinnedGradientController::resetSampleWindow() {
  if (hist_sample_count(latency_sample_hist_.get()) == 0) {
    return;
  }

  sample_rtt_ = processLatencySamplesAndClear();
  stats_.sample_rtt_msecs_.set(
      std::chrono::duration_cast<std::chrono::milliseconds>(sample_rtt_).count());
  updateConcurrencyLimit(calculateNewLimit());
}

std::chrono::microseconds PinnedGradientController::processLatencySamplesAndClear() {
  const std::array<double, 1> quantile{config_.sampleAggregatePercentile()};
  std::array<double, 1> calculated_quantile;
  hist_approx_quantile(latency_sample_hist_.get(), quantile.data(), 1, calculated_quantile.data());
  hist_clear(latency_sample_hist_.get());
  return std::chrono::microseconds(static_cast<int>(calculated_quantile[0]));
}

uint32_t PinnedGradientController::calculateNewLimit() {
  return calculateNewConcurrencyLimit(config_.minRTT(), sample_rtt_, concurrencyLimit(), config_,
                                      stats_);
};

RequestForwardingAction PinnedGradientController::forwardingDecision() {
  // Note that a race condition exists here which would allow more outstanding requests than the
  // concurrency limit bounded by the number of worker threads. After loading num_rq_outstanding_
  // and before loading concurrency_limit_, another thread could potentially swoop in and modify
  // num_rq_outstanding_, causing us to move forward with stale values and increment
  // num_rq_outstanding_.
  //
  // TODO (tonya11en): Reconsider using a CAS loop here.
  if (num_rq_outstanding_.load() < concurrencyLimit()) {
    ++num_rq_outstanding_;
    return RequestForwardingAction::Forward;
  }
  stats_.rq_blocked_.inc();
  return RequestForwardingAction::Block;
}

void PinnedGradientController::recordLatencySample(MonotonicTime rq_send_time) {
  ASSERT(num_rq_outstanding_.load() > 0);
  --num_rq_outstanding_;

  const std::chrono::microseconds rq_latency =
      std::chrono::duration_cast<std::chrono::microseconds>(time_source_.monotonicTime() -
                                                            rq_send_time);
  synchronizer_.syncPoint("pre_hist_insert");
  {
    absl::MutexLock ml(&sample_mutation_mtx_);
    hist_insert(latency_sample_hist_.get(), rq_latency.count(), 1);
  }
}

void PinnedGradientController::cancelLatencySample() {
  ASSERT(num_rq_outstanding_.load() > 0);
  --num_rq_outstanding_;
}

void PinnedGradientController::updateConcurrencyLimit(const uint32_t new_limit) {
  concurrency_limit_.store(new_limit);
  stats_.concurrency_limit_.set(concurrency_limit_.load());
}

} // namespace Controller
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
