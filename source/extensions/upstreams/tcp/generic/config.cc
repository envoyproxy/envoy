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
    const std::string& cluster_name, Upstream::ClusterManager& cluster_manager,
    const absl::optional<TunnelingConfig>& config, Upstream::LoadBalancerContext* context,
    Envoy::Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks) const {
  if (config.has_value()) {
    auto* cluster = cluster_manager.getThreadLocalCluster(cluster_name);
    if (!cluster) {
      return nullptr;
    }
    auto pool_type = ((cluster->info()->features() & Upstream::ClusterInfo::Features::HTTP2) != 0)
                         ? Http::CodecClient::Type::HTTP2
                         : Http::CodecClient::Type::HTTP1;
    auto ret = std::make_unique<TcpProxy::HttpConnPool>(
        cluster_name, cluster_manager, context, config.value(), upstream_callbacks, pool_type);
    return (ret->valid() ? std::move(ret) : nullptr);
  }
  auto ret = std::make_unique<TcpProxy::TcpConnPool>(cluster_name, cluster_manager, context,
                                                     upstream_callbacks);
  return (ret->valid() ? std::move(ret) : nullptr);
}

REGISTER_FACTORY(GenericConnPoolFactory, TcpProxy::GenericConnPoolFactory);

} // namespace Generic
} // namespace Tcp
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
