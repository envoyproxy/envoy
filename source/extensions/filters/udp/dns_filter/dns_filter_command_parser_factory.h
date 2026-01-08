#pragma once

#include "envoy/formatter/substitution_formatter_base.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

class DnsFilterCommandParserFactory : public Formatter::BuiltInCommandParserFactory {
public:
  DnsFilterCommandParserFactory() = default;

  std::string name() const override;
  Formatter::CommandParserPtr createCommandParser() const override;
};

DECLARE_FACTORY(DnsFilterCommandParserFactory);

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
