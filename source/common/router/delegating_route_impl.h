#pragma once

#include "envoy/router/router.h"

#include "source/common/config/metadata.h"

namespace Envoy {
namespace Router {

/**
 * Wrapper class around Router::Route that delegates all method calls to the RouteConstSharedPtr
 * base route it wraps around.
 *
 * Intended to be used as a route mutability mechanism, where a filter can create a derived class of
 * DelegatingRoute and override specific methods (e.g. routeEntry) while preserving the rest of the
 * properties/behavior of the base route.
 */
class DelegatingRoute : public Router::Route {
public:
  explicit DelegatingRoute(Router::RouteConstSharedPtr route) : base_route_(std::move(route)) {
    ASSERT(base_route_ != nullptr);
  }

  // Router::Route
  const Router::DirectResponseEntry* directResponseEntry() const override;
  const Router::RouteEntry* routeEntry() const override;
  const Router::Decorator* decorator() const override;
  const Router::RouteTracing* tracingConfig() const override;

  const RouteSpecificFilterConfig*
  mostSpecificPerFilterConfig(absl::string_view name) const override {
    return base_route_->mostSpecificPerFilterConfig(name);
  }
  RouteSpecificFilterConfigs perFilterConfigs(absl::string_view filter_name) const override {
    return base_route_->perFilterConfigs(filter_name);
  }

  const envoy::config::core::v3::Metadata& metadata() const override {
    return base_route_->metadata();
  }
  const Envoy::Config::TypedMetadata& typedMetadata() const override {
    return base_route_->typedMetadata();
  }
  absl::optional<bool> filterDisabled(absl::string_view name) const override {
    return base_route_->filterDisabled(name);
  }
  const std::string& routeName() const override { return base_route_->routeName(); }
  const VirtualHost& virtualHost() const override;

private:
  const Router::RouteConstSharedPtr base_route_;
};

/**
 * Wrapper class around Router::RouteEntry that delegates all method calls to the
 * RouteConstSharedPtr base route it wraps around.
 *
 * Intended to be used with DelegatingRoute when a filter wants to override the routeEntry() method.
 * See example with SetRouteFilter in test/integration/filters.
 */
class DelegatingRouteEntry : public Router::RouteEntry {
public:
  explicit DelegatingRouteEntry(Router::RouteConstSharedPtr route) : base_route_(std::move(route)) {
    ASSERT(base_route_ != nullptr);
  }

  // Router::ResponseEntry
  void finalizeResponseHeaders(Http::ResponseHeaderMap& headers,
                               const StreamInfo::StreamInfo& stream_info) const override;
  Http::HeaderTransforms responseHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                  bool do_formatting = true) const override;

  // Router::RouteEntry
  const std::string& clusterName() const override;
  const std::string getRequestHostValue(const Http::RequestHeaderMap& headers) const override;
  Http::Code clusterNotFoundResponseCode() const override;
  const CorsPolicy* corsPolicy() const override;
  absl::optional<std::string>
  currentUrlPathAfterRewrite(const Http::RequestHeaderMap& headers) const override;
  void finalizeRequestHeaders(Http::RequestHeaderMap& headers,
                              const StreamInfo::StreamInfo& stream_info,
                              bool insert_envoy_original_path) const override;
  Http::HeaderTransforms requestHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                 bool do_formatting = true) const override;

  const Http::HashPolicy* hashPolicy() const override;
  const HedgePolicy& hedgePolicy() const override;
  Upstream::ResourcePriority priority() const override;
  const RateLimitPolicy& rateLimitPolicy() const override;
  const RetryPolicy& retryPolicy() const override;
  const Router::PathMatcherSharedPtr& pathMatcher() const override;
  const Router::PathRewriterSharedPtr& pathRewriter() const override;
  const InternalRedirectPolicy& internalRedirectPolicy() const override;
  uint32_t retryShadowBufferLimit() const override;
  const std::vector<Router::ShadowPolicyPtr>& shadowPolicies() const override;
  std::chrono::milliseconds timeout() const override;
  absl::optional<std::chrono::milliseconds> idleTimeout() const override;
  bool usingNewTimeouts() const override;
  absl::optional<std::chrono::milliseconds> maxStreamDuration() const override;
  absl::optional<std::chrono::milliseconds> grpcTimeoutHeaderMax() const override;
  absl::optional<std::chrono::milliseconds> grpcTimeoutHeaderOffset() const override;
  absl::optional<std::chrono::milliseconds> maxGrpcTimeout() const override;
  absl::optional<std::chrono::milliseconds> grpcTimeoutOffset() const override;
  bool autoHostRewrite() const override;
  bool appendXfh() const override;
  const MetadataMatchCriteria* metadataMatchCriteria() const override;
  const std::multimap<std::string, std::string>& opaqueConfig() const override;
  bool includeVirtualHostRateLimits() const override;
  const TlsContextMatchCriteria* tlsContextMatchCriteria() const override;
  const PathMatchCriterion& pathMatchCriterion() const override;
  bool includeAttemptCountInRequest() const override;
  bool includeAttemptCountInResponse() const override;
  const UpgradeMap& upgradeMap() const override;
  const ConnectConfigOptRef connectConfig() const override;
  const EarlyDataPolicy& earlyDataPolicy() const override;
  const RouteStatsContextOptRef routeStatsContext() const override;

private:
  const Router::RouteConstSharedPtr base_route_;
};

} // namespace Router
} // namespace Envoy
