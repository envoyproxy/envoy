#pragma once

#include "envoy/formatter/substitution_formatter_base.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

/**
 * Creates a DNS filter-specific command parser for access logging.
 * Supports custom format commands for DNS-specific attributes:
 * - QUERY_NAME: The DNS query name being resolved
 * - QUERY_TYPE: The DNS query type (A, AAAA, SRV, etc.)
 * - QUERY_CLASS: The DNS query class
 * - ANSWER_COUNT: Number of answers in the response
 * - RESPONSE_CODE: DNS response code
 * - PARSE_STATUS: Whether the query was successfully parsed
 *
 * @return CommandParserPtr DNS filter command parser
 */
Formatter::CommandParserPtr createDnsFilterCommandParser();

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
