// DO NOT EDIT - THIS FILE WAS GENERATED
// clang-format off
#include "common/common/stack_array.h"

#include "extensions/filters/network/kafka/generated/serialization_composite.h"
#include "extensions/filters/network/kafka/serialization.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Tests in this class are supposed to check whether serialization operations
 * on composite deserializers are behaving correctly
 */

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
  EncodingContext encoder;
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
  EncodingContext encoder;
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

// tests for composite deserializers

struct CompositeResultWith0Fields {

  size_t encode(Buffer::Instance&, EncodingContext&) const {
    return 0;
  }

  bool operator==(const CompositeResultWith0Fields&) const {
    return true;
  }
};

typedef CompositeDeserializerWith0Delegates<CompositeResultWith0Fields>
    TestCompositeDeserializer0;

// composite with 0 delegates is special case: it's always ready
TEST(CompositeDeserializerWith0Delegates, EmptyBufferShouldBeReady) {
  // given
  const TestCompositeDeserializer0 testee{};
  // when, then
  ASSERT_EQ(testee.ready(), true);
}

TEST(CompositeDeserializerWith0Delegates, ShouldDeserialize) {
  const CompositeResultWith0Fields expected{};
  serializeThenDeserializeAndCheckEquality<TestCompositeDeserializer0>(expected);
}

struct CompositeResultWith1Fields {
  const std::string field1_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(field1_, dst);
    return written;
  }

  bool operator==(const CompositeResultWith1Fields& rhs) const {
    return field1_ == rhs.field1_;
  }
};

typedef CompositeDeserializerWith1Delegates<CompositeResultWith1Fields, StringDeserializer>
    TestCompositeDeserializer1;

TEST(CompositeDeserializerWith1Delegates, EmptyBufferShouldNotBeReady) {
  // given
  const TestCompositeDeserializer1 testee{};
  // when, then
  ASSERT_EQ(testee.ready(), false);
}

TEST(CompositeDeserializerWith1Delegates, ShouldDeserialize) {
  const CompositeResultWith1Fields expected{"s1"};
  serializeThenDeserializeAndCheckEquality<TestCompositeDeserializer1>(expected);
}

struct CompositeResultWith2Fields {
  const std::string field1_;
  const std::string field2_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(field1_, dst);
    written += encoder.encode(field2_, dst);
    return written;
  }

  bool operator==(const CompositeResultWith2Fields& rhs) const {
    return field1_ == rhs.field1_ && field2_ == rhs.field2_;
  }
};

typedef CompositeDeserializerWith2Delegates<CompositeResultWith2Fields, StringDeserializer,StringDeserializer>
    TestCompositeDeserializer2;

TEST(CompositeDeserializerWith2Delegates, EmptyBufferShouldNotBeReady) {
  // given
  const TestCompositeDeserializer2 testee{};
  // when, then
  ASSERT_EQ(testee.ready(), false);
}

TEST(CompositeDeserializerWith2Delegates, ShouldDeserialize) {
  const CompositeResultWith2Fields expected{"s1", "s2"};
  serializeThenDeserializeAndCheckEquality<TestCompositeDeserializer2>(expected);
}

struct CompositeResultWith3Fields {
  const std::string field1_;
  const std::string field2_;
  const std::string field3_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(field1_, dst);
    written += encoder.encode(field2_, dst);
    written += encoder.encode(field3_, dst);
    return written;
  }

  bool operator==(const CompositeResultWith3Fields& rhs) const {
    return field1_ == rhs.field1_ && field2_ == rhs.field2_ && field3_ == rhs.field3_;
  }
};

typedef CompositeDeserializerWith3Delegates<CompositeResultWith3Fields, StringDeserializer,StringDeserializer,StringDeserializer>
    TestCompositeDeserializer3;

TEST(CompositeDeserializerWith3Delegates, EmptyBufferShouldNotBeReady) {
  // given
  const TestCompositeDeserializer3 testee{};
  // when, then
  ASSERT_EQ(testee.ready(), false);
}

TEST(CompositeDeserializerWith3Delegates, ShouldDeserialize) {
  const CompositeResultWith3Fields expected{"s1", "s2", "s3"};
  serializeThenDeserializeAndCheckEquality<TestCompositeDeserializer3>(expected);
}

