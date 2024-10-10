#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dns/v3/dns_cluster.pb.h"
#include "source/extensions/clusters/common/backcompat_dns.h"

namespace Envoy {
namespace Upstream {

void mergeClusterAndProtoConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    envoy::extensions::clusters::dns::v3::DnsCluster& new_proto_config) {

  if (cluster.has_dns_refresh_rate() && !new_proto_config.has_dns_refresh_rate()) {
    new_proto_config.mutable_dns_refresh_rate()->CopyFrom(cluster.dns_refresh_rate());
  }

  if (cluster.has_dns_failure_refresh_rate() && !new_proto_config.has_dns_failure_refresh_rate()) {
    auto* new_refresh_rate = new_proto_config.mutable_dns_failure_refresh_rate();
    const auto& old_refresh_rate = cluster.dns_failure_refresh_rate();
    new_refresh_rate->mutable_max_interval()->CopyFrom(old_refresh_rate.max_interval());
    new_refresh_rate->mutable_base_interval()->CopyFrom(old_refresh_rate.base_interval());
  }

  if (!new_proto_config.has_respect_dns_ttl()) {
    new_proto_config.mutable_respect_dns_ttl()->set_value(cluster.respect_dns_ttl());
  }

  // if (new_proto_config.dns_lookup_family() ==
  //     envoy::extensions::clusters::dns::v3::DnsCluster::UNSPECIFIED) {
  //   new_proto_config.set_dns_lookup_family(translateLookupFamily(cluster.dns_lookup_family()));
  // }

  // if (cluster.has_typed_dns_resolver_config() &&
  //     !new_proto_config.has_typed_dns_resolver_config()) {
  //   new_proto_config.mutable_typed_dns_resolver_config()->CopyFrom(
  //       cluster.typed_dns_resolver_config());
  // }

  if (cluster.has_dns_jitter() && !new_proto_config.has_dns_jitter()) {
    new_proto_config.mutable_dns_jitter()->CopyFrom(cluster.dns_jitter());
  }
}
} // namespace Upstream
} // namespace Envoy
