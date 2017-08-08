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
  virtual ~DateProvider() {}

  /**
   * Set the Date header potentially using a cached value.
   * @param headers supplies the headers to fill.
   */
  virtual void setDateHeader(HeaderMap& headers) PURE;
};

} // namespace Http
} // namespace Envoy
