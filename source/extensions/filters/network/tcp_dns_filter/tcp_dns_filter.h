#pragma once

#include "envoy/network/filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/udp/dns_filter/dns_filter.h"
#include "source/extensions/filters/udp/dns_filter/dns_parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpDnsFilter {

using UdpFilters::DnsFilter::DnsFilterEnvoyConfig;
using UdpFilters::DnsFilter::DnsFilterEnvoyConfigSharedPtr;
using UdpFilters::DnsFilter::DnsMessageParser;
using UdpFilters::DnsFilter::DnsParserCounters;
using UdpFilters::DnsFilter::DnsQueryContext;
using UdpFilters::DnsFilter::DnsQueryContextPtr;
using UdpFilters::DnsFilter::DnsQueryRecord;
using UdpFilters::DnsFilter::DnsVirtualDomainConfigSharedPtr;

/**
 * TCP DNS filter that handles DNS queries over TCP connections.
 * Reuses the UDP DNS filter's parsing and resolution logic.
 * TCP DNS framing uses a 2-byte big-endian length prefix per RFC 1035 §4.2.2.
 */
class TcpDnsFilter : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  TcpDnsFilter(const DnsFilterEnvoyConfigSharedPtr& config);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  void processDnsQuery(Buffer::OwnedImpl& dns_buffer);
  bool resolveQuery(DnsQueryContextPtr& context, const DnsQueryRecord& query,
                    DnsMessageParser& parser);

  const DnsFilterEnvoyConfigSharedPtr config_;
  Network::ReadFilterCallbacks* read_callbacks_{};
  Buffer::OwnedImpl buffer_;
};

} // namespace TcpDnsFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
