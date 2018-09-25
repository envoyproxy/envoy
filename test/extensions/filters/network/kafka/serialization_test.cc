#include "extensions/filters/network/kafka/serialization.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

// === EMPTY (FRESHLY INITIALIZED) BUFFER TESTS ================================

// freshly created buffers should not be ready
#define TEST_EmptyBufferShouldNotBeReady(BufferClass) \
TEST(BufferClass, EmptyBufferShouldNotBeReady) {                               \
  const BufferClass testee{};                                                  \
  ASSERT_EQ(testee.ready(), false);                                            \
}

TEST_EmptyBufferShouldNotBeReady(Int8Buffer);
TEST_EmptyBufferShouldNotBeReady(Int16Buffer);
TEST_EmptyBufferShouldNotBeReady(Int32Buffer);
TEST_EmptyBufferShouldNotBeReady(UInt32Buffer);
TEST_EmptyBufferShouldNotBeReady(Int64Buffer);
TEST_EmptyBufferShouldNotBeReady(BoolBuffer);
TEST_EmptyBufferShouldNotBeReady(StringBuffer);
TEST_EmptyBufferShouldNotBeReady(NullableStringBuffer);
TEST_EmptyBufferShouldNotBeReady(NullableBytesIgnoringBuffer);
TEST(CompositeBuffer, EmptyBufferShouldNotBeReady) {
  // given
  const CompositeBuffer<INT8, Int8Buffer> testee{};
  // when, then
  ASSERT_EQ(testee.ready(), false);
}
TEST(ArrayBuffer, EmptyBufferShouldNotBeReady) {
  // given
  const ArrayBuffer<INT8, Int8Buffer> testee{};
  // when, then
  ASSERT_EQ(testee.ready(), false);
}

// Null buffer is a special case, it's always ready and can provide results via 0-arg ctor
TEST(NullBuffer, EmptyBufferShouldBeReady) {
  // given
  const NullBuffer<INT8> testee{};
  // when, then
  ASSERT_EQ(testee.ready(), true);
  ASSERT_EQ(testee.get(), 0);
}

// === SERIALIZATION / DESERIALIZATION TESTS ===================================

Encoder encoder;

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

  uint64_t remaining = 1024;
  const uint64_t orig_remaining = remaining;
  const std::unique_ptr<char[]> holder = std::unique_ptr<char[]>(new char[remaining]);

  const size_t written = encoder.encode(expected, holder.get());

  const char* data = holder.get();
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

  const std::unique_ptr<char[]> holder = std::unique_ptr<char[]>(new char[1024]);

  const size_t written = encoder.encode(expected, holder.get());

  const char* data = holder.get();
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

template <typename BT, typename AT>
void serializeThenDeserializeAndCheckEquality(AT expected) {
  serializeThenDeserializeAndCheckEqualityInOneGo<BT>(expected);
  serializeThenDeserializeAndCheckEqualityWithChunks<BT>(expected);
}

// === NUMERIC BUFFERS =========================================================

// macroed out test for numeric buffers
#define TEST_BufferShouldDeserialize(BufferClass, DataClass, Value)            \
TEST(DataClass, ShouldConsumeCorrectAmountOfData) {                            \
  /* given */                                                                  \
  const DataClass value = Value;                                               \
  serializeThenDeserializeAndCheckEquality<BufferClass>(value);                \
}

TEST_BufferShouldDeserialize(Int8Buffer, INT8, 42);
TEST_BufferShouldDeserialize(Int16Buffer, INT16, 42);
TEST_BufferShouldDeserialize(Int32Buffer, INT32, 42);
TEST_BufferShouldDeserialize(UInt32Buffer, UINT32, 42);
TEST_BufferShouldDeserialize(Int64Buffer, INT64, 42);
TEST_BufferShouldDeserialize(BoolBuffer, BOOLEAN, true);

// === (NULLABLE) STRING BUFFER ================================================

TEST(StringBuffer, ShouldDeserialize) {
  const STRING value = "sometext";
  serializeThenDeserializeAndCheckEquality<StringBuffer>(value);
}

