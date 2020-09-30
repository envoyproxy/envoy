#pragma once

#include "envoy/common/platform.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

constexpr uint16_t DNS_RECORD_CLASS_IN = 1;

constexpr uint16_t DNS_RECORD_TYPE_A = 0x01;
constexpr uint16_t DNS_RECORD_TYPE_AAAA = 0x1C;
constexpr uint16_t DNS_RECORD_TYPE_SRV = 0x21;
constexpr uint16_t DNS_RECORD_TYPE_OPT = 0x29;

constexpr uint16_t DNS_RESPONSE_CODE_NO_ERROR = 0;
constexpr uint16_t DNS_RESPONSE_CODE_FORMAT_ERROR = 1;
constexpr uint16_t DNS_RESPONSE_CODE_NAME_ERROR = 3;
constexpr uint16_t DNS_RESPONSE_CODE_NOT_IMPLEMENTED = 4;

constexpr size_t MIN_QUERY_NAME_LENGTH = 3;
constexpr size_t MAX_LABEL_LENGTH = 63;
constexpr size_t MAX_NAME_LENGTH = 255;

// Amazon Route53 will return up to 8 records in an answer
// https://aws.amazon.com/route53/faqs/#associate_multiple_ip_with_single_record
constexpr size_t MAX_RETURNED_RECORDS = 8;

// Ensure that responses stay below the 512 byte byte limit. If we are to exceed this we must
// add DNS extension fields
//
// Note:  There is Network::MAX_UDP_PACKET_SIZE, which is defined as 1500 bytes. If we support
// DNS extensions, which support up to 4096 bytes, we will have to keep this 1500 byte limit
// in mind.
constexpr uint64_t MAX_DNS_RESPONSE_SIZE = 512;

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
