#include "extensions/filters/network/kafka/tagged_fields.h"

#include "test/extensions/filters/network/kafka/serialization_utilities.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace SerializationTest {

/**
 * Tests in this file are supposed to check whether serialization operations
 * on Kafka-primitive types (ints, strings, arrays) are behaving correctly.
 */

// Freshly created deserializers should not be ready.
#define TEST_EmptyDeserializerShouldNotBeReady(DeserializerClass)                                  \
  TEST(DeserializerClass, EmptyBufferShouldNotBeReady) {                                           \
    const DeserializerClass testee{};                                                              \
    ASSERT_EQ(testee.ready(), false);                                                              \
  }

TEST_EmptyDeserializerShouldNotBeReady(Int8Deserializer);
TEST_EmptyDeserializerShouldNotBeReady(Int16Deserializer);
TEST_EmptyDeserializerShouldNotBeReady(Int32Deserializer);
TEST_EmptyDeserializerShouldNotBeReady(UInt32Deserializer);
TEST_EmptyDeserializerShouldNotBeReady(Int64Deserializer);
TEST_EmptyDeserializerShouldNotBeReady(BooleanDeserializer);
TEST_EmptyDeserializerShouldNotBeReady(VarUInt32Deserializer);

TEST_EmptyDeserializerShouldNotBeReady(StringDeserializer);
TEST_EmptyDeserializerShouldNotBeReady(CompactStringDeserializer);
TEST_EmptyDeserializerShouldNotBeReady(NullableStringDeserializer);
TEST_EmptyDeserializerShouldNotBeReady(NullableCompactStringDeserializer);
TEST_EmptyDeserializerShouldNotBeReady(BytesDeserializer);
TEST_EmptyDeserializerShouldNotBeReady(CompactBytesDeserializer);
TEST_EmptyDeserializerShouldNotBeReady(NullableBytesDeserializer);

TEST(ArrayDeserializer, EmptyBufferShouldNotBeReady) {
  // given
  const ArrayDeserializer<Int8Deserializer> testee{};
  // when, then
  ASSERT_EQ(testee.ready(), false);
}

TEST(CompactArrayDeserializer, EmptyBufferShouldNotBeReady) {
  // given
  const CompactArrayDeserializer<Int32Deserializer> testee{};
  // when, then
  ASSERT_EQ(testee.ready(), false);
}

TEST(NullableArrayDeserializer, EmptyBufferShouldNotBeReady) {
  // given
  const NullableArrayDeserializer<Int8Deserializer> testee{};
  // when, then
  ASSERT_EQ(testee.ready(), false);
}

TEST(NullableCompactArrayDeserializer, EmptyBufferShouldNotBeReady) {
  // given
  const NullableCompactArrayDeserializer<Int32Deserializer> testee{};
  // when, then
  ASSERT_EQ(testee.ready(), false);
}

// Extracted test for numeric buffers.
#define TEST_DeserializerShouldDeserialize(BufferClass, DataClass, Value)                          \
  TEST(DataClass, ShouldConsumeCorrectAmountOfData) {                                              \
    /* given */                                                                                    \
    const DataClass value = Value;                                                                 \
    serializeThenDeserializeAndCheckEquality<BufferClass>(value);                                  \
  }

TEST_DeserializerShouldDeserialize(Int8Deserializer, int8_t, 42);
TEST_DeserializerShouldDeserialize(Int16Deserializer, int16_t, 42);
TEST_DeserializerShouldDeserialize(Int32Deserializer, int32_t, 42);
TEST_DeserializerShouldDeserialize(UInt32Deserializer, uint32_t, 42);
TEST_DeserializerShouldDeserialize(Int64Deserializer, int64_t, 42);
TEST_DeserializerShouldDeserialize(BooleanDeserializer, bool, true);

EncodingContext encoder{-1}; // Provided api_version does not matter for primitive types.

// Variable-length uint32_t tests.

TEST(VarUInt32Deserializer, ShouldDeserialize) {
  const uint32_t value = 0;
  serializeCompactThenDeserializeAndCheckEquality<VarUInt32Deserializer>(value);
}

TEST(VarUInt32Deserializer, ShouldDeserializeMaxUint32) {
  const uint32_t value = std::numeric_limits<uint32_t>::max();
  serializeCompactThenDeserializeAndCheckEquality<VarUInt32Deserializer>(value);
}

