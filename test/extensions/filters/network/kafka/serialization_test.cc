#include "extensions/filters/network/kafka/serialization.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

// freshly created buffers should not be ready
#define TEST_EmptyBufferShouldNotBeReady(BufferClass)                                              \
  TEST(BufferClass, EmptyBufferShouldNotBeReady) {                                                 \
    const BufferClass testee{};                                                                    \
    ASSERT_EQ(testee.ready(), false);                                                              \
  }

TEST_EmptyBufferShouldNotBeReady(Int8Buffer);
TEST_EmptyBufferShouldNotBeReady(Int16Buffer);
TEST_EmptyBufferShouldNotBeReady(Int32Buffer);
TEST_EmptyBufferShouldNotBeReady(UInt32Buffer);
TEST_EmptyBufferShouldNotBeReady(Int64Buffer);
TEST_EmptyBufferShouldNotBeReady(BoolBuffer);
TEST_EmptyBufferShouldNotBeReady(StringBuffer);
TEST_EmptyBufferShouldNotBeReady(NullableStringBuffer);
TEST(CompositeBuffer, EmptyBufferShouldNotBeReady) {
  // given
  const CompositeBuffer<int8_t, Int8Buffer> testee{};
  // when, then
  ASSERT_EQ(testee.ready(), false);
}
TEST(ArrayBuffer, EmptyBufferShouldNotBeReady) {
  // given
  const ArrayBuffer<int8_t, Int8Buffer> testee{};
  // when, then
  ASSERT_EQ(testee.ready(), false);
}

// Null buffer is a special case, it's always ready and can provide results via 0-arg ctor
TEST(NullBuffer, EmptyBufferShouldBeReady) {
  // given
  const NullBuffer<int8_t> testee{};
  // when, then
  ASSERT_EQ(testee.ready(), true);
  ASSERT_EQ(testee.get(), 0);
}

EncodingContext encoder{-1}; // context is not used when serializing primitive types

const char* getRawData(const Buffer::OwnedImpl& buffer) {
  uint64_t num_slices = buffer.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  buffer.getRawSlices(slices, num_slices);
  return reinterpret_cast<const char*>((slices[0]).mem_);
}

// exactly what is says on the tin:
// 1. serialize expected using Encoder
// 2. deserialize byte array using testee buffer
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
// this verifies if buffer keeps state properly
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

template <typename BT, typename AT> void serializeThenDeserializeAndCheckEquality(AT expected) {
  serializeThenDeserializeAndCheckEqualityInOneGo<BT>(expected);
  serializeThenDeserializeAndCheckEqualityWithChunks<BT>(expected);
}

// macroed out test for numeric buffers
#define TEST_BufferShouldDeserialize(BufferClass, DataClass, Value)                                \
  TEST(DataClass, ShouldConsumeCorrectAmountOfData) {                                              \
    /* given */                                                                                    \
    const DataClass value = Value;                                                                 \
    serializeThenDeserializeAndCheckEquality<BufferClass>(value);                                  \
  }

TEST_BufferShouldDeserialize(Int8Buffer, int8_t, 42);
TEST_BufferShouldDeserialize(Int16Buffer, int16_t, 42);
TEST_BufferShouldDeserialize(Int32Buffer, int32_t, 42);
TEST_BufferShouldDeserialize(UInt32Buffer, uint32_t, 42);
TEST_BufferShouldDeserialize(Int64Buffer, int64_t, 42);
TEST_BufferShouldDeserialize(BoolBuffer, bool, true);

TEST(StringBuffer, ShouldDeserialize) {
  const std::string value = "sometext";
  serializeThenDeserializeAndCheckEquality<StringBuffer>(value);
}

TEST(StringBuffer, ShouldDeserializeEmptyString) {
  const std::string value = "";
  serializeThenDeserializeAndCheckEquality<StringBuffer>(value);
}

TEST(StringBuffer, ShouldThrowOnInvalidLength) {
  // given
  StringBuffer testee;
  Buffer::OwnedImpl buffer;

  int16_t len = -1; // STRING accepts only >= 0
  encoder.encode(len, buffer);

  uint64_t remaining = 1024;
  const char* data = getRawData(buffer);

  // when
  // then
  EXPECT_THROW(testee.feed(data, remaining), EnvoyException);
}

TEST(NullableStringBuffer, ShouldDeserializeString) {
  // given
  const NullableString value{"sometext"};
  serializeThenDeserializeAndCheckEquality<NullableStringBuffer>(value);
}

TEST(NullableStringBuffer, ShouldDeserializeEmptyString) {
  // given
  const NullableString value{""};
  serializeThenDeserializeAndCheckEquality<NullableStringBuffer>(value);
}

TEST(NullableStringBuffer, ShouldDeserializeAbsentString) {
  // given
  const NullableString value = absl::nullopt;
  serializeThenDeserializeAndCheckEquality<NullableStringBuffer>(value);
}

TEST(NullableStringBuffer, ShouldThrowOnInvalidLength) {
  // given
  NullableStringBuffer testee;
  Buffer::OwnedImpl buffer;

  int16_t len = -2; // -1 is OK for NULLABLE_STRING
  encoder.encode(len, buffer);

  uint64_t remaining = 1024;
  const char* data = getRawData(buffer);

  // when
  // then
  EXPECT_THROW(testee.feed(data, remaining), EnvoyException);
}

TEST(ArrayBuffer, ShouldConsumeCorrectAmountOfData) {
  const NullableArray<std::string> value{{"aaa", "bbbbb", "cc", "d", "e", "ffffffff"}};
  serializeThenDeserializeAndCheckEquality<ArrayBuffer<std::string, StringBuffer>>(value);
}

TEST(ArrayBuffer, ShouldThrowOnInvalidLength) {
  // given
  ArrayBuffer<std::string, StringBuffer> testee;
  Buffer::OwnedImpl buffer;

  const int32_t len = -2; // -1 is OK for ARRAY
  encoder.encode(len, buffer);

  uint64_t remaining = 1024;
  const char* data = getRawData(buffer);

  // when
  // then
  EXPECT_THROW(testee.feed(data, remaining), EnvoyException);
}

struct CompositeBufferResult {
  std::string field1_;
  NullableArray<int32_t> field2_;
  int16_t field3_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(field1_, dst);
    written += encoder.encode(field2_, dst);
    written += encoder.encode(field3_, dst);
    return written;
  }
};

bool operator==(const CompositeBufferResult& lhs, const CompositeBufferResult& rhs) {
  return (lhs.field1_ == rhs.field1_) && (lhs.field2_ == rhs.field2_) &&
         (lhs.field3_ == rhs.field3_);
}

typedef CompositeBuffer<CompositeBufferResult, StringBuffer, ArrayBuffer<int32_t, Int32Buffer>,
                        Int16Buffer>
    TestCompositeBuffer;

TEST(CompositeBuffer, ShouldDeserialize) {
  const CompositeBufferResult expected{"zzzzz", {{10, 20, 30, 40, 50}}, 1234};
  serializeThenDeserializeAndCheckEquality<TestCompositeBuffer>(expected);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
