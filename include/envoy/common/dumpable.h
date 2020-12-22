#pragma once

#include <ostream>

#include "envoy/common/pure.h"

namespace Envoy {

/*
 * Interface for classes that can dump their state.
 * This is similar to ScopeTrackedObject interface, but cannot be registered
 * for tracking work.
 */
class Dumpable {
public:
  virtual ~Dumpable() = default;
  /**
   * Dump debug state of the object in question to the provided ostream.
   *
   * This is called on Envoy fatal errors, so should do minimal memory allocation.
   *
   * @param os the ostream to output to.
   * @param indent_level how far to indent, for pretty-printed classes and subclasses.
   */
  virtual void dumpState(std::ostream& os, int indent_level = 0) const PURE;
};

} // namespace Envoy