TEST(StringBuffer, ShouldDeserializeEmptyString) {
  const STRING value = "";
  serializeThenDeserializeAndCheckEquality<StringBuffer>(value);
}

TEST(StringBuffer, ShouldThrowOnInvalidLength) {
  // given
  StringBuffer testee;

  uint64_t remaining = 1024;
  const std::unique_ptr<char[]> holder = std::unique_ptr<char[]>(new char[remaining]);

  INT16 len = -1;
  encoder.encode(len, holder.get());

  const char* data = holder.get();

  // when
  // then
  EXPECT_THROW(testee.feed(data, remaining), EnvoyException);
}

TEST(NullableStringBuffer, ShouldDeserializeString) {
  // given
  const NULLABLE_STRING value{"sometext"};
  serializeThenDeserializeAndCheckEquality<NullableStringBuffer>(value);
}

TEST(NullableStringBuffer, ShouldDeserializeEmptyString) {
  // given
  const NULLABLE_STRING value{""};
  serializeThenDeserializeAndCheckEquality<NullableStringBuffer>(value);
}

TEST(NullableStringBuffer, ShouldDeserializeAbsentString) {
  // given
  const NULLABLE_STRING value = absl::nullopt;
  serializeThenDeserializeAndCheckEquality<NullableStringBuffer>(value);
}

TEST(NullableStringBuffer, ShouldThrowOnInvalidLength) {
  // given
  NullableStringBuffer testee;

  uint64_t remaining = 1024;
  const std::unique_ptr<char[]> holder = std::unique_ptr<char[]>(new char[remaining]);

  INT16 len = -2; // -1 is OK for NULLABLE_STRING
  encoder.encode(len, holder.get());

  const char* data = holder.get();

  // when
  // then
  EXPECT_THROW(testee.feed(data, remaining), EnvoyException);
}

// === NULLABLE BYTES IGNORING BUFFER ==========================================

TEST(NullableBytesIgnoringBuffer, ShouldDeserialize) {
  // given
  NullableBytesIgnoringBuffer testee;

  uint64_t remaining = 1024;
  const uint64_t orig_remaining = remaining;
  const std::unique_ptr<char[]> holder = std::unique_ptr<char[]>(new char[remaining]);

  const INT32 bytes_to_ignore = 100;
  const size_t header_size = encoder.encode(bytes_to_ignore, holder.get());

  const char* data = holder.get();
  const char* orig_data = data;

  // when
  const size_t consumed = testee.feed(data, remaining);

  // then
  ASSERT_EQ(consumed, header_size + bytes_to_ignore);
  ASSERT_EQ(testee.ready(), true);
  ASSERT_EQ(testee.get(), bytes_to_ignore);
  ASSERT_EQ(data, orig_data + consumed);
  ASSERT_EQ(remaining, orig_remaining - consumed);

  // when - 2
  const size_t consumed2 = testee.feed(data, remaining);

  // then - 2 (nothing changes)
  ASSERT_EQ(consumed2, 0);
  ASSERT_EQ(data, orig_data + consumed);
  ASSERT_EQ(remaining, orig_remaining - consumed);
}

TEST(NullableBytesIgnoringBuffer, ShouldDeserializeNullBytes) {
  // given
  NullableBytesIgnoringBuffer testee;

  uint64_t remaining = 1024;
  const uint64_t orig_remaining = remaining;
  const std::unique_ptr<char[]> holder = std::unique_ptr<char[]>(new char[remaining]);

  const INT32 bytes_length = -1;
  const size_t header_size = encoder.encode(bytes_length, holder.get());

  const char* data = holder.get();
  const char* orig_data = data;

  // when
  const size_t consumed = testee.feed(data, remaining);

  // then
  ASSERT_EQ(consumed, header_size);
  ASSERT_EQ(testee.ready(), true);
  ASSERT_EQ(testee.get(), bytes_length);
  ASSERT_EQ(data, orig_data + consumed);
  ASSERT_EQ(remaining, orig_remaining - consumed);

  // when - 2
  const size_t consumed2 = testee.feed(data, remaining);

  // then - 2 (nothing changes)
  ASSERT_EQ(consumed2, 0);
  ASSERT_EQ(data, orig_data + consumed);
  ASSERT_EQ(remaining, orig_remaining - consumed);
}

