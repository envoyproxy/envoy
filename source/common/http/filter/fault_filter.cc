#include <chrono>
#include <random>

#include "fault_filter.h"

#include "envoy/event/timer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/stats.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"

namespace Http {
FaultFilter::FaultFilter(FaultFilterConfigPtr config) : config_(config), generator_(rd_()) {}

FaultFilter::~FaultFilter() { ASSERT(!delay_timer_); }

FilterHeadersStatus FaultFilter::decodeHeaders(HeaderMap& headers, bool) {
  /**
   * Delays and aborts are independent events. One can inject a delay
   * followed by an abort or inject just a delay or abort. In this callback,
   * if we inject a delay, then we will inject the abort in the delay timer
   * callback.
   */

  // Check for header matches first
  if (!matches(headers)) {
    return FilterHeadersStatus::Continue;
  }

  if ((config_->delay_probability_ > 0) &&
      (prob_dist_(generator_) <= config_->delay_probability_)) {
    // Inject delays
    delay_timer_ = callbacks_->dispatcher().createTimer([this]() -> void { postDelayInjection(); });
    delay_timer_->enableTimer(std::chrono::milliseconds(config_->delay_duration_));
    config_->stats_.delays_injected_.inc();
    return FilterHeadersStatus::StopIteration;
  } else if ((config_->abort_probability_ > 0) &&
             (prob_dist_(generator_) <= config_->abort_probability_)) {
    Http::HeaderMapPtr response_headers{new HeaderMapImpl{
        {Headers::get().Status, std::to_string(enumToInt(config_->abort_code_))}}};
    callbacks_->encodeHeaders(std::move(response_headers), true);
    config_->stats_.aborts_injected_.inc();
    return FilterHeadersStatus::StopIteration;
  }

  return FilterHeadersStatus::Continue;
}

FilterDataStatus FaultFilter::decodeData(Buffer::Instance&, bool end_stream) {
  if (end_stream) {
    resetInternalState();
  }
  return FilterDataStatus::Continue;
}

FilterTrailersStatus FaultFilter::decodeTrailers(HeaderMap&) {
  resetInternalState();
  return FilterTrailersStatus::Continue;
}

FaultFilterStats FaultFilter::generateStats(const std::string& prefix, Stats::Store& store) {
  std::string final_prefix = prefix + "fault.";
  return {ALL_FAULT_FILTER_STATS(POOL_COUNTER_PREFIX(store, final_prefix))};
}

// header match semantics in fault filter works is same as the one in route block
bool FaultFilter::matches(const Http::HeaderMap& headers) const {
  bool matches = true;

  if (!config_->fault_filter_headers_.empty()) {
    for (const FaultFilterHeaders& header_data : config_->fault_filter_headers_) {
      if (header_data.value_ == EMPTY_STRING) {
        matches &= headers.has(header_data.name_);
      } else {
        matches &= (headers.get(header_data.name_) == header_data.value_);
      }
      if (!matches) {
        break;
      }
    }
  }

  return matches;
}

void FaultFilter::onResetStream() { resetInternalState(); }

void FaultFilter::postDelayInjection() {

  resetInternalState();
  // Delays can be followed by aborts
  if ((config_->abort_probability_ > 0) &&
      (prob_dist_(generator_) <= config_->abort_probability_)) {
    Http::HeaderMapPtr response_headers{new HeaderMapImpl{
        {Headers::get().Status, std::to_string(enumToInt(config_->abort_code_))}}};
    config_->stats_.aborts_injected_.inc();
    callbacks_->encodeHeaders(std::move(response_headers), true);
  } else {
    // Continue request processing
    callbacks_->continueDecoding();
  }
}

void FaultFilter::resetInternalState() {
  if (delay_timer_) {
    delay_timer_->disableTimer();
    delay_timer_.reset();
  }
}

void FaultFilter::setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
  callbacks_->addResetStreamCallback([this]() -> void { onResetStream(); });
}
} // Http
