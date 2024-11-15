#include <set>

#include "test/test_common/utility.h"

#include "contrib/kafka/filters/network/source/external/requests.h"
#include "contrib/kafka/filters/network/source/mesh/command_handlers/produce_record_extractor.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {
namespace {

// Simple matcher that verifies that the input given is a collection containing correct number of
// unique (!) records for given topic-partition pairs.
MATCHER_P3(HasRecords, topic, partition, expected, "") {
  size_t expected_count = expected;
  std::set<absl::string_view> saved_key_pointers = {};
  std::set<absl::string_view> saved_value_pointers = {};
  size_t count = 0;

  for (const auto& record : arg) {
    if (record.topic_ == topic && record.partition_ == partition) {
      saved_key_pointers.insert(record.key_);
      saved_value_pointers.insert(record.value_);
      ++count;
    }
  }

  if (expected_count != count) {
    return false;
  }
  if (expected_count != saved_key_pointers.size()) {
    return false;
  }
  return saved_key_pointers.size() == saved_value_pointers.size();
}

// Helper function to create a record batch that contains a single record with 5-byte key and 5-byte
// value.
Bytes makeGoodRecordBatch() {
  // Record batch bytes get ignored (apart from magic field), so we can put 0 there.
  Bytes result = Bytes(16 + 1 + 44);
  result[16] = 2; // Record batch magic value.
  Bytes real_data = {/* Length = 36 */ 72,
                     /* Attributes */ 0,
                     /* Timestamp delta */ 0,
                     /* Offset delta */ 0,
                     /* Key length = 5 */ 10,
                     107,
                     107,
                     107,
                     107,
                     107,
                     /* Value length = 5 */ 10,
                     118,
                     118,
                     118,
                     118,
                     118,
                     /* Headers count = 2 */ 4,
                     /* Header key length = 3 */ 6,
                     49,
                     49,
                     49,
                     /* Header value length = 5 */ 10,
                     97,
                     97,
                     97,
                     97,
                     97,
                     /* Header key length = 3 */ 6,
                     50,
                     50,
                     50,
                     /* Header value length = 5 */ 10,
                     98,
                     98,
                     98,
                     98,
                     98};
  result.insert(result.end(), real_data.begin(), real_data.end());
  return result;
}

TEST(RecordExtractorImpl, shouldProcessRecordBytes) {
  // given
  const RecordExtractorImpl testee;

  const PartitionProduceData t1_ppd1 = {0, makeGoodRecordBatch()};
  const PartitionProduceData t1_ppd2 = {1, makeGoodRecordBatch()};
  const PartitionProduceData t1_ppd3 = {2, makeGoodRecordBatch()};
  const TopicProduceData tpd1 = {"topic1", {t1_ppd1, t1_ppd2, t1_ppd3}};

  // Weird input from client, protocol allows sending null value as bytes array.
  const PartitionProduceData t2_ppd = {20, absl::nullopt};
  const TopicProduceData tpd2 = {"topic2", {t2_ppd}};

  const std::vector<TopicProduceData> input = {tpd1, tpd2};

  // when
  const auto result = testee.extractRecords(input);

  // then
  EXPECT_THAT(result, HasRecords("topic1", 0, 1));
  EXPECT_THAT(result, HasRecords("topic1", 1, 1));
  EXPECT_THAT(result, HasRecords("topic1", 2, 1));
  EXPECT_THAT(result, HasRecords("topic2", 20, 0));
}

/**
 * Helper function to make record batch (batch contains 1+ records).
 * We use 'stage' parameter to make it a single function with various failure modes.
 */
const std::vector<TopicProduceData> makeTopicProduceData(const unsigned int stage) {
  Bytes bytes = makeGoodRecordBatch();
  if (1 == stage) {
    // No common fields before magic.
    bytes.erase(bytes.begin(), bytes.end());
  }
  if (2 == stage) {
    // No magic.
    bytes.erase(bytes.begin() + 16, bytes.end());
  }
  if (3 == stage) {
    // Bad magic.
    bytes[16] = 42;
  }
  if (4 == stage) {
    // No common fields after magic.
    bytes.erase(bytes.begin() + 17, bytes.end());
  }
  if (5 == stage) {
    // No record length after common fields.
    bytes[61] = 128; // This will force variable-length deserializer to wait for more bytes.
    bytes.erase(bytes.begin() + 62, bytes.end());
  }
  if (6 == stage) {
    // Record length is higher than size of real data.
    bytes.erase(bytes.begin() + 62, bytes.end());
  }
  if (7 == stage) {
    // Attributes field has negative length.
    bytes[61] = 3; /* -1 */
    bytes.erase(bytes.begin() + 62, bytes.end());
  }
  if (8 == stage) {
    // Attributes field is missing - length is valid, but there is no more data to read.
    bytes[61] = 0;
    bytes.erase(bytes.begin() + 62, bytes.end());
  }
  if (9 == stage) {
    // Header count not present - we are going to drop all 21 header bytes after value.
    bytes[61] = (36 - 21) << 1; // Length is encoded as variable length.
    bytes.erase(bytes.begin() + 77, bytes.end());
  }
  if (10 == stage) {
    // Negative variable length integer for header count.
    bytes[77] = 17;
  }
  if (11 == stage) {
    // Last header value is going to be shorter, so there will be one unconsumed byte.
    bytes[92] = 8;
  }
  const PartitionProduceData ppd = {0, bytes};
  const TopicProduceData tpd = {"topic", {ppd}};
  return {tpd};
}

TEST(RecordExtractorImpl, shouldHandleInvalidRecordBytes) {
  const RecordExtractorImpl testee;
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(1)), EnvoyException,
                          "no common fields");
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(2)), EnvoyException,
                          "magic byte is not present");
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(3)), EnvoyException,
                          "unknown magic value");
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(4)), EnvoyException,
                          "no attribute fields");
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(5)), EnvoyException,
                          "no length");
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(6)), EnvoyException,
                          "not enough bytes provided");
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(7)), EnvoyException,
                          "has invalid length");
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(8)), EnvoyException,
                          "attributes not present");
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(9)), EnvoyException,
                          "header count not present");
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(10)), EnvoyException,
                          "invalid header count");
  EXPECT_THROW_WITH_REGEX(testee.extractRecords(makeTopicProduceData(11)), EnvoyException,
                          "data left after consuming record");
}

