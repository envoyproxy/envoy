#pragma once

#include "common/buffer/buffer_impl.h"
#include "common/common/stack_array.h"

#include "extensions/filters/network/kafka/serialization.h"

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Verifies that 'incremented' string view is actually 'original' string view, that has incremented
 * by 'difference' bytes.
 */
void assertStringViewIncrement(absl::string_view incremented, absl::string_view original,
                               size_t difference) {

  ASSERT_EQ(incremented.data(), original.data() + difference);
  ASSERT_EQ(incremented.size(), original.size() - difference);
}

// Helper function converting buffer to raw bytes.
const char* getRawData(const Buffer::OwnedImpl& buffer) {
  uint64_t num_slices = buffer.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
  buffer.getRawSlices(slices.begin(), num_slices);
  return reinterpret_cast<const char*>((slices[0]).mem_);
}

// Exactly what is says on the tin:
// 1. serialize expected using Encoder,
// 2. deserialize byte array using testee deserializer,
// 3. verify that testee is ready, and its result is equal to expected,
// 4. verify that data pointer moved correct amount,
// 5. feed testee more data,
// 6. verify that nothing more was consumed (because the testee has been ready since step 3).
template <typename BT, typename AT>
void serializeThenDeserializeAndCheckEqualityInOneGo(AT expected) {
  // given
  BT testee{};

  Buffer::OwnedImpl buffer;
  EncodingContext encoder{-1};
  const size_t written = encoder.encode(expected, buffer);

  // Tell parser that there is more data, it should never consume more than written.
  const absl::string_view orig_data = {getRawData(buffer), 10 * written};
  absl::string_view data = orig_data;

  // when
  const size_t consumed = testee.feed(data);

  // then
  ASSERT_EQ(consumed, written);
  ASSERT_EQ(testee.ready(), true);
  ASSERT_EQ(testee.get(), expected);
  assertStringViewIncrement(data, orig_data, consumed);

  // when - 2
  const size_t consumed2 = testee.feed(data);

  // then - 2 (nothing changes)
  ASSERT_EQ(consumed2, 0);
  assertStringViewIncrement(data, orig_data, consumed);
}

// Does the same thing as the above test, but instead of providing whole data at one, it provides
// it in N one-byte chunks.
// This verifies if deserializer keeps state properly (no overwrites etc.).
template <typename BT, typename AT>
void serializeThenDeserializeAndCheckEqualityWithChunks(AT expected) {
  // given
  BT testee{};

  Buffer::OwnedImpl buffer;
  EncodingContext encoder{-1};
  const size_t written = encoder.encode(expected, buffer);

  const absl::string_view orig_data = {getRawData(buffer), written};

  // when
  absl::string_view data = orig_data;
  size_t consumed = 0;
  for (size_t i = 0; i < written; ++i) {
    data = {data.data(), 1}; // Consume data byte-by-byte.
    size_t step = testee.feed(data);
    consumed += step;
    ASSERT_EQ(step, 1);
    ASSERT_EQ(data.size(), 0);
  }

  // then
  ASSERT_EQ(consumed, written);
  ASSERT_EQ(testee.ready(), true);
  ASSERT_EQ(testee.get(), expected);
  assertStringViewIncrement(data, orig_data, consumed);

  // when - 2
  absl::string_view more_data = {data.data(), 1024};
  const size_t consumed2 = testee.feed(more_data);

  // then - 2 (nothing changes)
  ASSERT_EQ(consumed2, 0);
  ASSERT_EQ(more_data.data(), orig_data.data() + consumed);
  ASSERT_EQ(more_data.size(), 1024);
}

// Wrapper to run both tests.
template <typename BT, typename AT> void serializeThenDeserializeAndCheckEquality(AT expected) {
  serializeThenDeserializeAndCheckEqualityInOneGo<BT>(expected);
  serializeThenDeserializeAndCheckEqualityWithChunks<BT>(expected);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
