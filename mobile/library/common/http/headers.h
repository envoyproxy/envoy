#pragma once

#include "envoy/http/header_map.h"

namespace Envoy {
namespace Http {

/**
 * Constant HTTP headers used internally for in-band signalling in the request/response path.
 */
class InternalHeaderValues {
public:
  const LowerCaseString ErrorCode{"x-internal-error-code"};
  const LowerCaseString ErrorMessage{"x-internal-error-message"};
};

using InternalHeaders = ConstSingleton<InternalHeaderValues>;

} // namespace Http
} // namespace Envoy
