#include "source/common/router/delegating_route_impl.h"

namespace Envoy {
namespace Router {

// Router::DelegatingRoute
const DirectResponseEntry* DelegatingRoute::directResponseEntry() const {
  return base_route_->directResponseEntry();
}

const RouteEntry* DelegatingRoute::routeEntry() const { return base_route_->routeEntry(); }

const Decorator* DelegatingRoute::decorator() const { return base_route_->decorator(); }

const RouteTracing* DelegatingRoute::tracingConfig() const { return base_route_->tracingConfig(); }

// Router:DelegatingRouteEntry
void DelegatingRouteEntry::finalizeResponseHeaders(
    Http::ResponseHeaderMap& headers, const StreamInfo::StreamInfo& stream_info) const {
  return base_route_->routeEntry()->finalizeResponseHeaders(headers, stream_info);
}

Http::HeaderTransforms
DelegatingRouteEntry::responseHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                               bool do_formatting) const {
  return base_route_->routeEntry()->responseHeaderTransforms(stream_info, do_formatting);
}

const std::string& DelegatingRouteEntry::clusterName() const {
  return base_route_->routeEntry()->clusterName();
}

Http::Code DelegatingRouteEntry::clusterNotFoundResponseCode() const {
  return base_route_->routeEntry()->clusterNotFoundResponseCode();
}

const CorsPolicy* DelegatingRouteEntry::corsPolicy() const {
  return base_route_->routeEntry()->corsPolicy();
}

absl::optional<std::string>
DelegatingRouteEntry::currentUrlPathAfterRewrite(const Http::RequestHeaderMap& headers) const {
  return base_route_->routeEntry()->currentUrlPathAfterRewrite(headers);
}

void DelegatingRouteEntry::finalizeRequestHeaders(Http::RequestHeaderMap& headers,
                                                  const StreamInfo::StreamInfo& stream_info,
                                                  bool insert_envoy_original_path) const {
  return base_route_->routeEntry()->finalizeRequestHeaders(headers, stream_info,
                                                           insert_envoy_original_path);
}

Http::HeaderTransforms
DelegatingRouteEntry::requestHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                              bool do_formatting) const {
  return base_route_->routeEntry()->requestHeaderTransforms(stream_info, do_formatting);
}

const Http::HashPolicy* DelegatingRouteEntry::hashPolicy() const {
  return base_route_->routeEntry()->hashPolicy();
}

const HedgePolicy& DelegatingRouteEntry::hedgePolicy() const {
  return base_route_->routeEntry()->hedgePolicy();
}

Upstream::ResourcePriority DelegatingRouteEntry::priority() const {
  return base_route_->routeEntry()->priority();
}

const RateLimitPolicy& DelegatingRouteEntry::rateLimitPolicy() const {
  return base_route_->routeEntry()->rateLimitPolicy();
}

const RetryPolicy& DelegatingRouteEntry::retryPolicy() const {
  return base_route_->routeEntry()->retryPolicy();
}

const PathMatcherSharedPtr& DelegatingRouteEntry::pathMatcher() const {
  return base_route_->routeEntry()->pathMatcher();
}

const PathRewriterSharedPtr& DelegatingRouteEntry::pathRewriter() const {
  return base_route_->routeEntry()->pathRewriter();
}

const InternalRedirectPolicy& DelegatingRouteEntry::internalRedirectPolicy() const {
  return base_route_->routeEntry()->internalRedirectPolicy();
}

uint32_t DelegatingRouteEntry::retryShadowBufferLimit() const {
  return base_route_->routeEntry()->retryShadowBufferLimit();
}

const std::vector<Router::ShadowPolicyPtr>& DelegatingRouteEntry::shadowPolicies() const {
  return base_route_->routeEntry()->shadowPolicies();
}

std::chrono::milliseconds DelegatingRouteEntry::timeout() const {
  return base_route_->routeEntry()->timeout();
}

absl::optional<std::chrono::milliseconds> DelegatingRouteEntry::idleTimeout() const {
  return base_route_->routeEntry()->idleTimeout();
}

bool DelegatingRouteEntry::usingNewTimeouts() const {
  return base_route_->routeEntry()->usingNewTimeouts();
}

absl::optional<std::chrono::milliseconds> DelegatingRouteEntry::maxStreamDuration() const {
  return base_route_->routeEntry()->maxStreamDuration();
}

absl::optional<std::chrono::milliseconds> DelegatingRouteEntry::grpcTimeoutHeaderMax() const {
  return base_route_->routeEntry()->grpcTimeoutHeaderMax();
}

absl::optional<std::chrono::milliseconds> DelegatingRouteEntry::grpcTimeoutHeaderOffset() const {
  return base_route_->routeEntry()->grpcTimeoutHeaderOffset();
}

absl::optional<std::chrono::milliseconds> DelegatingRouteEntry::maxGrpcTimeout() const {
  return base_route_->routeEntry()->maxGrpcTimeout();
}

absl::optional<std::chrono::milliseconds> DelegatingRouteEntry::grpcTimeoutOffset() const {
  return base_route_->routeEntry()->grpcTimeoutOffset();
}

const VirtualCluster* DelegatingRouteEntry::virtualCluster(const Http::HeaderMap& headers) const {
  return base_route_->routeEntry()->virtualCluster(headers);
}

const VirtualHost& DelegatingRouteEntry::virtualHost() const {
  return base_route_->routeEntry()->virtualHost();
}

bool DelegatingRouteEntry::autoHostRewrite() const {
  return base_route_->routeEntry()->autoHostRewrite();
}

bool DelegatingRouteEntry::appendXfh() const { return base_route_->routeEntry()->appendXfh(); }

const MetadataMatchCriteria* DelegatingRouteEntry::metadataMatchCriteria() const {
  return base_route_->routeEntry()->metadataMatchCriteria();
}

const std::multimap<std::string, std::string>& DelegatingRouteEntry::opaqueConfig() const {
  return base_route_->routeEntry()->opaqueConfig();
}

bool DelegatingRouteEntry::includeVirtualHostRateLimits() const {
  return base_route_->routeEntry()->includeVirtualHostRateLimits();
}

const TlsContextMatchCriteria* DelegatingRouteEntry::tlsContextMatchCriteria() const {
  return base_route_->routeEntry()->tlsContextMatchCriteria();
}

const PathMatchCriterion& DelegatingRouteEntry::pathMatchCriterion() const {
  return base_route_->routeEntry()->pathMatchCriterion();
}

bool DelegatingRouteEntry::includeAttemptCountInRequest() const {
  return base_route_->routeEntry()->includeAttemptCountInRequest();
}

bool DelegatingRouteEntry::includeAttemptCountInResponse() const {
  return base_route_->routeEntry()->includeAttemptCountInResponse();
}

using UpgradeMap = std::map<std::string, bool>;
const UpgradeMap& DelegatingRouteEntry::upgradeMap() const {
  return base_route_->routeEntry()->upgradeMap();
}

using ConnectConfig = envoy::config::route::v3::RouteAction::UpgradeConfig::ConnectConfig;
using ConnectConfigOptRef = OptRef<ConnectConfig>;
const ConnectConfigOptRef DelegatingRouteEntry::connectConfig() const {
  return base_route_->routeEntry()->connectConfig();
}

const EarlyDataPolicy& DelegatingRouteEntry::earlyDataPolicy() const {
  return base_route_->routeEntry()->earlyDataPolicy();
}

const RouteStatsContextOptRef DelegatingRouteEntry::routeStatsContext() const {
  return base_route_->routeEntry()->routeStatsContext();
}

} // namespace Router
} // namespace Envoy
