#include "extensions/filters/network/sni_cluster/sni_cluster.h"

#include <regex>

#include "envoy/network/connection.h"

#include "common/common/assert.h"
#include "common/tcp_proxy/tcp_proxy.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniCluster {

SniClusterFilterConfig::SniClusterFilterConfig(
    const envoy::config::filter::network::sni_cluster::v2::SniCluster& proto_config)
    : sni_substitution_(proto_config.sni_substitution()) {}

Network::FilterStatus SniClusterFilter::onNewConnection() {
  absl::string_view sni = read_callbacks_->connection().requestedServerName();
  ENVOY_CONN_LOG(trace, "sni_cluster: new connection with server name {}",
                 read_callbacks_->connection(), sni);

  if (!sni.empty()) {
    // Rewrite the SNI value prior to setting the tcp_proxy cluster name.
    std::string sni_str(absl::StrCat(sni));
    if (!config_->sniSubstitution().regex().empty()) {
      sni_str = std::regex_replace(sni_str, std::regex(config_->sniSubstitution().regex()),
                                   config_->sniSubstitution().replacement());
    }
    ENVOY_CONN_LOG(trace, "sni_cluster: tcp proxy cluster name {}", read_callbacks_->connection(),
                   sni_str);

    // Set the tcp_proxy cluster to the same value as SNI. The data is mutable to allow
    // other filters to change it.
    read_callbacks_->connection().streamInfo().filterState().setData(
        TcpProxy::PerConnectionCluster::Key,
        std::make_unique<TcpProxy::PerConnectionCluster>(sni_str),
        StreamInfo::FilterState::StateType::Mutable);
  }

  return Network::FilterStatus::Continue;
}

} // namespace SniCluster
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
