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

Network::DnsLookupFamily
getDnsLookupFamilyFromEnum(envoy::extensions::clusters::common::dns::v3::DnsLookupFamily family) {
  switch (family) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::clusters::common::dns::v3::V6_ONLY:
    return Network::DnsLookupFamily::V6Only;
  case envoy::extensions::clusters::common::dns::v3::V4_ONLY:
    return Network::DnsLookupFamily::V4Only;
  case envoy::extensions::clusters::common::dns::v3::AUTO:
  case envoy::extensions::clusters::common::dns::v3::UNSPECIFIED:
    return Network::DnsLookupFamily::Auto;
  case envoy::extensions::clusters::common::dns::v3::V4_PREFERRED:
    return Network::DnsLookupFamily::V4Preferred;
  case envoy::extensions::clusters::common::dns::v3::ALL:
    return Network::DnsLookupFamily::All;
    break;
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

namespace {

class SafeByteReader {
public:
  SafeByteReader(const std::vector<uint8_t>& data) : data_(data), offset_(0) {}

  bool remain(size_t len) const { return offset_ + len <= data_.size(); }

  bool read16(uint16_t& val) {
    if (!remain(2))
      return false;
    val = (data_[offset_] << 8) | data_[offset_ + 1];
    offset_ += 2;
    return true;
  }

  bool read8(uint8_t& val) {
    if (!remain(1))
      return false;
    val = data_[offset_];
    offset_ += 1;
    return true;
  }

  bool skip(size_t len) {
    if (!remain(len))
      return false;
    offset_ += len;
    return true;
  }

  bool readBytes(std::vector<uint8_t>& out, size_t len) {
    if (!remain(len))
      return false;
    out.assign(data_.begin() + offset_, data_.begin() + offset_ + len);
    offset_ += len;
    return true;
  }

  bool skipDnsName() {
    while (true) {
      uint8_t len;
      if (!read8(len))
        return false;
      if (len == 0) {
        break; // End of name
      }
      if ((len & 0xC0) != 0) {
        if ((len & 0xC0) == 0xC0) {
          // Pointer (2 bytes total)
          uint8_t dummy;
          if (!read8(dummy))
            return false;
          break;
        }
        // Reserved label type
        return false;
      }
      // Regular label
      if (!skip(len))
        return false;
    }
    return true;
  }

private:
  const std::vector<uint8_t>& data_;
  size_t offset_;
};

} // namespace

std::vector<uint8_t> parseHttpsRecord(const std::vector<uint8_t>& rdata) {
  SafeByteReader reader(rdata);

  uint16_t priority;
  if (!reader.read16(priority)) {
    return {};
  }

  // Skip TargetName
  if (!reader.skipDnsName()) {
    return {};
  }

  // Now parse SvcParams
  while (reader.remain(4)) { // Each param has at least 2 bytes key + 2 bytes len
    uint16_t key;
    uint16_t len;
    if (!reader.read16(key) || !reader.read16(len)) {
      return {};
    }

    if (key == 5) { // ech
      std::vector<uint8_t> ech_config_list;
      if (!reader.readBytes(ech_config_list, len)) {
        return {};
      }
      return ech_config_list;
    } else {
      if (!reader.skip(len)) {
        return {};
      }
    }
  }

  return {};
}

} // namespace DnsUtils
} // namespace Envoy
