#include "common/common/logger.h"

#include "extensions/filters/udp/dns_filter/dns_filter.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {
namespace {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  Buffer::InstancePtr query_buffer = std::make_unique<Buffer::OwnedImpl>();
  query_buffer->add(buf, len);

  DnsMessageParser message_parser(false /* recurse */, 0 /*retry_count */);

  const auto local = Network::Utility::parseInternetAddressAndPort("127.0.2.1:5353");
  const auto peer = Network::Utility::parseInternetAddressAndPort("127.0.2.1:55088");

  DnsQueryContextPtr query_context = std::make_unique<DnsQueryContext>(local, peer);
  bool result = message_parser.parseDnsObject(query_context, query_buffer);
  UNREFERENCED_PARAMETER(result);
}

} // namespace

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
