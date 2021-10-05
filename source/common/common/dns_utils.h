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

std::vector<Network::Address::InstanceConstSharedPtr>
generateAddressList(const std::list<Network::DnsResponse>& responses, uint32_t port);

bool listChanged(const std::vector<Network::Address::InstanceConstSharedPtr>& list1,
                 const std::vector<Network::Address::InstanceConstSharedPtr>& list2);
} // namespace DnsUtils
} // namespace Envoy
