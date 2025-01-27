#include "config.h"

#include "upstream_request.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {
namespace Golang {

Router::GenericConnPoolPtr GolangGenericConnPoolFactory::createGenericConnPool(
    Upstream::HostConstSharedPtr, Upstream::ThreadLocalCluster& thread_local_cluster,
    Router::GenericConnPoolFactory::UpstreamProtocol, Upstream::ResourcePriority priority,
    absl::optional<Envoy::Http::Protocol>, Upstream::LoadBalancerContext* ctx,
    const Protobuf::Message& config) const {
  auto ret = std::make_unique<TcpConnPool>(thread_local_cluster, priority, ctx, config);
  return (ret->valid() ? std::move(ret) : nullptr);
}

REGISTER_FACTORY(GolangGenericConnPoolFactory, Router::GenericConnPoolFactory);

} // namespace Golang
} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
