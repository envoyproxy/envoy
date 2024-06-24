#include <algorithm>
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
  const std::vector<FetchableTopicResponse> result = testee.convert(input);

  // then
  ASSERT_EQ(result.size(), 2); // Number of unique topic names (not partitions).
  const auto topic1 =
      std::find_if(result.begin(), result.end(), [](auto x) { return "aaa" == x.topic_; });
  ASSERT_EQ(topic1->partitions_.size(), 3);
  const auto topic2 =
      std::find_if(result.begin(), result.end(), [](auto x) { return "bbb" == x.topic_; });
  ASSERT_EQ(topic2->partitions_.size(), 1);
}

// Helper method to generate records.
InboundRecordSharedPtr makeRecord() {
  const NullableBytes key = {Bytes(128)};
  const NullableBytes value = {Bytes(1024)};
  return std::make_shared<InboundRecord>("aaa", 0, 0, key, value);
}

TEST(FetchRecordConverterImpl, shouldProcessRecords) {
  // given
  const FetchRecordConverter& testee = FetchRecordConverterImpl{};
  InboundRecordsMap input = {};
  input[{"aaa", 0}] = {makeRecord(), makeRecord(), makeRecord()};

  // when
  const std::vector<FetchableTopicResponse> result = testee.convert(input);

  // then
  ASSERT_EQ(result.size(), 1);
  const auto& partitions = result[0].partitions_;
  ASSERT_EQ(partitions.size(), 1);
  const NullableBytes& data = partitions[0].records_;
  ASSERT_EQ(data.has_value(), true);
  ASSERT_GT(data->size(),
            3 * (128 + 1024)); // Records carry some metadata so it should always pass.

  // then - check whether metadata really says we carry 3 records.
  constexpr auto record_count_offset = 57;
  const auto ptr = reinterpret_cast<const uint32_t*>(data->data() + record_count_offset);
  const uint32_t record_count = be32toh(*ptr);
  ASSERT_EQ(record_count, 3);
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

// Here we check whether our manual implementation really works.
// https://github.com/apache/kafka/blob/3.3.2/clients/src/main/java/org/apache/kafka/common/utils/Crc32C.java
TEST(FetchRecordConverterImpl, shouldComputeCrc32c) {
  constexpr auto testee = &FetchRecordConverterImpl::computeCrc32cForTest;

  std::vector<unsigned char> arg1 = {};
  ASSERT_EQ(testee(arg1.data(), arg1.size()), 0x00000000);

  std::vector<unsigned char> arg2 = {0, 0, 0, 0};
  ASSERT_EQ(testee(arg2.data(), arg2.size()), 0x48674BC7);

  std::vector<unsigned char> arg3 = {13, 42, 13, 42, 13, 42, 13, 42};
  ASSERT_EQ(testee(arg3.data(), arg3.size()), 0xDB56B80F);
}

} // namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
