#include "extensions/upstreams/tcp/generic/config.h"

#include "envoy/upstream/cluster_manager.h"

#include "common/http/codec_client.h"
#include "common/tcp_proxy/upstream.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Tcp {
namespace Generic {

TcpProxy::GenericConnPoolPtr GenericConnPoolFactory::createGenericConnPool(
    Upstream::ThreadLocalCluster& thread_local_cluster,
    const absl::optional<TunnelingConfig>& config, Upstream::LoadBalancerContext* context,
    Envoy::Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks) const {
  if (config.has_value()) {
    auto pool_type =
        ((thread_local_cluster.info()->features() & Upstream::ClusterInfo::Features::HTTP2) != 0)
            ? Http::CodecClient::Type::HTTP2
            : Http::CodecClient::Type::HTTP1;
    auto ret = std::make_unique<TcpProxy::HttpConnPool>(
        thread_local_cluster, context, config.value(), upstream_callbacks, pool_type);
    return (ret->valid() ? std::move(ret) : nullptr);
  }
  auto ret =
      std::make_unique<TcpProxy::TcpConnPool>(thread_local_cluster, context, upstream_callbacks);
  return (ret->valid() ? std::move(ret) : nullptr);
}

REGISTER_FACTORY(GenericConnPoolFactory, TcpProxy::GenericConnPoolFactory);

} // namespace Generic
} // namespace Tcp
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