TEST(NullableBytesIgnoringBuffer, ShouldThrowOnInvalidLength) {
  // given
  NullableBytesIgnoringBuffer testee;

  uint64_t remaining = 1024;
  const std::unique_ptr<char[]> holder = std::unique_ptr<char[]>(new char[remaining]);

  INT32 len = -2; // -1 is OK for NULLABLE_BYTES
  encoder.encode(len, holder.get());

  const char* data = holder.get();

  // when
  // then
  EXPECT_THROW(testee.feed(data, remaining), EnvoyException);
}

// === NULLABLE BYTES CAPTURING BUFFER =========================================

TEST(NullableBytesCapturingBuffer, ShouldDeserialize) {
  const NULLABLE_BYTES value{{ 'a', 'b', 'c', 'd' }};
  serializeThenDeserializeAndCheckEquality<NullableBytesCapturingBuffer>(value);
}

TEST(NullableBytesCapturingBuffer, ShouldDeserializeEmptyBytes) {
  const NULLABLE_BYTES value{{}};
  serializeThenDeserializeAndCheckEquality<NullableBytesCapturingBuffer>(value);
}

TEST(NullableBytesCapturingBuffer, ShouldDeserializeNullBytes) {
  const NULLABLE_BYTES value = absl::nullopt;
  serializeThenDeserializeAndCheckEquality<NullableBytesCapturingBuffer>(value);
}

TEST(NullableBytesCapturingBuffer, ShouldThrowOnInvalidLength) {
  // given
  NullableBytesCapturingBuffer testee;

  uint64_t remaining = 1024;
  const std::unique_ptr<char[]> holder = std::unique_ptr<char[]>(new char[remaining]);

  INT16 len = -2; // -1 is OK for NULLABLE_BYTES
  encoder.encode(len, holder.get());

  const char* data = holder.get();

  // when
  // then
  EXPECT_THROW(testee.feed(data, remaining), EnvoyException);
}

// === ARRAY BUFFER ============================================================

TEST(ArrayBuffer, ShouldConsumeCorrectAmountOfData) {
  const NULLABLE_ARRAY<STRING> value{{ "aaa", "bbbbb", "cc", "d", "e", "ffffffff" }};
  serializeThenDeserializeAndCheckEquality<ArrayBuffer<STRING, StringBuffer>>(value);
}

TEST(ArrayBuffer, ShouldThrowOnInvalidLength) {
  // given
  ArrayBuffer<std::string, StringBuffer> testee;

  uint64_t remaining = 1024;
  const std::unique_ptr<char[]> holder = std::unique_ptr<char[]>(new char[remaining]);

  INT32 len = -2; // -1 is OK for ARRAY
  encoder.encode(len, holder.get());

  const char* data = holder.get();

  // when
  // then
  EXPECT_THROW(testee.feed(data, remaining), EnvoyException);
}

// === COMPOSITE BUFFER ========================================================

struct CompositeBufferResult {
  STRING field1_;
  NULLABLE_ARRAY<INT32> field2_;
  INT16 field3_;

  size_t encode(char* dst, Encoder& encoder) const {
    size_t written{0};
    written += encoder.encode(field1_, dst + written);
    written += encoder.encode(field2_, dst + written);
    written += encoder.encode(field3_, dst + written);
    return written;
  }
};

bool operator==(const CompositeBufferResult& lhs, const CompositeBufferResult& rhs) {
  return (lhs.field1_== rhs.field1_)
      && (lhs.field2_== rhs.field2_)
      && (lhs.field3_== rhs.field3_);
}

typedef CompositeBuffer<CompositeBufferResult, StringBuffer, ArrayBuffer<INT32, Int32Buffer>, Int16Buffer> TestCompositeBuffer;

TEST(CompositeBuffer, ShouldDeserialize) {
  const CompositeBufferResult expected {
    "zzzzz",
    {{ 10, 20, 30, 40, 50 }},
    1234
  };
  serializeThenDeserializeAndCheckEquality<TestCompositeBuffer>(expected);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
