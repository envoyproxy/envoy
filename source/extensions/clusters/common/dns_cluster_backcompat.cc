#include "source/extensions/clusters/common/dns_cluster_backcompat.h"

#include "envoy/common/exception.h"
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

  switch (cluster.dns_lookup_family()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::cluster::v3::Cluster::AUTO:
    dns_cluster.set_dns_lookup_family(envoy::extensions::clusters::common::dns::v3::AUTO);
    break;
  case envoy::config::cluster::v3::Cluster::V4_ONLY:
    dns_cluster.set_dns_lookup_family(envoy::extensions::clusters::common::dns::v3::V4_ONLY);
    break;
  case envoy::config::cluster::v3::Cluster::V6_ONLY:
    dns_cluster.set_dns_lookup_family(envoy::extensions::clusters::common::dns::v3::V6_ONLY);
    break;
  case envoy::config::cluster::v3::Cluster::V4_PREFERRED:
    dns_cluster.set_dns_lookup_family(
        envoy::extensions::clusters::common::dns::v3::V4_PREFERRED);
    break;
  case envoy::config::cluster::v3::Cluster::ALL:
    dns_cluster.set_dns_lookup_family(envoy::extensions::clusters::common::dns::v3::ALL);
    break;
  }
}

} // namespace Upstream
} // namespace Envoy
