#pragma once

#include "envoy/common/pure.h"

namespace Envoy {

/**
 * Represents some other part of the process.
 */
class ProcessObject {
public:
  virtual ~ProcessObject() {}
};

/**
 * Context passed to filters to access resources from non-Envoy parts of the
 * process.
 */
class ProcessContext {
public:
  virtual ~ProcessContext() {}

  /**
   * @return the ProcessObject for this context.
   */
  virtual ProcessObject& get() const PURE;
};

} // namespace Envoy
