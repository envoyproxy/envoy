#pragma once

#include "envoy/tracing/trace_config.h"
#include "envoy/tracing/tracer.h"

#include "source/extensions/filters/network/generic_proxy/access_log.h"
#include "source/extensions/filters/network/generic_proxy/interface/codec.h"
#include "source/extensions/filters/network/generic_proxy/interface/filter.h"
#include "source/extensions/filters/network/generic_proxy/match_input.h"
#include "source/extensions/filters/network/generic_proxy/route.h"
#include "source/extensions/filters/network/generic_proxy/stats.h"

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
  virtual RouteEntryConstSharedPtr routeEntry(const MatchInput& request) const PURE;

  /**
   * Get codec factory for  decoding/encoding of request/response.
   * @return CodecFactory codec factory.
   */
  virtual const CodecFactory& codecFactory() const PURE;

  /**
   * @return const Network::DrainDecision& a drain decision that filters can use to
   * determine if they should be doing graceful closes on connections when possible.
   */
  virtual const Network::DrainDecision& drainDecision() const PURE;

  /**
   *  @return Tracing::Tracer tracing provider to use.
   */
  virtual OptRef<Tracing::Tracer> tracingProvider() const PURE;

  /**
   * @return connection manager tracing config.
   */
  virtual OptRef<const Tracing::ConnectionManagerTracingConfig> tracingConfig() const PURE;

  /**
   * @return stats to use.
   */
  virtual GenericFilterStats& stats() PURE;

  /**
   * @return code or flags stats name to use.
   */
  virtual const CodeOrFlags& codeOrFlags() const PURE;

  /**
   * @return const std::vector<AccessLogInstanceSharedPtr>& access logs.
   */
  virtual const std::vector<AccessLogInstanceSharedPtr>& accessLogs() const PURE;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
