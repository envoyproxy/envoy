#include "source/common/common/dns_utils.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace DnsUtils {

Network::DnsLookupFamily
getDnsLookupFamilyFromCluster(const envoy::config::cluster::v3::Cluster& cluster) {
  return getDnsLookupFamilyFromEnum(cluster.dns_lookup_family());
}

Network::DnsLookupFamily
getDnsLookupFamilyFromEnum(envoy::config::cluster::v3::Cluster::DnsLookupFamily family) {
  switch (family) {
  case envoy::config::cluster::v3::Cluster::V6_ONLY:
    return Network::DnsLookupFamily::V6Only;
  case envoy::config::cluster::v3::Cluster::V4_ONLY:
    return Network::DnsLookupFamily::V4Only;
  case envoy::config::cluster::v3::Cluster::AUTO:
    return Network::DnsLookupFamily::Auto;
  case envoy::config::cluster::v3::Cluster::V4_PREFERRED:
    return Network::DnsLookupFamily::V4Preferred;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace DnsUtils
} // namespace Envoy
