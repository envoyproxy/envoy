#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dns/v3/dns_cluster.pb.h"

namespace Envoy {
namespace Upstream {

void createDnsClusterFromLegacyFields(
    const envoy::config::cluster::v3::Cluster& cluster,
    envoy::extensions::clusters::dns::v3::DnsCluster& new_proto_config);

}
} // namespace Envoy
