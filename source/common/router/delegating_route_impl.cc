#include "common/router/delegating_route_impl.h"

namespace Envoy {
namespace Router {

// Router::DelegatingRoute
const DirectResponseEntry* DelegatingRoute::directResponseEntry() const {
  return base_route_->directResponseEntry();
}

const RouteEntry* DelegatingRoute::routeEntry() const { return base_route_->routeEntry(); }

const Decorator* DelegatingRoute::decorator() const { return base_route_->decorator(); }

const RouteTracing* DelegatingRoute::tracingConfig() const { return base_route_->tracingConfig(); }

const RouteSpecificFilterConfig* DelegatingRoute::perFilterConfig(const std::string& name) const {
  return base_route_->perFilterConfig(name);
}

// Router:DelegatingRouteEntry
void DelegatingRouteEntry::finalizeResponseHeaders(
    Http::ResponseHeaderMap& headers, const StreamInfo::StreamInfo& stream_info) const {
  return base_route_entry_->finalizeResponseHeaders(headers, stream_info);
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

void DelegatingRouteEntry::finalizeRequestHeaders(Http::RequestHeaderMap& headers,
                                                  const StreamInfo::StreamInfo& stream_info,
                                                  bool insert_envoy_original_path) const {
  return base_route_entry_->finalizeRequestHeaders(headers, stream_info,
                                                   insert_envoy_original_path);
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

const InternalRedirectPolicy& DelegatingRouteEntry::internalRedirectPolicy() const {
  return base_route_entry_->internalRedirectPolicy();
}

uint32_t DelegatingRouteEntry::retryShadowBufferLimit() const {
  return base_route_entry_->retryShadowBufferLimit();
}

const std::vector<ShadowPolicyPtr>& DelegatingRouteEntry::shadowPolicies() const {
  return base_route_entry_->shadowPolicies();
}

std::chrono::milliseconds DelegatingRouteEntry::timeout() const {
  return base_route_entry_->timeout();
}

absl::optional<std::chrono::milliseconds> DelegatingRouteEntry::idleTimeout() const {
  return base_route_entry_->idleTimeout();
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

const VirtualCluster* DelegatingRouteEntry::virtualCluster(const Http::HeaderMap& headers) const {
  return base_route_entry_->virtualCluster(headers);
}

const VirtualHost& DelegatingRouteEntry::virtualHost() const {
  return base_route_entry_->virtualHost();
}

bool DelegatingRouteEntry::autoHostRewrite() const { return base_route_entry_->autoHostRewrite(); }

const MetadataMatchCriteria* DelegatingRouteEntry::metadataMatchCriteria() const {
  return base_route_entry_->metadataMatchCriteria();
}

const std::multimap<std::string, std::string>& DelegatingRouteEntry::opaqueConfig() const {
  return base_route_entry_->opaqueConfig();
}

bool DelegatingRouteEntry::includeVirtualHostRateLimits() const {
  return base_route_entry_->includeVirtualHostRateLimits();
}

const Envoy::Config::TypedMetadata& DelegatingRouteEntry::typedMetadata() const {
  return base_route_entry_->typedMetadata();
}

const envoy::config::core::v3::Metadata& DelegatingRouteEntry::metadata() const {
  return base_route_entry_->metadata();
}

const TlsContextMatchCriteria* DelegatingRouteEntry::tlsContextMatchCriteria() const {
  return base_route_entry_->tlsContextMatchCriteria();
}

const PathMatchCriterion& DelegatingRouteEntry::pathMatchCriterion() const {
  return base_route_entry_->pathMatchCriterion();
}

const RouteSpecificFilterConfig*
DelegatingRouteEntry::perFilterConfig(const std::string& name) const {
  return base_route_entry_->perFilterConfig(name);
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
const absl::optional<ConnectConfig>& DelegatingRouteEntry::connectConfig() const {
  return base_route_entry_->connectConfig();
}

const std::string& DelegatingRouteEntry::routeName() const {
  return base_route_entry_->routeName();
}

} // namespace Router
} // namespace Envoy
