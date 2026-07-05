#include "source/extensions/filters/http/upstream_rbac/upstream_rbac_filter.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/stream_info/upstream_address.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace UpstreamRBACFilter {

void UpstreamRoleBasedAccessControlFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  RBACFilter::RoleBasedAccessControlFilter::setDecoderFilterCallbacks(callbacks);
  if (auto cb = callbacks.upstreamCallbacks(); cb) {
    cb->addUpstreamCallbacks(*this);
  }
}

void UpstreamRoleBasedAccessControlFilter::setUpstreamAddress(
    const Network::Address::InstanceConstSharedPtr& address) {
  // Always (re)write the selected host's address so the `upstream_ip_port` matcher evaluates *this*
  // attempt's host. The upstream address filter state is request-scoped and shared across retries
  // (and may also be populated by the dynamic forward proxy filter via `save_upstream_address`), so
  // a stale or foreign address must never be trusted for an SSRF deny decision — overwrite it.
  //
  // NOTE: setData overwrites an existing entry only when it was stored at the same life span. An
  // entry written at a *higher* life span (e.g. LifeSpan::Connection) would trip a life-span
  // conflict and be left untouched instead. In the supported HTTP-over-TCP setup the upstream
  // address is only ever written at LifeSpan::Request (here, and by the dynamic forward proxy HTTP
  // filter), so this reliably overwrites.
  callbacks_->streamInfo().filterState()->setData(
      StreamInfo::UpstreamAddress::key(), std::make_shared<StreamInfo::UpstreamAddress>(address),
      StreamInfo::FilterState::LifeSpan::Request);
}

void UpstreamRoleBasedAccessControlFilter::onHostSelected(
    const Upstream::HostDescriptionConstSharedPtr& host) {
  ASSERT(host != nullptr);

  // A selected host may have no resolved address (e.g. a logical host). With no upstream address
  // there is nothing for the upstream_ip_port matcher to evaluate and no concrete connection
  // target, so skip evaluation — and avoid dereferencing a null address below.
  const Network::Address::InstanceConstSharedPtr address = host->address();
  if (address == nullptr) {
    ENVOY_LOG(warn, "selected upstream host has no resolved address; skipping upstream RBAC");
    return;
  }

  setUpstreamAddress(address);

  // The RBAC engine evaluates against the downstream connection (source IP, SSL, SNI, ...). A
  // proxied request always has one, but guard rather than dereference an empty OptRef.
  if (!callbacks_->connection().has_value()) {
    ENVOY_LOG(warn, "no downstream connection available at host selection; skipping upstream RBAC");
    return;
  }

  // onHostSelected() fires during the downstream router's decodeHeaders(), before this upstream
  // filter chain's own decodeHeaders() runs, so the request headers come from the (downstream)
  // stream info that the connection manager populated earlier.
  const Http::RequestHeaderMap* request_headers = callbacks_->streamInfo().getRequestHeaders();
  if (request_headers == nullptr) {
    request_headers = Http::StaticEmptyHeaders::get().request_headers.get();
  }

  ENVOY_LOG(debug, "checking upstream host {} for request", address->asString());

  // Reuse the downstream RBAC filter's shadow + enforced evaluation. evaluateEnforcedEngine() sends
  // the 403 local reply on denial (aborting the upstream connection) and returns StopIteration.
  Protobuf::Struct metrics;
  const bool shadow_engine_evaluated = evaluateShadowEngine(*request_headers, metrics);
  const Http::FilterHeadersStatus status = evaluateEnforcedEngine(*request_headers, metrics);
  const bool enforced_engine_evaluated =
      config_->engine(callbacks_, Filters::Common::RBAC::EnforcementMode::Enforced) != nullptr;

  if (shadow_engine_evaluated || enforced_engine_evaluated) {
    callbacks_->streamInfo().setDynamicMetadata(std::string(DynamicMetadataKey), metrics);
  }

  if (status != Http::FilterHeadersStatus::StopIteration) {
    ENVOY_LOG(debug, "allowed by default or by policy");
  }
}

} // namespace UpstreamRBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
