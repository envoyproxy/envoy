#pragma once

#include <string>
#include <vector>

#include "envoy/common/time.h"

#include "common/common/lock_guard.h"
#include "common/common/thread.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

/**
 * Reservoir Keeps track of the number of sampled segments within a single second.
 * This class is implemented to be thread-safe to achieve accurate sampling.
 */
class Reservoir {
public:
  /**
   * Copy constructor.
   */
  Reservoir(const Reservoir&);

  /**
   * Assignment operator.
   */
  Reservoir& operator=(const Reservoir& reservoir);

  /**
   * Default constructor.
   */
  Reservoir(TimeSource& time_source)
      : traces_per_second_(0), this_second_(0), used_this_second_(0), time_source_(time_source){};

  /**
   * Constructor. Reservoir creates a new reservoir with a specified trace per second
   * sampling capacity. The maximum supported sampling capacity per second is currently 100,000,000.
   * @param trace_per_second
   */
  Reservoir(const int trace_per_second, TimeSource& time_source);

  /**
   * take returns true when the reservoir has remaining sampling capacity for the current epoch.
   * take returns false when the reservoir has no remaining sampling capacity for the current epoch.
   */
  bool take();

  /**
   * set the value traces_per_second_
   * @param traces_per_second
   */
  void setTracesPerSecond(const int traces_per_second) { traces_per_second_ = traces_per_second; }

  /**
   * set the value of this_second_
   * @param this_second
   */
  void setThisSecond(const int this_second) { this_second_ = this_second; }

  /**
   * set the value of used_this_second_
   * @param used_this_second
   */
  void setUsedThisSecond(const int used_this_second) { used_this_second_ = used_this_second; }

  /**
   * @return traces_per_second_
   */
  int tracesPerSecond() const { return traces_per_second_; }

  /**
   * @return this_second_
   */
  int thisSecond() const { return this_second_; }

  /**
   * @return used_this_second_
   */
  int usedThisSecond() const { return used_this_second_; }

private:
  // trace_per_second_ is the number of guaranteed sampled segments.
  int traces_per_second_;
  int this_second_;
  int used_this_second_;
  Thread::MutexBasicLockable m_lock;
  TimeSource& time_source_;
};

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
