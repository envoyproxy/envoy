#pragma once

#include "extensions/filters/udp/dns_filter/dns_filter.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {
namespace Utils {

static constexpr uint64_t MAX_UDP_DNS_SIZE{512};

std::string buildQueryFromBytes(const char* bytes, const size_t count);
std::string buildQueryForDomain(const std::string& name, uint16_t rec_type, uint16_t rec_class);
void verifyAddress(const std::list<std::string>& addresses, const DnsAnswerRecordPtr& answer);
size_t getResponseQueryCount(DnsMessageParser& parser);

} // namespace Utils
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
