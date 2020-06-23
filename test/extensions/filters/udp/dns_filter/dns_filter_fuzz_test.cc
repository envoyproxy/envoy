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
  static const auto local = Network::Utility::parseInternetAddressAndPort("127.0.2.1:5353");
  static const auto peer = Network::Utility::parseInternetAddressAndPort("127.0.2.1:55088");

  static NiceMock<Runtime::MockRandomGenerator> random;
  static NiceMock<Stats::MockHistogram> histogram;
  histogram.unit_ = Stats::Histogram::Unit::Milliseconds;
  static Api::ApiPtr api = Api::createApiForTest();
  static NiceMock<Stats::MockCounter> mock_query_buffer_underflow;
  static NiceMock<Stats::MockCounter> mock_record_name_overflow;
  static NiceMock<Stats::MockCounter> query_parsing_failure;
  static DnsParserCounters counters(mock_query_buffer_underflow, mock_record_name_overflow,
                                    query_parsing_failure);

  FuzzedDataProvider data_provider(buf, len);
  Buffer::InstancePtr query_buffer = std::make_unique<Buffer::OwnedImpl>();

  while (data_provider.remaining_bytes()) {
    const std::string query = data_provider.ConsumeRandomLengthString(1024);
    query_buffer->add(query.data(), query.size());

    const uint16_t retry_count = data_provider.ConsumeIntegralInRange<uint16_t>(0, 3);
    DnsMessageParser message_parser(true, api->timeSource(), retry_count, random, histogram);
    uint64_t offset = data_provider.ConsumeIntegralInRange<uint64_t>(0, query.size());

    const uint8_t fuzz_function = data_provider.ConsumeIntegralInRange<uint8_t>(0, 2);
    switch (fuzz_function) {
    case 0: {
      DnsQueryContextPtr query_context =
          std::make_unique<DnsQueryContext>(local, peer, counters, retry_count);
      bool result = message_parser.parseDnsObject(query_context, query_buffer);
      UNREFERENCED_PARAMETER(result);
    } break;

    case 1: {
      DnsQueryRecordPtr ptr = message_parser.parseDnsQueryRecord(query_buffer, &offset);
      UNREFERENCED_PARAMETER(ptr);
    } break;

    case 2: {
      DnsAnswerRecordPtr ptr = message_parser.parseDnsAnswerRecord(query_buffer, &offset);
      UNREFERENCED_PARAMETER(ptr);
    } break;
    } // end case
    query_buffer->drain(query_buffer->length());
  }
}
} // namespace
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
