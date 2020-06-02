#pragma once

#include "envoy/registry/registry.h"
#include "envoy/router/router.h"

#include "extensions/upstreams/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Default {

/**
 * Config registration for the Default conn pool.
 * @see FIXME.
 */
class DefaultGenericConnPoolFactory : public Router::GenericConnPoolFactory {
public:
  ~DefaultGenericConnPoolFactory() override = default;
  std::string name() const override { return HttpUpstreamsNames::get().Default; }
  std::string category() const override { return "envoy.upstreams"; }
  Router::GenericConnPoolPtr
  createGenericConnPool(Upstream::ClusterManager& cm, bool is_connect,
                        const Router::RouteEntry& route_entry, Envoy::Http::Protocol protocol,
                        Upstream::LoadBalancerContext* ctx) const override;
};

DECLARE_FACTORY(DefaultGenericConnPoolFactory);

} // namespace Default
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
