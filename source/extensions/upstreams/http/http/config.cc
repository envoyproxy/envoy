#include "extensions/upstreams/http/http/config.h"

#include "extensions/upstreams/http/http/upstream_request.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Http {

Router::GenericConnPoolPtr HttpGenericConnPoolFactory::createGenericConnPool(
    Upstream::ClusterManager& cm, bool is_connect, const Router::RouteEntry& route_entry,
    absl::optional<Envoy::Http::Protocol> downstream_protocol,
    Upstream::LoadBalancerContext* ctx) const {
  auto ret = std::make_unique<HttpConnPool>(cm, is_connect, route_entry, downstream_protocol, ctx);
  return (ret->valid() ? std::move(ret) : nullptr);
}

REGISTER_FACTORY(HttpGenericConnPoolFactory, Router::GenericConnPoolFactory);

} // namespace Http
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