TEST(VarUInt32Deserializer, ShouldDeserializeEdgeValues) {
  // Each of these values should fit in 1, 2, 3, 4 bytes.
  std::vector<uint32_t> values = {0x7f, 0x3fff, 0x1fffff, 0xfffffff};
  for (auto i = 0; i < static_cast<int>(values.size()); ++i) {
    // given
    Buffer::OwnedImpl buffer;

    // when
    const uint32_t written = encoder.encodeCompact(values[i], buffer);

    // then
    ASSERT_EQ(written, i + 1);
    absl::string_view data = {getRawData(buffer), 1024};
    // All bits in lower bytes need to be set.
    for (auto j = 0; j + 1 < i; ++j) {
      ASSERT_EQ(static_cast<uint8_t>(data[j]), 0xFF);
    }
    // Highest bit in last byte needs to be clear (end marker).
    ASSERT_EQ(static_cast<uint8_t>(data[i]), 0x7F);
  }
}

TEST(VarUInt32Deserializer, ShouldSerializeMaxUint32Properly) {
  // given
  Buffer::OwnedImpl buffer;

  // when
  const uint32_t value = std::numeric_limits<uint32_t>::max();
  const uint32_t result = encoder.encodeCompact(value, buffer);

  // then
  ASSERT_EQ(result, 5);
  absl::string_view data = {getRawData(buffer), 1024};
  ASSERT_EQ(static_cast<uint8_t>(data[0]), 0xFF); // Bits 1-7 (starting at 1).
  ASSERT_EQ(static_cast<uint8_t>(data[1]), 0xFF); // Bits 8-14.
  ASSERT_EQ(static_cast<uint8_t>(data[2]), 0xFF); // Bits 15-21.
  ASSERT_EQ(static_cast<uint8_t>(data[3]), 0xFF); // Bits 22-28.
  ASSERT_EQ(static_cast<uint8_t>(data[4]), 0x0F); // Bits 29-32.
}

TEST(VarUInt32Deserializer, ShouldThrowIfNoEndWith5Bytes) {
  // given
  VarUInt32Deserializer testee;
  Buffer::OwnedImpl buffer;

  // The buffer makes no sense, it's 5 times 0xFF, while varint encoding ensures that in the worst
  // case 5th byte has the highest bit clear.
  for (int i = 0; i < 5; ++i) {
    const uint8_t all_bits_set = 0xFF;
    buffer.add(&all_bits_set, sizeof(all_bits_set));
  }

  absl::string_view data = {getRawData(buffer), 1024};

  // when
  // then
  EXPECT_THROW(testee.feed(data), EnvoyException);
}

// String tests.

TEST(StringDeserializer, ShouldDeserialize) {
  const std::string value = "sometext";
  serializeThenDeserializeAndCheckEquality<StringDeserializer>(value);
}

TEST(StringDeserializer, ShouldDeserializeEmptyString) {
  const std::string value = "";
  serializeThenDeserializeAndCheckEquality<StringDeserializer>(value);
}

TEST(StringDeserializer, ShouldThrowOnInvalidLength) {
  // given
  StringDeserializer testee;
  Buffer::OwnedImpl buffer;

  int16_t len = -1; // STRING accepts length >= 0.
  encoder.encode(len, buffer);

  absl::string_view data = {getRawData(buffer), 1024};

  // when
  // then
  EXPECT_THROW(testee.feed(data), EnvoyException);
}

// Compact string tests.

TEST(CompactStringDeserializer, ShouldDeserialize) {
  const std::string value = "sometext";
  serializeCompactThenDeserializeAndCheckEquality<CompactStringDeserializer>(value);
}

TEST(CompactStringDeserializer, ShouldDeserializeEmptyString) {
  const std::string value = "";
  serializeCompactThenDeserializeAndCheckEquality<CompactStringDeserializer>(value);
}

TEST(CompactStringDeserializer, ShouldThrowOnInvalidLength) {
  // given
  CompactStringDeserializer testee;
  Buffer::OwnedImpl buffer;

  const uint32_t len = 0; // COMPACT_STRING requires length >= 1.
  encoder.encodeCompact(len, buffer);

  absl::string_view data = {getRawData(buffer), 1024};

  // when
  // then
  EXPECT_THROW(testee.feed(data), EnvoyException);
}

// Nullable string tests.

TEST(NullableStringDeserializer, ShouldDeserializeString) {
  // given
  const NullableString value{"sometext"};
  serializeThenDeserializeAndCheckEquality<NullableStringDeserializer>(value);
}

