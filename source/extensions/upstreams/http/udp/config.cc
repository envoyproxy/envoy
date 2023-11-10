#include "source/extensions/upstreams/http/udp/config.h"

#include "source/extensions/upstreams/http/udp/upstream_request.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Udp {

Router::GenericConnPoolPtr UdpGenericConnPoolFactory::createGenericConnPool(
    Upstream::ThreadLocalCluster& thread_local_cluster,
    Router::GenericConnPoolFactory::UpstreamProtocol, Upstream::ResourcePriority,
    absl::optional<Envoy::Http::Protocol>, Upstream::LoadBalancerContext* ctx) const {
  auto ret = std::make_unique<UdpConnPool>(thread_local_cluster, ctx);
  return (ret->valid() ? std::move(ret) : nullptr);
}

REGISTER_FACTORY(UdpGenericConnPoolFactory, Router::GenericConnPoolFactory);

} // namespace Udp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
