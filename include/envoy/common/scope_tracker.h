#pragma once

#include <ostream>

#include "envoy/common/pure.h"

namespace Envoy {

/*
 * A class for tracking the scope of work.
 * Currently this is only used for best-effort tracking the L7 stream doing
 * work if a fatal error occurs.
 */
class ScopeTrackedObject {
public:
  virtual ~ScopeTrackedObject() = default;

  /**
   * Dump debug state of the object in question to the provided ostream
   *
   * This is called on Envoy fatal errors, so should do minimal memory allocation.
   *
   * @param os the ostream to output to.
   * @param indent_level how far to indent, for pretty-printed classes and subclasses.
   */
  virtual void dumpState(std::ostream& os, int indent_level = 0) const PURE;
};

} // namespace Envoy
