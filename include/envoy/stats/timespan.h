#pragma once

#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Stats {

/**
 * An abstraction of timespan which can be completed.
 */
class CompletableTimespan {
public:
  virtual ~CompletableTimespan() = default;

  /**
   * Time elapsed since the creation of the timespan, in milliseconds.
   */
  virtual uint64_t elapsedMs() PURE;

  /**
   * Complete the timespan.
   */
  virtual void complete() PURE;
};

using Timespan = CompletableTimespan;
using TimespanPtr = std::unique_ptr<Timespan>;

} // namespace Stats
} // namespace Envoy
