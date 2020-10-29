#include "extensions/upstreams/tcp/generic/config.h"

#include "envoy/upstream/cluster_manager.h"

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
    auto* cluster = cluster_manager.get(cluster_name);
    if (!cluster) {
      return nullptr;
    }
    // TODO(snowp): Ideally we should prevent this from being configured, but that's tricky to get
    // right since whether a cluster is invalid depends on both the tcp_proxy config + cluster
    // config.
    if ((cluster->info()->features() & Upstream::ClusterInfo::Features::HTTP2) == 0) {
      ENVOY_LOG_MISC(error, "Attempted to tunnel over HTTP/1.1, this is not supported. Set "
                            "http2_protocol_options on the cluster.");
      return nullptr;
    }
    auto ret = std::make_unique<TcpProxy::HttpConnPool>(cluster_name, cluster_manager, context,
                                                        config.value(), upstream_callbacks);
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
