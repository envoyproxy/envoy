#include "source/extensions/filters/udp/dns_filter/dns_filter_command_parser_factory.h"

#include "source/extensions/filters/udp/dns_filter/dns_filter_access_log.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

std::string DnsFilterCommandParserFactory::name() const {
  return "envoy.built_in_formatters.dns_filter";
}

Formatter::CommandParserPtr DnsFilterCommandParserFactory::createCommandParser() const {
  return createDnsFilterCommandParser();
}

REGISTER_FACTORY(DnsFilterCommandParserFactory, Formatter::BuiltInCommandParserFactory);

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
