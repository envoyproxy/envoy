#pragma once

#include "envoy/extensions/filters/udp/dns_filter/v3/dns_filter.pb.h"
#include "envoy/network/address.h"

#include "source/extensions/filters/udp/dns_filter/dns_filter_constants.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {
namespace Utils {

using envoy::data::dns::v3::DnsTable;

/**
 * @brief Returns the protocol name string from the configured protobuf entity
 */
std::string getProtoName(const DnsTable::DnsServiceProtocol& protocol);

/**
 * @brief Extracts the service name from the fully qualified service name. The leading underscore
 * is discarded from the output
 */
absl::string_view getServiceFromName(const absl::string_view name);

/**
 * @brief Construct the full service name, including underscores, from the name, protocol and
 * domain fields.
 *
 * A DNS service record name must have the protocol and name labels begin with underscores. This
 * function validates the input fields and concatenates them to form the full name
 */
std::string buildServiceName(const std::string& name, const std::string& proto,
                             const std::string& domain);

absl::optional<uint16_t>
getAddressRecordType(const Network::Address::InstanceConstSharedPtr& ipaddr);

/**
 * @brief For a given fully qualified domain name, extract up to the last two labels to form a
 * domain suffix.
 */
absl::string_view getDomainSuffix(const absl::string_view name);

/**
 * @brief For given domain name return virtual domain name that will be used for internal
 * resolution. Valid wildcard names are sanitized and converted to format to store in
 * virtual domain config.
 */
absl::string_view getVirtualDomainName(const absl::string_view domain_name);

} // namespace Utils
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
