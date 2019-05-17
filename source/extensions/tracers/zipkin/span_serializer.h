#pragma once

#include "extensions/tracers/zipkin/zipkin_core_types.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {
class SpanSerializer {
public:
  /**
   * Destructor.
   */
  virtual ~SpanSerializer() {}

  /**
   * Method that a concrete SpanSerializer class must implement to serialize spans.
   *
   * @param span The span that needs action.
   */
  virtual std::string serialize(std::vector<Span> spans);

  virtual std::string contentType();
};
} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
