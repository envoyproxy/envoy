#pragma once

#include "envoy/http/filter.h"
#include "envoy/upstream/upstream.h"

#include "source/extensions/filters/http/rbac/rbac_filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace UpstreamRBACFilter {

// Dynamic metadata namespace used to expose the RBAC result, mirroring the downstream RBAC filter.
inline constexpr absl::string_view DynamicMetadataKey = "envoy.filters.http.upstream_rbac";

/**
 * An upstream HTTP filter that authorizes the *selected upstream host* using the RBAC engine.
 *
 * It is a thin specialization of the downstream RBAC filter: it reuses the engine, config and the
 * shadow/enforced evaluation logic (RoleBasedAccessControlFilter::evaluate{Shadow,Enforced}Engine),
 * but instead of evaluating during decodeHeaders it evaluates from the onHostSelected() callback —
 * after a host has been selected but before the upstream connection is initiated. This lets the
 * `upstream_ip_port` matcher work for every cluster type, including dynamic forward proxy
 * `sub_cluster_config` where the upstream address filter-state is otherwise never populated, and
 * rejects the request before any connection is established.
 */
class UpstreamRoleBasedAccessControlFilter : public RBACFilter::RoleBasedAccessControlFilter,
                                             public Http::UpstreamCallbacks {
public:
  using RBACFilter::RoleBasedAccessControlFilter::RoleBasedAccessControlFilter;

  // Http::StreamDecoderFilter
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;
  // The RBAC check runs in onHostSelected(), not during decode, so decoding just passes through.
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }

  // Http::UpstreamCallbacks
  void onHostSelected(const Upstream::HostDescriptionConstSharedPtr& host) override;
  void onUpstreamConnectionEstablished() override {}

private:
  // (Over)writes the selected host's address into the upstream address filter state so the
  // `upstream_ip_port` RBAC matcher resolves the host selected for *this* attempt.
  void setUpstreamAddress(const Network::Address::InstanceConstSharedPtr& address);
};

} // namespace UpstreamRBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