TEST(NullableStringDeserializer, ShouldDeserializeEmptyString) {
  // given
  const NullableString value{""};
  serializeThenDeserializeAndCheckEquality<NullableStringDeserializer>(value);
}

TEST(NullableStringDeserializer, ShouldDeserializeAbsentString) {
  // given
  const NullableString value = absl::nullopt;
  serializeThenDeserializeAndCheckEquality<NullableStringDeserializer>(value);
}

TEST(NullableStringDeserializer, ShouldThrowOnInvalidLength) {
  // given
  NullableStringDeserializer testee;
  Buffer::OwnedImpl buffer;

  int16_t len = -2; // -1 is OK for NULLABLE_STRING.
  encoder.encode(len, buffer);

  absl::string_view data = {getRawData(buffer), 1024};

  // when
  // then
  EXPECT_THROW(testee.feed(data), EnvoyException);
}

// Nullable compact string tests.

TEST(NullableCompactStringDeserializer, ShouldDeserializeString) {
  // given
  const NullableString value{"sometext"};
  serializeCompactThenDeserializeAndCheckEquality<NullableCompactStringDeserializer>(value);
}

TEST(NullableCompactStringDeserializer, ShouldDeserializeEmptyString) {
  // given
  const NullableString value{""};
  serializeCompactThenDeserializeAndCheckEquality<NullableCompactStringDeserializer>(value);
}

TEST(NullableCompactStringDeserializer, ShouldDeserializeAbsentString) {
  // given
  const NullableString value = absl::nullopt;
  serializeCompactThenDeserializeAndCheckEquality<NullableCompactStringDeserializer>(value);
}

// Byte array tests.

TEST(BytesDeserializer, ShouldDeserialize) {
  const Bytes value{'a', 'b', 'c', 'd'};
  serializeThenDeserializeAndCheckEquality<BytesDeserializer>(value);
}

TEST(BytesDeserializer, ShouldDeserializeEmptyBytes) {
  const Bytes value{};
  serializeThenDeserializeAndCheckEquality<BytesDeserializer>(value);
}

TEST(BytesDeserializer, ShouldThrowOnInvalidLength) {
  // given
  BytesDeserializer testee;
  Buffer::OwnedImpl buffer;

  const int32_t bytes_length = -1; // BYTES accepts length >= 0.
  encoder.encode(bytes_length, buffer);

  absl::string_view data = {getRawData(buffer), 1024};

  // when
  // then
  EXPECT_THROW(testee.feed(data), EnvoyException);
}

// Compact byte array tests.

TEST(CompactBytesDeserializer, ShouldDeserialize) {
  const Bytes value{'a', 'b', 'c', 'd'};
  serializeCompactThenDeserializeAndCheckEquality<CompactBytesDeserializer>(value);
}

TEST(CompactBytesDeserializer, ShouldDeserializeEmptyBytes) {
  const Bytes value{};
  serializeCompactThenDeserializeAndCheckEquality<CompactBytesDeserializer>(value);
}

TEST(CompactBytesDeserializer, ShouldThrowOnInvalidLength) {
  // given
  CompactBytesDeserializer testee;
  Buffer::OwnedImpl buffer;

  const uint32_t bytes_length = 0; // COMPACT_BYTES requires length >= 1.
  encoder.encodeCompact(bytes_length, buffer);

  absl::string_view data = {getRawData(buffer), 1024};

  // when
  // then
  EXPECT_THROW(testee.feed(data), EnvoyException);
}

// Nullable byte array tests.

TEST(NullableBytesDeserializer, ShouldDeserialize) {
  const NullableBytes value{{'a', 'b', 'c', 'd'}};
  serializeThenDeserializeAndCheckEquality<NullableBytesDeserializer>(value);
}

TEST(NullableBytesDeserializer, ShouldDeserializeEmptyBytes) {
  // gcc refuses to initialize optional with empty vector with value{{}}
  const NullableBytes value = {{}};
  serializeThenDeserializeAndCheckEquality<NullableBytesDeserializer>(value);
}

TEST(NullableBytesDeserializer, ShouldDeserializeNullBytes) {
  const NullableBytes value = absl::nullopt;
  serializeThenDeserializeAndCheckEquality<NullableBytesDeserializer>(value);
}

