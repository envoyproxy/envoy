#pragma once

#include <memory>
#include <string>
#include <vector>

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
  virtual ~TracerInterface() = default;

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

/**
 * Buffered pending spans serializer.
 */
class Serializer {
public:
  virtual ~Serializer() = default;

  /**
   * Serialize buffered pending spans.
   *
   * @return std::string serialized buffered pending spans.
   */
  virtual std::string serialize(const std::vector<Span>& spans) PURE;
};

using SerializerPtr = std::unique_ptr<Serializer>;

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
