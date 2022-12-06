#include "source/common/common/logger.h"
#include "source/extensions/filters/udp/dns_filter/dns_filter.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
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

  static NiceMock<Random::MockRandomGenerator> random;
  static NiceMock<Stats::MockHistogram> histogram;
  histogram.unit_ = Stats::Histogram::Unit::Milliseconds;
  static Api::ApiPtr api = Api::createApiForTest();
  static NiceMock<Stats::MockCounter> mock_query_buffer_underflow;
  static NiceMock<Stats::MockCounter> mock_record_name_overflow;
  static NiceMock<Stats::MockCounter> query_parsing_failure;
  static NiceMock<Stats::MockCounter> queries_with_additional_rrs;
  static NiceMock<Stats::MockCounter> queries_with_ans_or_authority_rrs;
  static DnsParserCounters counters(mock_query_buffer_underflow, mock_record_name_overflow,
                                    query_parsing_failure, queries_with_additional_rrs,
                                    queries_with_ans_or_authority_rrs);

  FuzzedDataProvider data_provider(buf, len);
  Buffer::InstancePtr query_buffer = std::make_unique<Buffer::OwnedImpl>();

  while (data_provider.remaining_bytes()) {
    const std::string query = data_provider.ConsumeRandomLengthString(1024);
    query_buffer->add(query.data(), query.size());

    const uint16_t retry_count = data_provider.ConsumeIntegralInRange<uint16_t>(0, 3);
    DnsMessageParser message_parser(true, api->timeSource(), retry_count, random, histogram);

    DnsQueryContextPtr query_context =
        std::make_unique<DnsQueryContext>(local, peer, counters, retry_count);
    bool result = message_parser.parseDnsObject(query_context, query_buffer);
    UNREFERENCED_PARAMETER(result);

    query_buffer->drain(query_buffer->length());
  }
}
} // namespace
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
