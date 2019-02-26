#include "common/common/stack_array.h"

#include "extensions/filters/network/kafka/serialization.h"
#include "extensions/filters/network/kafka/serialization_composite.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Tests in this class are supposed to check whether serialization operations
 * on Kafka-primitive types are behaving correctly
 */

// freshly created deserializers should not be ready
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

EncodingContext encoder{-1}; // api_version does not matter for primitive types

// helper function
const char* getRawData(const Buffer::OwnedImpl& buffer) {
  uint64_t num_slices = buffer.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
  buffer.getRawSlices(slices.begin(), num_slices);
  return reinterpret_cast<const char*>((slices[0]).mem_);
}

// exactly what is says on the tin:
// 1. serialize expected using Encoder
// 2. deserialize byte array using testee deserializer
// 3. verify result = expected
// 4. verify that data pointer moved correct amount
// 5. feed testee more data
// 6. verify that nothing more was consumed
template <typename BT, typename AT>
void serializeThenDeserializeAndCheckEqualityInOneGo(AT expected) {
  // given
  BT testee{};

  Buffer::OwnedImpl buffer;
  const size_t written = encoder.encode(expected, buffer);

  uint64_t remaining =
      10 *
      written; // tell parser that there is more data, it should never consume more than written
  const uint64_t orig_remaining = remaining;
  const char* data = getRawData(buffer);
  const char* orig_data = data;

  // when
  const size_t consumed = testee.feed(data, remaining);

  // then
  ASSERT_EQ(consumed, written);
  ASSERT_EQ(testee.ready(), true);
  ASSERT_EQ(testee.get(), expected);
  ASSERT_EQ(data, orig_data + consumed);
  ASSERT_EQ(remaining, orig_remaining - consumed);

  // when - 2
  const size_t consumed2 = testee.feed(data, remaining);

  // then - 2 (nothing changes)
  ASSERT_EQ(consumed2, 0);
  ASSERT_EQ(data, orig_data + consumed);
  ASSERT_EQ(remaining, orig_remaining - consumed);
}

// does the same thing as the above test,
// but instead of providing whole data at one, it provides it in N one-byte chunks
// this verifies if deserializer keeps state properly (no overwrites etc.)
template <typename BT, typename AT>
void serializeThenDeserializeAndCheckEqualityWithChunks(AT expected) {
  // given
  BT testee{};

  Buffer::OwnedImpl buffer;
  const size_t written = encoder.encode(expected, buffer);

  const char* data = getRawData(buffer);
  const char* orig_data = data;

  // when
  size_t consumed = 0;
  for (size_t i = 0; i < written; ++i) {
    uint64_t data_size = 1;
    consumed += testee.feed(data, data_size);
    ASSERT_EQ(data_size, 0);
  }

  // then
  ASSERT_EQ(consumed, written);
  ASSERT_EQ(testee.ready(), true);
  ASSERT_EQ(testee.get(), expected);
  ASSERT_EQ(data, orig_data + consumed);

  // when - 2
  uint64_t remaining = 1024;
  const size_t consumed2 = testee.feed(data, remaining);

  // then - 2 (nothing changes)
  ASSERT_EQ(consumed2, 0);
  ASSERT_EQ(data, orig_data + consumed);
  ASSERT_EQ(remaining, 1024);
}

// wrapper to run both tests
template <typename BT, typename AT> void serializeThenDeserializeAndCheckEquality(AT expected) {
  serializeThenDeserializeAndCheckEqualityInOneGo<BT>(expected);
  serializeThenDeserializeAndCheckEqualityWithChunks<BT>(expected);
}

// extracted test for numeric buffers
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

  int16_t len = -1; // STRING accepts only >= 0
  encoder.encode(len, buffer);

  uint64_t remaining = 1024;
  const char* data = getRawData(buffer);

  // when
  // then
  EXPECT_THROW(testee.feed(data, remaining), EnvoyException);
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

  int16_t len = -2; // -1 is OK for NULLABLE_STRING
  encoder.encode(len, buffer);

  uint64_t remaining = 1024;
  const char* data = getRawData(buffer);

  // when
  // then
  EXPECT_THROW(testee.feed(data, remaining), EnvoyException);
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

  const int32_t bytes_length = -1; // BYTES accepts only >= 0
  encoder.encode(bytes_length, buffer);

  uint64_t remaining = 1024;
  const char* data = getRawData(buffer);

  // when
  // then
  EXPECT_THROW(testee.feed(data, remaining), EnvoyException);
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

  const int32_t bytes_length = -2; // -1 is OK for NULLABLE_BYTES
  encoder.encode(bytes_length, buffer);

  uint64_t remaining = 1024;
  const char* data = getRawData(buffer);

  // when
  // then
  EXPECT_THROW(testee.feed(data, remaining), EnvoyException);
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

  const int32_t len = -1; // ARRAY accepts only >= 0
  encoder.encode(len, buffer);

  uint64_t remaining = 1024;
  const char* data = getRawData(buffer);

  // when
  // then
  EXPECT_THROW(testee.feed(data, remaining), EnvoyException);
}

TEST(NullableArrayDeserializer, ShouldConsumeCorrectAmountOfData) {
  const NullableArray<std::string> value{{"aaa", "bbbbb", "cc", "d", "e", "ffffffff"}};
  serializeThenDeserializeAndCheckEquality<
      NullableArrayDeserializer<std::string, StringDeserializer>>(value);
}

TEST(NullableArrayDeserializer, ShouldThrowOnInvalidLength) {
  // given
  NullableArrayDeserializer<std::string, StringDeserializer> testee;
  Buffer::OwnedImpl buffer;

  const int32_t len = -2; // -1 is OK for ARRAY
  encoder.encode(len, buffer);

  uint64_t remaining = 1024;
  const char* data = getRawData(buffer);

  // when
  // then
  EXPECT_THROW(testee.feed(data, remaining), EnvoyException);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
