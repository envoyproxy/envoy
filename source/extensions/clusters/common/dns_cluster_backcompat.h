#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dns/v3/dns_cluster.pb.h"

namespace Envoy {
namespace Upstream {

/**
* create a DnsCluster from the legacy Cluster options so that we only have to worry about one API.
* NOTE: this does not consider the `typed_dns_resolver_config` field.
*/
void createDnsClusterFromLegacyFields(
    const envoy::config::cluster::v3::Cluster& cluster,
    envoy::extensions::clusters::dns::v3::DnsCluster& new_proto_config);

} // namespace Upstream
} // namespace Envoy