struct CompositeResultWith4Fields {
  const std::string field1_;
  const std::string field2_;
  const std::string field3_;
  const std::string field4_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(field1_, dst);
    written += encoder.encode(field2_, dst);
    written += encoder.encode(field3_, dst);
    written += encoder.encode(field4_, dst);
    return written;
  }

  bool operator==(const CompositeResultWith4Fields& rhs) const {
    return field1_ == rhs.field1_ && field2_ == rhs.field2_ && field3_ == rhs.field3_ && field4_ == rhs.field4_;
  }
};

typedef CompositeDeserializerWith4Delegates<CompositeResultWith4Fields, StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer>
    TestCompositeDeserializer4;

TEST(CompositeDeserializerWith4Delegates, EmptyBufferShouldNotBeReady) {
  // given
  const TestCompositeDeserializer4 testee{};
  // when, then
  ASSERT_EQ(testee.ready(), false);
}

TEST(CompositeDeserializerWith4Delegates, ShouldDeserialize) {
  const CompositeResultWith4Fields expected{"s1", "s2", "s3", "s4"};
  serializeThenDeserializeAndCheckEquality<TestCompositeDeserializer4>(expected);
}

struct CompositeResultWith5Fields {
  const std::string field1_;
  const std::string field2_;
  const std::string field3_;
  const std::string field4_;
  const std::string field5_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(field1_, dst);
    written += encoder.encode(field2_, dst);
    written += encoder.encode(field3_, dst);
    written += encoder.encode(field4_, dst);
    written += encoder.encode(field5_, dst);
    return written;
  }

  bool operator==(const CompositeResultWith5Fields& rhs) const {
    return field1_ == rhs.field1_ && field2_ == rhs.field2_ && field3_ == rhs.field3_ && field4_ == rhs.field4_ && field5_ == rhs.field5_;
  }
};

typedef CompositeDeserializerWith5Delegates<CompositeResultWith5Fields, StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer>
    TestCompositeDeserializer5;

TEST(CompositeDeserializerWith5Delegates, EmptyBufferShouldNotBeReady) {
  // given
  const TestCompositeDeserializer5 testee{};
  // when, then
  ASSERT_EQ(testee.ready(), false);
}

TEST(CompositeDeserializerWith5Delegates, ShouldDeserialize) {
  const CompositeResultWith5Fields expected{"s1", "s2", "s3", "s4", "s5"};
  serializeThenDeserializeAndCheckEquality<TestCompositeDeserializer5>(expected);
}

struct CompositeResultWith6Fields {
  const std::string field1_;
  const std::string field2_;
  const std::string field3_;
  const std::string field4_;
  const std::string field5_;
  const std::string field6_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(field1_, dst);
    written += encoder.encode(field2_, dst);
    written += encoder.encode(field3_, dst);
    written += encoder.encode(field4_, dst);
    written += encoder.encode(field5_, dst);
    written += encoder.encode(field6_, dst);
    return written;
  }

  bool operator==(const CompositeResultWith6Fields& rhs) const {
    return field1_ == rhs.field1_ && field2_ == rhs.field2_ && field3_ == rhs.field3_ && field4_ == rhs.field4_ && field5_ == rhs.field5_ && field6_ == rhs.field6_;
  }
};

typedef CompositeDeserializerWith6Delegates<CompositeResultWith6Fields, StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer>
    TestCompositeDeserializer6;

TEST(CompositeDeserializerWith6Delegates, EmptyBufferShouldNotBeReady) {
  // given
  const TestCompositeDeserializer6 testee{};
  // when, then
  ASSERT_EQ(testee.ready(), false);
}

TEST(CompositeDeserializerWith6Delegates, ShouldDeserialize) {
  const CompositeResultWith6Fields expected{"s1", "s2", "s3", "s4", "s5", "s6"};
  serializeThenDeserializeAndCheckEquality<TestCompositeDeserializer6>(expected);
}

struct CompositeResultWith7Fields {
  const std::string field1_;
  const std::string field2_;
  const std::string field3_;
  const std::string field4_;
  const std::string field5_;
  const std::string field6_;
  const std::string field7_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(field1_, dst);
    written += encoder.encode(field2_, dst);
    written += encoder.encode(field3_, dst);
    written += encoder.encode(field4_, dst);
    written += encoder.encode(field5_, dst);
    written += encoder.encode(field6_, dst);
    written += encoder.encode(field7_, dst);
    return written;
  }

  bool operator==(const CompositeResultWith7Fields& rhs) const {
    return field1_ == rhs.field1_ && field2_ == rhs.field2_ && field3_ == rhs.field3_ && field4_ == rhs.field4_ && field5_ == rhs.field5_ && field6_ == rhs.field6_ && field7_ == rhs.field7_;
  }
};

