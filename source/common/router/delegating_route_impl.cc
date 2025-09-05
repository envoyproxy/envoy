#include "source/common/router/delegating_route_impl.h"

namespace Envoy {
namespace Router {

// Router:DelegatingRouteEntry
void DelegatingRouteEntry::finalizeResponseHeaders(
    Http::ResponseHeaderMap& headers, const StreamInfo::StreamInfo& stream_info) const {
  return base_route_entry_->finalizeResponseHeaders(headers, stream_info);
}

Http::HeaderTransforms
DelegatingRouteEntry::responseHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                               bool do_formatting) const {
  return base_route_entry_->responseHeaderTransforms(stream_info, do_formatting);
}

const std::string& DelegatingRouteEntry::clusterName() const {
  return base_route_entry_->clusterName();
}

Http::Code DelegatingRouteEntry::clusterNotFoundResponseCode() const {
  return base_route_entry_->clusterNotFoundResponseCode();
}

const CorsPolicy* DelegatingRouteEntry::corsPolicy() const {
  return base_route_entry_->corsPolicy();
}

absl::optional<std::string>
DelegatingRouteEntry::currentUrlPathAfterRewrite(const Http::RequestHeaderMap& headers) const {
  return base_route_entry_->currentUrlPathAfterRewrite(headers);
}

void DelegatingRouteEntry::finalizeRequestHeaders(Http::RequestHeaderMap& headers,
                                                  const StreamInfo::StreamInfo& stream_info,
                                                  bool insert_envoy_original_path) const {
  return base_route_entry_->finalizeRequestHeaders(headers, stream_info,
                                                   insert_envoy_original_path);
}

Http::HeaderTransforms
DelegatingRouteEntry::requestHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                              bool do_formatting) const {
  return base_route_entry_->requestHeaderTransforms(stream_info, do_formatting);
}

const Http::HashPolicy* DelegatingRouteEntry::hashPolicy() const {
  return base_route_entry_->hashPolicy();
}

const HedgePolicy& DelegatingRouteEntry::hedgePolicy() const {
  return base_route_entry_->hedgePolicy();
}

Upstream::ResourcePriority DelegatingRouteEntry::priority() const {
  return base_route_entry_->priority();
}

const RateLimitPolicy& DelegatingRouteEntry::rateLimitPolicy() const {
  return base_route_entry_->rateLimitPolicy();
}

const RetryPolicy& DelegatingRouteEntry::retryPolicy() const {
  return base_route_entry_->retryPolicy();
}

const PathMatcherSharedPtr& DelegatingRouteEntry::pathMatcher() const {
  return base_route_entry_->pathMatcher();
}

const PathRewriterSharedPtr& DelegatingRouteEntry::pathRewriter() const {
  return base_route_entry_->pathRewriter();
}

const InternalRedirectPolicy& DelegatingRouteEntry::internalRedirectPolicy() const {
  return base_route_entry_->internalRedirectPolicy();
}

uint64_t DelegatingRouteEntry::requestBodyBufferLimit() const {
  return base_route_entry_->requestBodyBufferLimit();
}

const std::vector<Router::ShadowPolicyPtr>& DelegatingRouteEntry::shadowPolicies() const {
  return base_route_entry_->shadowPolicies();
}

std::chrono::milliseconds DelegatingRouteEntry::timeout() const {
  return base_route_entry_->timeout();
}

absl::optional<std::chrono::milliseconds> DelegatingRouteEntry::idleTimeout() const {
  return base_route_entry_->idleTimeout();
}

absl::optional<std::chrono::milliseconds> DelegatingRouteEntry::flushTimeout() const {
  return base_route_entry_->flushTimeout();
}

bool DelegatingRouteEntry::usingNewTimeouts() const {
  return base_route_entry_->usingNewTimeouts();
}

absl::optional<std::chrono::milliseconds> DelegatingRouteEntry::maxStreamDuration() const {
  return base_route_entry_->maxStreamDuration();
}

absl::optional<std::chrono::milliseconds> DelegatingRouteEntry::grpcTimeoutHeaderMax() const {
  return base_route_entry_->grpcTimeoutHeaderMax();
}

absl::optional<std::chrono::milliseconds> DelegatingRouteEntry::grpcTimeoutHeaderOffset() const {
  return base_route_entry_->grpcTimeoutHeaderOffset();
}

absl::optional<std::chrono::milliseconds> DelegatingRouteEntry::maxGrpcTimeout() const {
  return base_route_entry_->maxGrpcTimeout();
}

absl::optional<std::chrono::milliseconds> DelegatingRouteEntry::grpcTimeoutOffset() const {
  return base_route_entry_->grpcTimeoutOffset();
}

bool DelegatingRouteEntry::autoHostRewrite() const { return base_route_entry_->autoHostRewrite(); }

bool DelegatingRouteEntry::appendXfh() const { return base_route_entry_->appendXfh(); }

const MetadataMatchCriteria* DelegatingRouteEntry::metadataMatchCriteria() const {
  return base_route_entry_->metadataMatchCriteria();
}

const std::multimap<std::string, std::string>& DelegatingRouteEntry::opaqueConfig() const {
  return base_route_entry_->opaqueConfig();
}

bool DelegatingRouteEntry::includeVirtualHostRateLimits() const {
  return base_route_entry_->includeVirtualHostRateLimits();
}

const TlsContextMatchCriteria* DelegatingRouteEntry::tlsContextMatchCriteria() const {
  return base_route_entry_->tlsContextMatchCriteria();
}

const PathMatchCriterion& DelegatingRouteEntry::pathMatchCriterion() const {
  return base_route_entry_->pathMatchCriterion();
}

bool DelegatingRouteEntry::includeAttemptCountInRequest() const {
  return base_route_entry_->includeAttemptCountInRequest();
}

bool DelegatingRouteEntry::includeAttemptCountInResponse() const {
  return base_route_entry_->includeAttemptCountInResponse();
}

using UpgradeMap = std::map<std::string, bool>;
const UpgradeMap& DelegatingRouteEntry::upgradeMap() const {
  return base_route_entry_->upgradeMap();
}

using ConnectConfig = envoy::config::route::v3::RouteAction::UpgradeConfig::ConnectConfig;
using ConnectConfigOptRef = OptRef<ConnectConfig>;
const ConnectConfigOptRef DelegatingRouteEntry::connectConfig() const {
  return base_route_entry_->connectConfig();
}

const EarlyDataPolicy& DelegatingRouteEntry::earlyDataPolicy() const {
  return base_route_entry_->earlyDataPolicy();
}

const RouteStatsContextOptRef DelegatingRouteEntry::routeStatsContext() const {
  return base_route_entry_->routeStatsContext();
}

void DelegatingRouteEntry::refreshRouteCluster(const Http::RequestHeaderMap& headers,
                                               const StreamInfo::StreamInfo& stream_info) const {
  base_route_entry_->refreshRouteCluster(headers, stream_info);
}

} // namespace Router
} // namespace Envoy
