#include "source/common/common/dns_utils.h"

#include "source/common/common/assert.h"
#include "source/common/network/utility.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace DnsUtils {

Network::DnsLookupFamily
getDnsLookupFamilyFromCluster(const envoy::config::cluster::v3::Cluster& cluster) {
  return getDnsLookupFamilyFromEnum(cluster.dns_lookup_family());
}

Network::DnsLookupFamily
getDnsLookupFamilyFromEnum(envoy::config::cluster::v3::Cluster::DnsLookupFamily family) {
  switch (family) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::cluster::v3::Cluster::V6_ONLY:
    return Network::DnsLookupFamily::V6Only;
  case envoy::config::cluster::v3::Cluster::V4_ONLY:
    return Network::DnsLookupFamily::V4Only;
  case envoy::config::cluster::v3::Cluster::AUTO:
    return Network::DnsLookupFamily::Auto;
  case envoy::config::cluster::v3::Cluster::V4_PREFERRED:
    return Network::DnsLookupFamily::V4Preferred;
  case envoy::config::cluster::v3::Cluster::ALL:
    return Network::DnsLookupFamily::All;
  }
  IS_ENVOY_BUG("unexpected dns lookup family enum");
  return Network::DnsLookupFamily::All;
}

std::vector<Network::Address::InstanceConstSharedPtr>
generateAddressList(const std::list<Network::DnsResponse>& responses, uint32_t port) {
  std::vector<Network::Address::InstanceConstSharedPtr> addresses;
  for (const auto& response : responses) {
    auto address = Network::Utility::getAddressWithPort(*(response.addrInfo().address_), port);
    if (address) {
      addresses.push_back(address);
    }
  }
  return addresses;
}

bool listChanged(const std::vector<Network::Address::InstanceConstSharedPtr>& list1,
                 const std::vector<Network::Address::InstanceConstSharedPtr>& list2) {
  if (list1.size() != list2.size()) {
    return true;
  }
  // Eventually we could rewrite this to not count a change to the order of
  // addresses as a functional change.
  for (size_t i = 0; i < list1.size(); ++i) {
    if (*list1[i] != *list2[i]) {
      return true;
    }
  }
  return false;
}

} // namespace DnsUtils
} // namespace Envoy
