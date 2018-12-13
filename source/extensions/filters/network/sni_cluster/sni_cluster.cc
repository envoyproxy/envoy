#include "extensions/filters/network/sni_cluster/sni_cluster.h"

#include "envoy/network/connection.h"

#include "common/common/assert.h"
#include "common/tcp_proxy/tcp_proxy.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniCluster {

Network::FilterStatus SniClusterFilter::onNewConnection() {
  absl::string_view sni = read_callbacks_->connection().requestedServerName();
  ENVOY_CONN_LOG(trace, "sni_cluster: new connection with server name {}",
                 read_callbacks_->connection(), sni);

  if (!sni.empty()) {
    // Set the tcp_proxy cluster to the same value as SNI. The data is mutable to allow
    // other filters to change it.
    read_callbacks_->connection().streamInfo().filterState().setData(
        TcpProxy::PerConnectionCluster::key(),
        std::make_unique<TcpProxy::PerConnectionCluster>(sni),
        StreamInfo::FilterState::StateType::Mutable);
  }

  return Network::FilterStatus::Continue;
}

} // namespace SniCluster
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
