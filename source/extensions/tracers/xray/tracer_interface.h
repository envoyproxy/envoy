#pragma once

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

class Span;

/**
 * This interface must be observed by a XRay tracer.
 */
class TracerInterface {
public:
  /**
   * Destructor.
   */
  virtual ~TracerInterface() {}

  /**
   * A XRay tracer must implement this method. Its implementation must perform whatever
   * actions are required when the given span is considered finished.
   *
   * This method is invoked by the Span object when its finish() method is called.
   *
   * @param span The span that needs action.
   */
  virtual void reportSpan(Span&& span) PURE;
};

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
