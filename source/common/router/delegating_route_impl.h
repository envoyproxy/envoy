#pragma once

#include "envoy/router/router.h"

namespace Envoy {
namespace Router {

/**
 * Implementation of DelegatingRoute that delegates all method calls to the RouteConstSharedPtr
 * base route it wraps around.
 *
 * Intended to be used as a route mutability mechanism, where a filter can create a derived class of
 * DelegatingRoute and override specific methods (e.g. routeEntry) while preserving the rest of the
 * properties/behavior of the base route.
 */
class DelegatingRoute : public Router::Route {
public:
  DelegatingRoute(Router::RouteConstSharedPtr route) : base_route_(route) {}

  const Router::DirectResponseEntry* directResponseEntry() const override;
  const Router::RouteEntry* routeEntry() const override;
  const Router::Decorator* decorator() const override;
  const Router::RouteTracing* tracingConfig() const override;
  const Router::RouteSpecificFilterConfig* perFilterConfig(const std::string&) const override;

private:
  Router::RouteConstSharedPtr base_route_;
};

class DelegatingRouteEntry : public Router::RouteEntry {
public:
  DelegatingRouteEntry(Router::RouteEntry* route_entry) : base_route_entry_(route_entry) {}

  const std::string& clusterName() const override;
  Http::Code clusterNotFoundResponseCode() const override;
  const CorsPolicy* corsPolicy() const override;
  void finalizeRequestHeaders(Http::RequestHeaderMap& headers,
                              const StreamInfo::StreamInfo& stream_info,
                              bool insert_envoy_original_path) const override;
  const Http::HashPolicy* hashPolicy() const override;
  const HedgePolicy& hedgePolicy() const override;
  Upstream::ResourcePriority priority() const override;
  const RateLimitPolicy& rateLimitPolicy() const override;
  const RetryPolicy& retryPolicy() const override;
  const InternalRedirectPolicy& internalRedirectPolicy() const override;
  uint32_t retryShadowBufferLimit() const override;
  const std::vector<ShadowPolicyPtr>& shadowPolicies() const override;
  std::chrono::milliseconds timeout() const override;
  absl::optional<std::chrono::milliseconds> idleTimeout() const override;
  absl::optional<std::chrono::milliseconds> maxStreamDuration() const override;
  absl::optional<std::chrono::milliseconds> grpcTimeoutHeaderMax() const override;
  absl::optional<std::chrono::milliseconds> grpcTimeoutHeaderOffset() const override;
  absl::optional<std::chrono::milliseconds> maxGrpcTimeout() const override;
  absl::optional<std::chrono::milliseconds> grpcTimeoutOffset() const override;
  const VirtualCluster* virtualCluster(const Http::HeaderMap& headers) const override;
  const VirtualHost& virtualHost() const override;
  bool autoHostRewrite() const override;
  const MetadataMatchCriteria* metadataMatchCriteria() const override;
  const std::multimap<std::string, std::string>& opaqueConfig() const override;
  bool includeVirtualHostRateLimits() const override;
  const Envoy::Config::TypedMetadata& typedMetadata() const override;
  const envoy::config::core::v3::Metadata& metadata() const override;
  const TlsContextMatchCriteria* tlsContextMatchCriteria() const override;
  const PathMatchCriterion& pathMatchCriterion() const override;
  const RouteSpecificFilterConfig* perFilterConfig(const std::string& name) const override;
  bool includeAttemptCountInRequest() const override;
  bool includeAttemptCountInResponse() const override;
  const UpgradeMap& upgradeMap() const override;
  const absl::optional<ConnectConfig>& connectConfig() const override;
  const std::string& routeName() const override;

private:
  Router::RouteEntry* base_route_entry_;
};

} // namespace Router
} // namespace Envoy
