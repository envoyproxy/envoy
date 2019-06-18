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

TEST_EmptyDeserializerShouldNotBeReady(StringDeserializer);
TEST_EmptyDeserializerShouldNotBeReady(NullableStringDeserializer);
TEST_EmptyDeserializerShouldNotBeReady(BytesDeserializer);
TEST_EmptyDeserializerShouldNotBeReady(NullableBytesDeserializer);

TEST(ArrayDeserializer, EmptyBufferShouldNotBeReady) {
  // given
  const ArrayDeserializer<int8_t, Int8Deserializer> testee{};
  // when, then
  ASSERT_EQ(testee.ready(), false);
}

TEST(NullableArrayDeserializer, EmptyBufferShouldNotBeReady) {
  // given
  const NullableArrayDeserializer<int8_t, Int8Deserializer> testee{};
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

TEST(NullableBytesDeserializer, ShouldDeserialize) {
  const NullableBytes value{{'a', 'b', 'c', 'd'}};
  serializeThenDeserializeAndCheckEquality<NullableBytesDeserializer>(value);
}

TEST(NullableBytesDeserializer, ShouldDeserializeEmptyBytes) {
  const NullableBytes value{{}};
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

TEST(ArrayDeserializer, ShouldConsumeCorrectAmountOfData) {
  const std::vector<std::string> value{{"aaa", "bbbbb", "cc", "d", "e", "ffffffff"}};
  serializeThenDeserializeAndCheckEquality<ArrayDeserializer<std::string, StringDeserializer>>(
      value);
}

TEST(ArrayDeserializer, ShouldThrowOnInvalidLength) {
  // given
  ArrayDeserializer<std::string, StringDeserializer> testee;
  Buffer::OwnedImpl buffer;

  const int32_t len = -1; // ARRAY accepts length >= 0.
  encoder.encode(len, buffer);

  absl::string_view data = {getRawData(buffer), 1024};

  // when
  // then
  EXPECT_THROW(testee.feed(data), EnvoyException);
}

TEST(NullableArrayDeserializer, ShouldConsumeCorrectAmountOfData) {
  const NullableArray<std::string> value{{"aaa", "bbbbb", "cc", "d", "e", "ffffffff"}};
  serializeThenDeserializeAndCheckEquality<
      NullableArrayDeserializer<std::string, StringDeserializer>>(value);
}

TEST(NullableArrayDeserializer, ShouldConsumeNullArray) {
  const NullableArray<std::string> value = absl::nullopt;
  serializeThenDeserializeAndCheckEquality<
      NullableArrayDeserializer<std::string, StringDeserializer>>(value);
}

TEST(NullableArrayDeserializer, ShouldThrowOnInvalidLength) {
  // given
  NullableArrayDeserializer<std::string, StringDeserializer> testee;
  Buffer::OwnedImpl buffer;

  const int32_t len = -2; // -1 is OK for NULLABLE_ARRAY.
  encoder.encode(len, buffer);

  absl::string_view data = {getRawData(buffer), 1024};

  // when
  // then
  EXPECT_THROW(testee.feed(data), EnvoyException);
}

} // namespace SerializationTest
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
