#pragma once

#include "envoy/extensions/filters/udp/dns_filter/v3alpha/dns_filter.pb.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {
namespace Utils {

constexpr size_t MAX_LABEL_LENGTH = 63;
constexpr size_t MAX_NAME_LENGTH = 255;

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
 * @brief Extracts the protocol name from the fully qualified service name. The leading underscore
 * is discarded from the output
 */
absl::string_view getProtoFromName(const absl::string_view name);

/**
 * @brief Construct the full service name, including underscores, from the name, protocol and
 * domain fields.
 *
 * A DNS service record name must have the protocol and name labels begin with underscores. This
 * function validates the input fields and concatenates them to form the full name
 */
std::string buildServiceName(const std::string& name, const std::string& proto,
                             const std::string& domain);

} // namespace Utils
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
