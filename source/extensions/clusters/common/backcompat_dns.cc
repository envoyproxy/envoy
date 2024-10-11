#include "source/extensions/clusters/common/backcompat_dns.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dns/v3/dns_cluster.pb.h"

namespace Envoy {
namespace Upstream {

void createDnsClusterFromLegacyFields(
    const envoy::config::cluster::v3::Cluster& cluster,
    envoy::extensions::clusters::dns::v3::DnsCluster& dns_cluster) {

  // We have to add all these guards because otherwise dns_cluster.mutable_FIELD will initialize the
  // field and dns_cluster.has_FIELD will return true
  if (cluster.has_dns_refresh_rate()) {
    dns_cluster.mutable_dns_refresh_rate()->CopyFrom(cluster.dns_refresh_rate());
  }

  // FIXME: tests this
  if (cluster.has_dns_failure_refresh_rate()) {
    auto* new_refresh_rate = dns_cluster.mutable_dns_failure_refresh_rate();
    const auto& old_refresh_rate = cluster.dns_failure_refresh_rate();

    if (old_refresh_rate.has_max_interval()) {
      new_refresh_rate->mutable_max_interval()->CopyFrom(old_refresh_rate.max_interval());
    }
    if (old_refresh_rate.has_base_interval()) {
      new_refresh_rate->mutable_base_interval()->CopyFrom(old_refresh_rate.base_interval());
    }
  }

  dns_cluster.set_respect_dns_ttl(cluster.respect_dns_ttl());

  if (cluster.has_dns_jitter()) {
    dns_cluster.mutable_dns_jitter()->CopyFrom(cluster.dns_jitter());
  }
}

} // namespace Upstream
} // namespace Envoy
