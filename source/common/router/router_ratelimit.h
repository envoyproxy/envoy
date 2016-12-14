#pragma once

#include "envoy/router/router.h"
#include "envoy/router/router_ratelimit.h"

#include "common/http/filter/ratelimit.h"

namespace Router {

/**
* Action for service to service rate limiting.
*/
class ServiceToServiceAction : public Action {
public:
  // Router::Action
  void populateDescriptors(const Router::RouteEntry& route,
                           std::vector<::RateLimit::Descriptor>& descriptors,
                           const std::string& local_service_cluster, const Http::HeaderMap&,
                           Http::StreamDecoderFilterCallbacks&) const override;
};

/**
* Action for request headers rate limiting.
*/
class RequestHeadersAction : public Action {
public:
  RequestHeadersAction(const Json::Object& action)
      : header_name_(action.getString("header_name")),
        descriptor_key_(action.getString("descriptor_key")) {}

  // Router::Action
  void populateDescriptors(const Router::RouteEntry& route,
                           std::vector<::RateLimit::Descriptor>& descriptors, const std::string&,
                           const Http::HeaderMap& headers,
                           Http::StreamDecoderFilterCallbacks&) const override;

private:
  const Http::LowerCaseString header_name_;
  const std::string descriptor_key_;
};

/**
 * Action for remote address rate limiting.
 */
class RemoteAddressAction : public Action {
public:
  // Router::Action
  void populateDescriptors(const Router::RouteEntry& route,
                           std::vector<::RateLimit::Descriptor>& descriptors, const std::string&,
                           const Http::HeaderMap&,
                           Http::StreamDecoderFilterCallbacks& callbacks) const override;
};

class RateLimitPolicyEntryImpl : public RateLimitPolicyEntry {
public:
  RateLimitPolicyEntryImpl(const Json::Object& config);

  // Router::RateLimitPolicyEntry
  int64_t stage() const override { return stage_; }

  // Router::RateLimitPolicyEntry
  const std::string& killSwitchKey() const override { return kill_switch_key_; }

  // Router::RateLimitPolicyEntry
  void populateDescriptors(const Router::RouteEntry& route,
                           std::vector<::RateLimit::Descriptor>& descriptors,
                           const std::string& local_service_cluster, const Http::HeaderMap&,
                           Http::StreamDecoderFilterCallbacks& callbacks) const override;

private:
  const std::string kill_switch_key_;
  int64_t stage_{};
  std::vector<ActionPtr> actions_;
};

/**
 * Implementation of RateLimitPolicy that reads from the JSON route config.
 */
class RateLimitPolicyImpl : public RateLimitPolicy {
public:
  RateLimitPolicyImpl(const Json::Object& config);

  // Router::RateLimitPolicy
  const std::string& routeKey() const override { return route_key_; }

  // Router::RateLimitPolicy
  const std::vector<std::reference_wrapper<RateLimitPolicyEntry>>&
  getApplicableRateLimit(int64_t stage = 0) const override;

private:
  const std::string route_key_;
  std::vector<std::vector<std::reference_wrapper<RateLimitPolicyEntry>>> rate_limit_entries_;
  static const std::vector<std::reference_wrapper<RateLimitPolicyEntry>> empty_rate_limit_;
};

} // Router