// Minor helper function.
absl::string_view bytesToStringView(const Bytes& bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

TEST(RecordExtractorImpl, shouldExtractByteArray) {
  {
    const Bytes noBytes = Bytes(0);
    auto arg = bytesToStringView(noBytes);
    EXPECT_THROW_WITH_REGEX(RecordExtractorImpl::extractByteArray(arg), EnvoyException,
                            "byte array length not present");
  }
  {
    const Bytes nullValueBytes = {0b00000001}; // Length = -1.
    auto arg = bytesToStringView(nullValueBytes);
    EXPECT_EQ(RecordExtractorImpl::extractByteArray(arg), absl::string_view());
  }
  {
    const Bytes negativeLengthBytes = {0b01111111}; // Length = -64.
    auto arg = bytesToStringView(negativeLengthBytes);
    EXPECT_THROW_WITH_REGEX(RecordExtractorImpl::extractByteArray(arg), EnvoyException,
                            "byte array length less than -1: -64");
  }
  {
    const Bytes bigLengthBytes = {0b01111110}; // Length = 63.
    auto arg = bytesToStringView(bigLengthBytes);
    EXPECT_THROW_WITH_REGEX(RecordExtractorImpl::extractByteArray(arg), EnvoyException,
                            "byte array length larger than data provided: 63 vs 0");
  }
  {
    // Length = 4, 7 bytes follow, 4 should be consumed, 13s should stay unconsumed.
    const Bytes goodBytes = {0b00001000, 42, 42, 42, 42, 13, 13, 13};
    auto arg = bytesToStringView(goodBytes);
    EXPECT_EQ(RecordExtractorImpl::extractByteArray(arg),
              absl::string_view(reinterpret_cast<const char*>(goodBytes.data() + 1), 4));
    EXPECT_EQ(arg.data(), reinterpret_cast<const char*>(goodBytes.data() + 5));
    EXPECT_EQ(arg.size(), 3);
  }
}

} // namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