TEST(NullableBytesDeserializer, ShouldThrowOnInvalidLength) {
  // given
  NullableBytesDeserializer testee;
  Buffer::OwnedImpl buffer;

  const int32_t bytes_length = -2; // -1 is OK for NULLABLE_BYTES.
  encoder.encode(bytes_length, buffer);

  absl::string_view data = {getRawData(buffer), 1024};

  // when
  // then
  EXPECT_THROW(testee.feed(data), EnvoyException);
}

// Generic array tests.

TEST(ArrayDeserializer, ShouldConsumeCorrectAmountOfData) {
  const std::vector<std::string> value{{"aaa", "bbbbb", "cc", "d", "e", "ffffffff"}};
  serializeThenDeserializeAndCheckEquality<ArrayDeserializer<StringDeserializer>>(value);
}

TEST(ArrayDeserializer, ShouldThrowOnInvalidLength) {
  // given
  ArrayDeserializer<StringDeserializer> testee;
  Buffer::OwnedImpl buffer;

  const int32_t len = -1; // ARRAY accepts length >= 0.
  encoder.encode(len, buffer);

  absl::string_view data = {getRawData(buffer), 1024};

  // when
  // then
  EXPECT_THROW(testee.feed(data), EnvoyException);
}

// Compact generic array tests.

TEST(CompactArrayDeserializer, ShouldConsumeCorrectAmountOfData) {
  const std::vector<int32_t> value{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}};
  serializeCompactThenDeserializeAndCheckEquality<CompactArrayDeserializer<Int32Deserializer>>(
      value);
}

TEST(CompactArrayDeserializer, ShouldThrowOnInvalidLength) {
  // given
  CompactArrayDeserializer<Int8Deserializer> testee;
  Buffer::OwnedImpl buffer;

  const uint32_t len = 0; // COMPACT_ARRAY accepts length >= 1.
  encoder.encodeCompact(len, buffer);

  absl::string_view data = {getRawData(buffer), 1024};

  // when
  // then
  EXPECT_THROW(testee.feed(data), EnvoyException);
}

// Generic nullable array tests.

TEST(NullableArrayDeserializer, ShouldConsumeCorrectAmountOfData) {
  const NullableArray<std::string> value{{"aaa", "bbbbb", "cc", "d", "e", "ffffffff"}};
  serializeThenDeserializeAndCheckEquality<NullableArrayDeserializer<StringDeserializer>>(value);
}

TEST(NullableArrayDeserializer, ShouldConsumeNullArray) {
  const NullableArray<std::string> value = absl::nullopt;
  serializeThenDeserializeAndCheckEquality<NullableArrayDeserializer<StringDeserializer>>(value);
}

TEST(NullableArrayDeserializer, ShouldThrowOnInvalidLength) {
  // given
  NullableArrayDeserializer<StringDeserializer> testee;
  Buffer::OwnedImpl buffer;

  const int32_t len = -2; // -1 is OK for NULLABLE_ARRAY.
  encoder.encode(len, buffer);

  absl::string_view data = {getRawData(buffer), 1024};

  // when
  // then
  EXPECT_THROW(testee.feed(data), EnvoyException);
}

// Compact nullable generic array tests.

TEST(NullableCompactArrayDeserializer, ShouldConsumeCorrectAmountOfData) {
  const NullableArray<int32_t> value{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}};
  serializeCompactThenDeserializeAndCheckEquality<
      NullableCompactArrayDeserializer<Int32Deserializer>>(value);
}

TEST(NullableCompactArrayDeserializer, ShouldConsumeNullArray) {
  const NullableArray<int32_t> value = absl::nullopt;
  serializeCompactThenDeserializeAndCheckEquality<
      NullableCompactArrayDeserializer<Int32Deserializer>>(value);
}

// Tagged fields.

TEST(TaggedFieldDeserializer, ShouldConsumeCorrectAmountOfData) {
  const TaggedField value{200, Bytes{1, 2, 3, 4, 5, 6}};
  serializeCompactThenDeserializeAndCheckEquality<TaggedFieldDeserializer>(value);
}

TEST(TaggedFieldsDeserializer, ShouldConsumeCorrectAmountOfData) {
  std::vector<TaggedField> fields;
  for (uint32_t i = 0; i < 200; ++i) {
    const TaggedField tagged_field = {i, Bytes{1, 2, 3, 4}};
    fields.push_back(tagged_field);
  }
  const TaggedFields value{fields};
  serializeCompactThenDeserializeAndCheckEquality<TaggedFieldsDeserializer>(value);
}

} // namespace SerializationTest
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
