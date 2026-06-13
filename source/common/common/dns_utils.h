#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dns/v3/dns_cluster.pb.h"
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
Network::DnsLookupFamily
getDnsLookupFamilyFromEnum(envoy::extensions::clusters::common::dns::v3::DnsLookupFamily family);

// Generates a list of InstanceConstSharedPtr from the DNS responses provided.
std::vector<Network::Address::InstanceConstSharedPtr>
generateAddressList(const std::list<Network::DnsResponse>& responses, uint32_t port);

// Returns true if list1 differs from list2, false otherwise.
bool listChanged(const std::vector<Network::Address::InstanceConstSharedPtr>& list1,
                 const std::vector<Network::Address::InstanceConstSharedPtr>& list2);

// Parses raw HTTPS RDATA (RFC 9460) and returns the ECHConfigList if found.
// Returns empty vector if not found or parsing fails.
std::vector<uint8_t> parseHttpsRecord(const std::vector<uint8_t>& rdata);
} // namespace DnsUtils
} // namespace Envoy
