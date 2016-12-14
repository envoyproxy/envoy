#pragma once

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/ratelimit/ratelimit.h"

namespace Router {
/**
* Base interface for reneric rate limit action.
*/
class Action {
public:
  virtual ~Action() {}

  /**
   * Potentially populate the descriptor array with new descriptors to query.
   * @param route supplies the target route for the request.
   * @param descriptors supplies the descriptor array to optionally fill.
   * @param config supplies the filter configuration.
   */
  virtual void populateDescriptors(const RouteEntry& route,
                                   std::vector<::RateLimit::Descriptor>& descriptors,
                                   const std::string& local_service_cluster,
                                   const Http::HeaderMap& headers,
                                   Http::StreamDecoderFilterCallbacks& callbacks) const PURE;
};

typedef std::unique_ptr<Action> ActionPtr;

/**
* Rate limit configuration
*/
class RateLimitPolicyEntry {
public:
  virtual ~RateLimitPolicyEntry() {}

  /**
   * @return the stage value that the configuration is applicable to.
   */
  virtual int64_t stage() const PURE;

  /**
   * @return runtime key to be set to disable the configuration.
   */
  virtual const std::string& killSwitchKey() const PURE;

  /**
   * Potentially populate the descriptor array with new descriptors to query.
   * @param route supplies the target route for the request.
   * @param descriptors supplies the descriptor array to optionally fill.
   * @param config supplies the filter configuration.
   */
  virtual void populateDescriptors(const RouteEntry& route,
                                   std::vector<::RateLimit::Descriptor>& descriptors,
                                   const std::string& local_service_cluster,
                                   const Http::HeaderMap& headers,
                                   Http::StreamDecoderFilterCallbacks& callbacks) const PURE;
};

/**
* Rate limiting policy
*/
class RateLimitPolicy {
public:
  virtual ~RateLimitPolicy() {}

  /**
   * @return the route key, if it exists.
   */
  virtual const std::string& routeKey() const PURE;

  /**
   * @param the stage value to use for comparison finding the set of applicable rate limits.
   * @return set of RateLimitPolicyEntry that are applicable for a stage.
   */
  virtual const std::vector<std::reference_wrapper<RateLimitPolicyEntry>>&
  getApplicableRateLimit(int64_t stage) const PURE;
};

} // Router
