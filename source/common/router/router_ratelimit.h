#pragma once

#include "envoy/router/router.h"
#include "envoy/router/router_ratelimit.h"

#include "common/http/filter/ratelimit.h"

namespace Router {

/**
 * Action for source cluster rate limiting.
 */
class SourceClusterAction : public RateLimitAction {
public:
  // Router::RateLimitAction
  void populateDescriptor(const Router::RouteEntry& route, ::RateLimit::Descriptor& descriptor,
                          const std::string& local_service_cluster, const Http::HeaderMap& headers,
                          const std::string& remote_address) const override;
};

/**
 * Action for destination cluster rate limiting.
 */
class DestinationClusterAction : public RateLimitAction {
public:
  // Router::RateLimitAction
  void populateDescriptor(const Router::RouteEntry& route, ::RateLimit::Descriptor& descriptor,
                          const std::string& local_service_cluster, const Http::HeaderMap& headers,
                          const std::string& remote_address) const override;
};

/**
* Action for request headers rate limiting.
*/
class RequestHeadersAction : public RateLimitAction {
public:
  RequestHeadersAction(const Json::Object& action)
      : header_name_(action.getString("header_name")),
        descriptor_key_(action.getString("descriptor_key")) {}

  // Router::RateLimitAction
  void populateDescriptor(const Router::RouteEntry& route, ::RateLimit::Descriptor& descriptor,
                          const std::string& local_service_cluster, const Http::HeaderMap& headers,
                          const std::string& remote_address) const override;

private:
  const Http::LowerCaseString header_name_;
  const std::string descriptor_key_;
};

/**
 * Action for remote address rate limiting.
 */
class RemoteAddressAction : public RateLimitAction {
public:
  // Router::RateLimitAction
  void populateDescriptor(const Router::RouteEntry& route, ::RateLimit::Descriptor& descriptor,
                          const std::string& local_service_cluster, const Http::HeaderMap& headers,
                          const std::string& remote_address) const override;
};

/**
 * Action for route key rate limiting.
 */
class RateLimitKeyAction : public RateLimitAction {
public:
  RateLimitKeyAction(const Json::Object& action)
      : rate_limit_value_(action.getString("rate_limit_value")) {}

  // Router::RateLimitAction
  void populateDescriptor(const Router::RouteEntry& route, ::RateLimit::Descriptor& descriptor,
                          const std::string& local_service_cluster, const Http::HeaderMap& headers,
                          const std::string& remote_address) const override;

private:
  const std::string rate_limit_value_;
};

/*
 * Implementation of RateLimitPolicyEntry that holds the action for the configuration.
 */
class RateLimitPolicyEntryImpl : public RateLimitPolicyEntry {
public:
  RateLimitPolicyEntryImpl(const Json::Object& config);

  // Router::RateLimitPolicyEntry
  int64_t stage() const override { return stage_; }
  const std::string& disableKey() const override { return disable_key_; }

  // Router::RateLimitAction
  void populateDescriptors(const Router::RouteEntry& route,
                           std::vector<::RateLimit::Descriptor>& descriptors,
                           const std::string& local_service_cluster, const Http::HeaderMap&,
                           const std::string& remote_address) const override;

private:
  const std::string disable_key_;
  int64_t stage_;
  std::vector<RateLimitActionPtr> actions_;
};

/**
 * Implementation of RateLimitPolicy that reads from the JSON route config.
 */
class RateLimitPolicyImpl : public RateLimitPolicy {
public:
  RateLimitPolicyImpl(const Json::Object& config);

  // Router::RateLimitPolicy
  const std::vector<std::reference_wrapper<const RateLimitPolicyEntry>>&
  getApplicableRateLimit(int64_t stage = 0) const override;

private:
  std::vector<std::vector<std::unique_ptr<RateLimitPolicyEntry>>> rate_limit_entries_;
  std::vector<std::vector<std::reference_wrapper<const RateLimitPolicyEntry>>>
      rate_limit_entries_reference_;
  static const std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> empty_rate_limit_;
};

} // Router