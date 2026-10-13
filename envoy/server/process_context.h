#pragma once

#include "envoy/common/optref.h"
#include "envoy/common/pure.h"

namespace Envoy {

/**
 * Represents some other part of the process.
 */
class ProcessObject {
public:
  virtual ~ProcessObject() = default;
};

using ProcessObjectOptRef = OptRef<ProcessObject>;

/**
 * Context passed to filters to access resources from non-Envoy parts of the
 * process.
 */
class ProcessContext {
public:
  virtual ~ProcessContext() = default;

  /**
   * @return the ProcessObject for this context.
   */
  virtual ProcessObject& get() const PURE;
};

using ProcessContextOptRef = OptRef<ProcessContext>;

} // namespace Envoy
