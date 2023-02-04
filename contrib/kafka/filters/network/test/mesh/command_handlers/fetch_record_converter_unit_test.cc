#include <set>

#include "test/test_common/utility.h"

#include "contrib/kafka/filters/network/source/mesh/command_handlers/fetch_record_converter.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {
namespace {

TEST(FetchRecordConverterImpl, shouldProcessEmptyInput) {
  // given
  const FetchRecordConverter& testee = FetchRecordConverterImpl{};
  const InboundRecordsMap input = {};

  // when
  const auto result = testee.convert(input);

  // then
  ASSERT_EQ(result.size(), 0);
}

TEST(FetchRecordConverterImpl, shouldProcessInputWithNoRecords) {
  // given
  const FetchRecordConverter& testee = FetchRecordConverterImpl{};
  InboundRecordsMap input = {};
  input[{"aaa", 0}] = {};
  input[{"aaa", 1}] = {};
  input[{"aaa", 2}] = {};
  input[{"bbb", 0}] = {};

  // when
  const auto result = testee.convert(input);

  // then
  ASSERT_EQ(result.size(), 2); // Number of unique topic names (not partitions).
}

} // namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
