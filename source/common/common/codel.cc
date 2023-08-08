/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "source/common/common/codel.h"

#include <algorithm>
#include <stdexcept>

// TODO(balrawi): Pass these as config parameters
using namespace std::chrono;

namespace Envoy {
namespace Codel {

const uint32_t codel_interval = 100;
const uint32_t codel_target_delay = 5;

Codel::Codel(TimeSource& time_source)
    : Codel(time_source,
            Codel::Options(milliseconds(codel_interval), milliseconds(codel_target_delay))) {}

Codel::Codel(TimeSource& time_source, const Options& options)
    : codel_min_delay_ns_(0),
      codel_interval_time_ns_(
          duration_cast<nanoseconds>(time_source_.monotonicTime().time_since_epoch()).count()),
      target_delay_(options.targetDelay()), interval_(options.interval()), codel_reset_delay_(true),
      time_soruce_(time_source), overloaded_(false) {}

bool Codel::overloadedExplicitNow(nanoseconds delay, steady_clock::time_point now) {
  bool ret = false;

  // Avoid another thread updating the value at the same time we are using it
  // to calculate the overloaded state
  auto minDelay = nanoseconds(codel_min_delay_ns_);
  // Get a snapshot of the parameters to determine overload condition
  auto opts = getOptions();
  auto sloughTimeout = getSloughTimeout(opts.targetDelay());

  if (now > steady_clock::time_point(nanoseconds(codel_interval_time_ns_)) &&
      // testing before exchanging is more cacheline-friendly
      (!codel_reset_delay_.load(std::memory_order_acquire) && !codel_reset_delay_.exchange(true))) {
    codel_interval_time_ns_ =
        duration_cast<nanoseconds>((now + opts.interval()).time_since_epoch()).count();

    if (minDelay > opts.targetDelay()) {
      overloaded_ = true;
    } else {
      overloaded_ = false;
    }
  }
  // Care must be taken that only a single thread resets codel_min_delay_ns_,
  // and that it happens after the interval reset above
  if (codel_reset_delay_.load(std::memory_order_acquire) && codel_reset_delay_.exchange(false)) {
    codel_min_delay_ns_ = delay.count();
    // More than one request must come in during an interval before codel
    // starts dropping requests
    return false;
  } else if (delay < nanoseconds(codel_min_delay_ns_)) {
    codel_min_delay_ns_ = delay.count();
  }

  // Here is where we apply different logic than codel proper. Instead of
  // adapting the interval until the next drop, we slough off requests with
  // queueing delay > 2*target_delay while in the overloaded regime. This
  // empirically works better for our services than the codel approach of
  // increasingly often dropping packets.
  if (overloaded_ && delay > sloughTimeout) {
    ret = true;
  }

  return ret;
}

int Codel::getLoad() {
  // it might be better to use the average delay instead of minDelay, but we'd
  // have to track it.
  auto opts = getOptions();
  return std::min<int>(100, 100 * getMinDelay() / getSloughTimeout(opts.targetDelay()));
}

absl::StatusOr<bool> Codel::setOptions(Options const& options) {
  // Carry out some basic sanity checks.
  auto delay = options.targetDelay();
  auto interval = options.interval();

  if (interval <= delay || delay <= milliseconds::zero() || interval <= milliseconds::zero()) {
    return absl::InvalidArgumentError("Invalid arguments provided");
  }
  interval_.store(interval, std::memory_order_relaxed);
  target_delay_.store(delay, std::memory_order_relaxed);
}

const Codel::Options Codel::getOptions() const {
  auto interval = interval_.load(std::memory_order_relaxed);
  auto delay = target_delay_.load(std::memory_order_relaxed);
  // Enforcing the invariant that targetDelay <= interval. A violation could
  // potentially occur if either parameter was updated by another concurrent
  // thread via the setOptions() method.
  delay = std::min(delay, interval);

  return Codel::Options(interval, delay);
}

nanoseconds Codel::getMinDelay() { return nanoseconds(codel_min_delay_ns_); }

steady_clock::time_point Codel::getIntervalTime() {
  return steady_clock::time_point(nanoseconds(codel_interval_time_ns_));
}

milliseconds Codel::getSloughTimeout(milliseconds delay) const { return delay * 2; }

} // namespace Codel
} // namespace Envoy
