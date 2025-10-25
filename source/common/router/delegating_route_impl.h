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
template <class Interface> class DelegatingRouteBase : public Interface {
public:
  explicit DelegatingRouteBase(Router::RouteConstSharedPtr route) : base_route_(std::move(route)) {
    ASSERT(base_route_ != nullptr);
  }

  // Router::Route
  const Router::DirectResponseEntry* directResponseEntry() const override {
    return base_route_->directResponseEntry();
  }
  const Router::RouteEntry* routeEntry() const override { return base_route_->routeEntry(); }
  const Router::Decorator* decorator() const override { return base_route_->decorator(); }
  const Router::RouteTracing* tracingConfig() const override {
    return base_route_->tracingConfig();
  }
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
  const VirtualHostConstSharedPtr& virtualHost() const override {
    return base_route_->virtualHost();
  }

protected:
  const Router::RouteConstSharedPtr base_route_;
};

using DelegatingRoute = DelegatingRouteBase<Route>;

/**
 * Wrapper class around Router::RouteEntryAndRoute that delegates all method calls to the
 * RouteConstSharedPtr base route it wraps around.
 *
 * Intended to be used when a filter wants to override the routeEntry() method.
 * See example with SetRouteFilter in test/integration/filters.
 */
class DelegatingRouteEntry : public DelegatingRouteBase<RouteEntryAndRoute> {
public:
  explicit DelegatingRouteEntry(RouteConstSharedPtr route)
      : DelegatingRouteBase(std::move(route)), base_route_entry_(base_route_->routeEntry()) {
    ASSERT(base_route_entry_ != nullptr);
    ASSERT(base_route_->directResponseEntry() == nullptr);
  }

  // Override the routeEntry to return this. By this way, the derived class of this class can
  // override the methods of Router::RouteEntry directly.

  // Router::Route
  const Router::RouteEntry* routeEntry() const override { return this; }

  // Router::ResponseEntry
  void finalizeResponseHeaders(Http::ResponseHeaderMap& headers,
                               const Formatter::HttpFormatterContext& context,
                               const StreamInfo::StreamInfo& stream_info) const override;
  Http::HeaderTransforms responseHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                  bool do_formatting = true) const override;

  // Router::RouteEntry
  const std::string& clusterName() const override;
  Http::Code clusterNotFoundResponseCode() const override;
  const CorsPolicy* corsPolicy() const override;
  std::string currentUrlPathAfterRewrite(const Http::RequestHeaderMap& headers,
                                         const Formatter::HttpFormatterContext& context,
                                         const StreamInfo::StreamInfo& stream_info) const override;
  void finalizeRequestHeaders(Http::RequestHeaderMap& headers,
                              const Formatter::HttpFormatterContext& context,
                              const StreamInfo::StreamInfo& stream_info,
                              bool insert_envoy_original_path) const override;
  Http::HeaderTransforms requestHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                 bool do_formatting = true) const override;

  const Http::HashPolicy* hashPolicy() const override;
  const HedgePolicy& hedgePolicy() const override;
  Upstream::ResourcePriority priority() const override;
  const RateLimitPolicy& rateLimitPolicy() const override;
  const RetryPolicyConstSharedPtr& retryPolicy() const override;
  const Router::PathMatcherSharedPtr& pathMatcher() const override;
  const Router::PathRewriterSharedPtr& pathRewriter() const override;
  const InternalRedirectPolicy& internalRedirectPolicy() const override;
  uint64_t requestBodyBufferLimit() const override;
  const std::vector<Router::ShadowPolicyPtr>& shadowPolicies() const override;
  std::chrono::milliseconds timeout() const override;
  absl::optional<std::chrono::milliseconds> idleTimeout() const override;
  absl::optional<std::chrono::milliseconds> flushTimeout() const override;
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
  void refreshRouteCluster(const Http::RequestHeaderMap& headers,
                           const StreamInfo::StreamInfo& stream_info) const override;

private:
  const RouteEntry* base_route_entry_{};
};

/**
 * A DynamicRouteEntry is a DelegatingRouteEntry that overrides the clusterName() method.
 * The cluster name is determined by the filter that created this route entry.
 */
class DynamicRouteEntry : public DelegatingRouteEntry {
public:
  DynamicRouteEntry(RouteConstSharedPtr route, std::string&& cluster_name)
      : DelegatingRouteEntry(std::move(route)), cluster_name_(std::move(cluster_name)) {}

  const std::string& clusterName() const override { return cluster_name_; }

private:
  const std::string cluster_name_;
};

} // namespace Router
} // namespace Envoy
