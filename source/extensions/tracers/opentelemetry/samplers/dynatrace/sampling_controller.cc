#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/sampling_controller.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

namespace {}

void SamplingController::update() {
  absl::WriterMutexLock lock{&stream_summary_mutex_};
  const auto top_k = stream_summary_->getTopK();
  const auto last_period_count = stream_summary_->getN();

  // update sampling exponents
  update(top_k, last_period_count,
         sampler_config_provider_->getSamplerConfig().getRootSpansPerMinute());
  // Note: getTopK() returns references to values in StreamSummary.
  // Do not destroy it while top_k is used!
  stream_summary_ = std::make_unique<StreamSummaryT>(STREAM_SUMMARY_SIZE);
}

void SamplingController::update(const TopKListT& top_k, uint64_t last_period_count,
                                uint32_t total_wanted) {

  SamplingExponentsT new_sampling_exponents;
  // start with sampling exponent 0, which means multiplicity == 1 (every span is sampled)
  for (auto const& counter : top_k) {
    new_sampling_exponents[counter.getItem()] = SamplingState(0);
  }

  // use the last entry as "rest bucket", which is used for new/unknown requests
  rest_bucket_key_ = (!top_k.empty()) ? top_k.back().getItem() : "";

  calculateSamplingExponents(top_k, total_wanted, new_sampling_exponents);
  last_effective_count_ = calculateEffectiveCount(top_k, new_sampling_exponents);
  logSamplingInfo(top_k, new_sampling_exponents, last_period_count, total_wanted);

  absl::WriterMutexLock lock{&sampling_exponents_mutex_};
  sampling_exponents_ = std::move(new_sampling_exponents);
}

uint64_t SamplingController::getEffectiveCount() const {
  absl::ReaderMutexLock lock{&stream_summary_mutex_};
  return last_effective_count_;
}

void SamplingController::offer(const std::string& sampling_key) {
  if (!sampling_key.empty()) {
    absl::WriterMutexLock lock{&stream_summary_mutex_};
    stream_summary_->offer(sampling_key);
  }
}

SamplingState SamplingController::getSamplingState(const std::string& sampling_key) const {
  { // scope for lock
    absl::ReaderMutexLock sax_lock{&sampling_exponents_mutex_};
    auto iter = sampling_exponents_.find(sampling_key);
    if (iter != sampling_exponents_.end()) {
      return iter->second;
    }

    // try to use "rest bucket"
    auto rest_bucket_iter = sampling_exponents_.find(rest_bucket_key_);
    if (rest_bucket_iter != sampling_exponents_.end()) {
      return rest_bucket_iter->second;
    }
  }

  // If we can't find a sampling exponent, we calculate it based on the total number of requests
  // in this period. This should also handle the "warm up phase" where no top_k is available
  const auto divisor = sampler_config_provider_->getSamplerConfig().getRootSpansPerMinute() / 2;
  if (divisor == 0) {
    return SamplingState{MAX_SAMPLING_EXPONENT};
  }
  absl::ReaderMutexLock ss_lock{&stream_summary_mutex_};
  const uint32_t exp = stream_summary_->getN() / divisor;
  return SamplingState{exp};
}

std::string SamplingController::getSamplingKey(const absl::string_view path_query,
                                               const absl::string_view method) {
  // remove query part (truncate after first '?')
  const size_t query_offset = path_query.find('?');
  auto path =
      path_query.substr(0, query_offset != path_query.npos ? query_offset : path_query.size());
  return absl::StrCat(method, "_", path);
}

void SamplingController::logSamplingInfo(const TopKListT& top_k,
                                         const SamplingExponentsT& new_sampling_exponents,
                                         uint64_t last_period_count, uint32_t total_wanted) const {
  ENVOY_LOG(debug,
            "Updating sampling info. top_k.size(): {}, last_period_count: {}, total_wanted: {}",
            top_k.size(), last_period_count, total_wanted);
  for (auto const& counter : top_k) {
    auto sampling_state = new_sampling_exponents.find(counter.getItem());
    ENVOY_LOG(debug, "- {}: value: {}, exponent: {}", counter.getItem(), counter.getValue(),
              sampling_state->second.getExponent());
  }
}

uint64_t SamplingController::calculateEffectiveCount(const TopKListT& top_k,
                                                     const SamplingExponentsT& sampling_exponents) {
  uint64_t cnt = 0;
  for (auto const& counter : top_k) {
    auto sampling_state = sampling_exponents.find(counter.getItem());
    if (sampling_state != sampling_exponents.end()) {
      auto counterVal = counter.getValue();
      auto mul = sampling_state->second.getMultiplicity();
      auto res = counterVal / mul;
      cnt += res;
    }
  }
  return cnt;
}

void SamplingController::calculateSamplingExponents(
    const TopKListT& top_k, uint32_t total_wanted,
    SamplingExponentsT& new_sampling_exponents) const {
  const auto top_k_size = top_k.size();
  if (top_k_size == 0 || total_wanted == 0) {
    return;
  }

  // number of requests which are allowed for every entry
  const uint32_t allowed_per_entry = total_wanted / top_k_size;

  for (auto& counter : top_k) {
    // allowed multiplicity for this entry
    auto wanted_multiplicity = counter.getValue() / allowed_per_entry;
    auto sampling_state = new_sampling_exponents.find(counter.getItem());
    // sampling exponent has to be a power of 2. Find the exponent to have multiplicity near to
    // wanted_multiplicity
    while (wanted_multiplicity > sampling_state->second.getMultiplicity() &&
           sampling_state->second.getExponent() < MAX_SAMPLING_EXPONENT) {
      sampling_state->second.increaseExponent();
    }
    if (wanted_multiplicity < sampling_state->second.getMultiplicity()) {
      // we want to have multiplicity <= wanted_multiplicity, therefore exponent is decreased once.
      sampling_state->second.decreaseExponent();
    }
  }

  auto effective_count = calculateEffectiveCount(top_k, new_sampling_exponents);
  // There might be entries where allowed_per_entry is greater than their count.
  // Therefore, we would sample less than total_wanted.
  // To avoid this, we decrease the exponent of other entries if possible
  if (effective_count < total_wanted) {
    for (int i = 0; i < 5; i++) { // max tries
      for (auto reverse_it = top_k.rbegin(); reverse_it != top_k.rend();
           ++reverse_it) { // start with lowest frequency
        auto rev_sampling_state = new_sampling_exponents.find(reverse_it->getItem());
        rev_sampling_state->second.decreaseExponent();
        effective_count = calculateEffectiveCount(top_k, new_sampling_exponents);
        if (effective_count >= total_wanted) { // we are done
          return;
        }
      }
    }
  }
}
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
