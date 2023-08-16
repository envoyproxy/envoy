#include "source/extensions/filters/udp/dns_filter/dns_filter_utils.h"

#include <algorithm>

#include "envoy/common/platform.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/logger.h"
#include "source/common/network/address_impl.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {
namespace Utils {

std::string getProtoName(const DnsTable::DnsServiceProtocol& protocol) {
  std::string proto = protocol.name();
  if (proto.empty()) {
    switch (protocol.number()) {
    case 6:
      proto = "tcp";
      break;
    case 17:
      proto = "udp";
      break;
    default:
      // For Envoy to resolve a protocol to a name "/etc/protocols"
      // should exist. This isn't guaranteed. Since most services are
      // tcp or udp, if we get a different value, return an empty string.
      proto = EMPTY_STRING;
      break;
    } // end switch
  }
  return proto;
}

absl::string_view getServiceFromName(const absl::string_view name) {
  const size_t offset = name.find_first_of('.');
  if (offset != std::string::npos && offset < name.size()) {
    size_t start = 0;
    if (name[start] == '_') {
      return name.substr(++start, offset - 1);
    }
  }
  return EMPTY_STRING;
}

std::string buildServiceName(const std::string& name, const std::string& proto,
                             const std::string& domain) {
  std::string result{};
  if (name[0] != '_') {
    result += "_";
  }
  absl::StrAppend(&result, name, ".");
  if (proto[0] != '_') {
    result += "_";
  }
  absl::StrAppend(&result, proto, ".", domain);
  return result;
}

absl::optional<uint16_t>
getAddressRecordType(const Network::Address::InstanceConstSharedPtr& ipaddr) {
  if (ipaddr->type() == Network::Address::Type::Ip) {
    if (ipaddr->ip()->ipv6() != nullptr) {
      return absl::make_optional<uint16_t>(DNS_RECORD_TYPE_AAAA);
    } else if (ipaddr->ip()->ipv4() != nullptr) {
      return absl::make_optional<uint16_t>(DNS_RECORD_TYPE_A);
    }
  }
  return absl::nullopt;
}

absl::string_view getDomainSuffix(const absl::string_view name) {
  size_t pos, offset = std::string::npos, iterations = MAX_SUFFIX_LABEL_COUNT;

  // Start from the end of a name and attempt to determine the last
  // two labels. This should return the domain name and the top level
  // domain
  while (iterations--) {
    pos = name.find_last_of('.', offset);
    if (pos == std::string::npos) {
      return name;
    }
    offset = pos - 1;
  }

  return name.substr(pos + 1);
}

} // namespace Utils
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
