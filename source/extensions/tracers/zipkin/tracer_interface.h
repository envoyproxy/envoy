#pragma once

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

class Span;

/**
 * This interface must be observed by a Zipkin tracer.
 */
class TracerInterface {
public:
  /**
   * Destructor.
   */
  virtual ~TracerInterface() {}

  /**
   * A Zipkin tracer must implement this method. Its implementation must perform whatever
   * actions are required when the given span is considered finished. An implementation
   * will typically buffer the given span so that it can be flushed later.
   *
   * This method is invoked by the Span object when its finish() method is called.
   *
   * @param span The span that needs action.
   */
  virtual void reportSpan(Span&& span) PURE;
};

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
