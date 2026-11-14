#pragma once

#include "envoy/common/optref.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Http {
class OnDemandAsyncClientProvider;
}
namespace Upstream {

// On-demand configuration shared across workers.
// Each primary or mirror discovery configuration has its own instance.
class OdCdsContext {
public:
  OdCdsContext(OdCdsApiHandlePtr api, std::chrono::milliseconds timeout,
               std::shared_ptr<Http::OnDemandAsyncClientProvider> client_provider = nullptr)
      : api_(std::move(api)), timeout_(timeout), client_provider_(std::move(client_provider)) {}

  OdCdsApiHandle& api() const { return *api_; }
  std::chrono::milliseconds timeout() const { return timeout_; }
  OptRef<Http::OnDemandAsyncClientProvider> clientProvider() const {
    return makeOptRefFromPtr(client_provider_.get());
  }

private:
  const OdCdsApiHandlePtr api_;
  const std::chrono::milliseconds timeout_;
  const std::shared_ptr<Http::OnDemandAsyncClientProvider> client_provider_;
};
using OdCdsContextSharedPtr = std::shared_ptr<const OdCdsContext>;

// Per-request wrapper that passes the mirror discovery context from the on-demand
// filter to the router.
class OdCdsFilterState : public StreamInfo::FilterState::Object {
public:
  explicit OdCdsFilterState(OdCdsContextSharedPtr mirror_discovery_context)
      : mirror_discovery_context_(std::move(mirror_discovery_context)) {}
  static constexpr absl::string_view key() { return "envoy.upstream.od_cds_context"; }
  OdCdsContextSharedPtr mirrorDiscoveryContextSharedPtr() const {
    return mirror_discovery_context_;
  }

private:
  const OdCdsContextSharedPtr mirror_discovery_context_;
};

} // namespace Upstream
} // namespace Envoy
