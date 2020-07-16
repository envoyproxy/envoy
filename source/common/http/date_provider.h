#pragma once

#include "envoy/common/pure.h"
#include "envoy/http/header_map.h"

namespace Envoy {
namespace Http {

/**
 * Fills headers with a date header.
 */
class DateProvider {
public:
  virtual ~DateProvider() = default;

  /**
   * Set the `date` header (potentially using a cached value).
   * @param headers the headers to populate.
   */
  virtual void setDateHeader(RequestOrResponseHeaderMap& headers) PURE;
};

} // namespace Http
} // namespace Envoy