typedef CompositeDeserializerWith7Delegates<CompositeResultWith7Fields, StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer>
    TestCompositeDeserializer7;

TEST(CompositeDeserializerWith7Delegates, EmptyBufferShouldNotBeReady) {
  // given
  const TestCompositeDeserializer7 testee{};
  // when, then
  ASSERT_EQ(testee.ready(), false);
}

TEST(CompositeDeserializerWith7Delegates, ShouldDeserialize) {
  const CompositeResultWith7Fields expected{"s1", "s2", "s3", "s4", "s5", "s6", "s7"};
  serializeThenDeserializeAndCheckEquality<TestCompositeDeserializer7>(expected);
}

struct CompositeResultWith8Fields {
  const std::string field1_;
  const std::string field2_;
  const std::string field3_;
  const std::string field4_;
  const std::string field5_;
  const std::string field6_;
  const std::string field7_;
  const std::string field8_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(field1_, dst);
    written += encoder.encode(field2_, dst);
    written += encoder.encode(field3_, dst);
    written += encoder.encode(field4_, dst);
    written += encoder.encode(field5_, dst);
    written += encoder.encode(field6_, dst);
    written += encoder.encode(field7_, dst);
    written += encoder.encode(field8_, dst);
    return written;
  }

  bool operator==(const CompositeResultWith8Fields& rhs) const {
    return field1_ == rhs.field1_ && field2_ == rhs.field2_ && field3_ == rhs.field3_ && field4_ == rhs.field4_ && field5_ == rhs.field5_ && field6_ == rhs.field6_ && field7_ == rhs.field7_ && field8_ == rhs.field8_;
  }
};

typedef CompositeDeserializerWith8Delegates<CompositeResultWith8Fields, StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer>
    TestCompositeDeserializer8;

TEST(CompositeDeserializerWith8Delegates, EmptyBufferShouldNotBeReady) {
  // given
  const TestCompositeDeserializer8 testee{};
  // when, then
  ASSERT_EQ(testee.ready(), false);
}

TEST(CompositeDeserializerWith8Delegates, ShouldDeserialize) {
  const CompositeResultWith8Fields expected{"s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8"};
  serializeThenDeserializeAndCheckEquality<TestCompositeDeserializer8>(expected);
}

struct CompositeResultWith9Fields {
  const std::string field1_;
  const std::string field2_;
  const std::string field3_;
  const std::string field4_;
  const std::string field5_;
  const std::string field6_;
  const std::string field7_;
  const std::string field8_;
  const std::string field9_;

  size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
    size_t written{0};
    written += encoder.encode(field1_, dst);
    written += encoder.encode(field2_, dst);
    written += encoder.encode(field3_, dst);
    written += encoder.encode(field4_, dst);
    written += encoder.encode(field5_, dst);
    written += encoder.encode(field6_, dst);
    written += encoder.encode(field7_, dst);
    written += encoder.encode(field8_, dst);
    written += encoder.encode(field9_, dst);
    return written;
  }

  bool operator==(const CompositeResultWith9Fields& rhs) const {
    return field1_ == rhs.field1_ && field2_ == rhs.field2_ && field3_ == rhs.field3_ && field4_ == rhs.field4_ && field5_ == rhs.field5_ && field6_ == rhs.field6_ && field7_ == rhs.field7_ && field8_ == rhs.field8_ && field9_ == rhs.field9_;
  }
};

typedef CompositeDeserializerWith9Delegates<CompositeResultWith9Fields, StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer,StringDeserializer>
    TestCompositeDeserializer9;

TEST(CompositeDeserializerWith9Delegates, EmptyBufferShouldNotBeReady) {
  // given
  const TestCompositeDeserializer9 testee{};
  // when, then
  ASSERT_EQ(testee.ready(), false);
}

TEST(CompositeDeserializerWith9Delegates, ShouldDeserialize) {
  const CompositeResultWith9Fields expected{"s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9"};
  serializeThenDeserializeAndCheckEquality<TestCompositeDeserializer9>(expected);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
// clang-format on
