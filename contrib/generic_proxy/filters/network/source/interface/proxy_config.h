#pragma once

#include "contrib/generic_proxy/filters/network/source/interface/codec.h"
#include "contrib/generic_proxy/filters/network/source/interface/filter.h"
#include "contrib/generic_proxy/filters/network/source/interface/route.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

/**
 * Filter config interface for generic proxy read filter.
 */
class FilterConfig : public FilterChainFactory {
public:
  /**
   * Get route entry by generic request.
   * @param request request.
   * @return RouteEntryConstSharedPtr route entry.
   */
  virtual RouteEntryConstSharedPtr routeEntry(const Request& request) const PURE;

  /**
   * Get codec factory for  decoding/encoding of request/response.
   * @return CodecFactory codec factory.
   */
  virtual const CodecFactory& codecFactory() const PURE;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
