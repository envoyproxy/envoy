#pragma once

#include <cstdint>
#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Stats {

class Histogram;
class Source;

/**
 * A sink for stats. Each sink is responsible for writing stats to a backing store.
 */
class Sink {
public:
  virtual ~Sink() {}

  /**
   * Periodic metric flush to the sink.
   * @param source interface through which the sink can access all metrics being flushed.
   */
  virtual void flush(Source& source) PURE;

  /**
   * Flush a single histogram sample. Note: this call is called synchronously as a part of recording
   * the metric, so implementations must be thread-safe.
   * @param histogram the histogram that this sample applies to.
   * @param value the value of the sample.
   */
  virtual void onHistogramComplete(const Histogram& histogram, uint64_t value) PURE;
};

typedef std::unique_ptr<Sink> SinkPtr;

} // namespace Stats
} // namespace Envoy
