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

const std::string generateQuery(FuzzedDataProvider* data_provider) {
  size_t query_size = data_provider->ConsumeIntegralInRange<size_t>(0, 512);
  return data_provider->ConsumeRandomLengthString(query_size);
}

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  FuzzedDataProvider data_provider(buf, len);
  const bool recurse = data_provider.ConsumeBool();
  const uint16_t retry_count = data_provider.ConsumeIntegralInRange<uint16_t>(0, 3);

  static NiceMock<Runtime::MockRandomGenerator> random;
  DnsMessageParser message_parser(recurse, retry_count, random);

  const auto local = Network::Utility::parseInternetAddressAndPort("127.0.2.1:5353");
  const auto peer = Network::Utility::parseInternetAddressAndPort("127.0.2.1:55088");

  Buffer::InstancePtr query_buffer = std::make_unique<Buffer::OwnedImpl>();
  const std::string query = generateQuery(&data_provider);
  query_buffer->add(query.data(), query.size());

  const uint8_t fuzz_function = data_provider.ConsumeIntegralInRange<uint8_t>(0, 2);
  switch (fuzz_function) {
  case 0: {
    DnsQueryContextPtr query_context = std::make_unique<DnsQueryContext>(local, peer);
    bool result = message_parser.parseDnsObject(query_context, query_buffer);
    UNREFERENCED_PARAMETER(result);
  } break;

  case 1: {
    uint64_t offset = data_provider.ConsumeIntegralInRange<uint64_t>(0, query_buffer->length());
    DnsQueryRecordPtr ptr = message_parser.parseDnsQueryRecord(query_buffer, &offset);
    UNREFERENCED_PARAMETER(ptr);
  } break;

  case 2: {
    uint64_t offset = data_provider.ConsumeIntegralInRange<uint64_t>(0, query_buffer->length());
    DnsAnswerRecordPtr ptr = message_parser.parseDnsAnswerRecord(query_buffer, &offset);
    UNREFERENCED_PARAMETER(ptr);
  } break;
  } // end case
}
} // namespace
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
