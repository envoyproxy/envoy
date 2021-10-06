#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/network/dns.h"

namespace Envoy {
namespace DnsUtils {

/**
 * Utility function to get Dns from cluster/enum.
 */
Network::DnsLookupFamily
getDnsLookupFamilyFromCluster(const envoy::config::cluster::v3::Cluster& cluster);
Network::DnsLookupFamily
getDnsLookupFamilyFromEnum(envoy::config::cluster::v3::Cluster::DnsLookupFamily family);

} // namespace DnsUtils
} // namespace Envoy
