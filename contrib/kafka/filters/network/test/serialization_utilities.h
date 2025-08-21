#pragma once

#include "source/common/buffer/buffer_impl.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/string_view.h"
#include "contrib/kafka/filters/network/source/serialization.h"
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
                               size_t difference);

// Helper function converting buffer to raw bytes.
const char* getRawData(const Buffer::Instance& buffer);

// Helper methods for testing serialization and deserialization.
// We have two dimensions to test here: single-pass vs chunks (as we never know how the input is
// going to be delivered), and normal vs compact for some data types (like strings).

// Exactly what is says on the tin:
// 1. serialize expected using Encoder,
// 2. deserialize byte array using testee deserializer,
// 3. verify that testee is ready, and its result is equal to expected,
// 4. verify that data pointer moved correct amount,
// 5. feed testee more data,
// 6. verify that nothing more was consumed (because the testee has been ready since step 3).
template <typename BT>
void serializeThenDeserializeAndCheckEqualityInOneGo(const typename BT::result_type expected) {
  // given
  BT testee{};

  Buffer::OwnedImpl buffer;
  EncodingContext encoder{-1};
  const uint32_t written = encoder.encode(expected, buffer);
  // Insert garbage after serialized payload.
  const uint32_t garbage_size = encoder.encode(Bytes(10000), buffer);

  // Tell parser that there is more data, it should never consume more than written.
  const absl::string_view orig_data = {getRawData(buffer), written + garbage_size};
  absl::string_view data = orig_data;

  // when
  const uint32_t consumed = testee.feed(data);

  // then
  ASSERT_EQ(consumed, written);
  ASSERT_EQ(testee.ready(), true);
  ASSERT_EQ(testee.get(), expected);
  assertStringViewIncrement(data, orig_data, consumed);

  // when - 2
  const uint32_t consumed2 = testee.feed(data);

  // then - 2 (nothing changes)
  ASSERT_EQ(consumed2, 0);
  assertStringViewIncrement(data, orig_data, consumed);
}

// Does the same thing as the above test, but instead of providing whole data at one, it provides
// it in N one-byte chunks.
// This verifies if deserializer keeps state properly (no overwrites etc.).
template <typename BT>
void serializeThenDeserializeAndCheckEqualityWithChunks(const typename BT::result_type expected) {
  // given
  BT testee{};

  Buffer::OwnedImpl buffer;
  EncodingContext encoder{-1};
  const uint32_t written = encoder.encode(expected, buffer);
  // Insert garbage after serialized payload.
  const uint32_t garbage_size = encoder.encode(Bytes(10000), buffer);

  const absl::string_view orig_data = {getRawData(buffer), written + garbage_size};

  // when
  absl::string_view data = orig_data;
  uint32_t consumed = 0;
  for (uint32_t i = 0; i < written; ++i) {
    data = {data.data(), 1}; // Consume data byte-by-byte.
    uint32_t step = testee.feed(data);
    consumed += step;
    ASSERT_EQ(step, 1);
    ASSERT_EQ(data.size(), 0);
  }

  // then
  ASSERT_EQ(consumed, written);
  ASSERT_EQ(testee.ready(), true);
  ASSERT_EQ(testee.get(), expected);

  ASSERT_EQ(data.data(), orig_data.data() + consumed);

  // when - 2
  absl::string_view more_data = {data.data(), garbage_size};
  const uint32_t consumed2 = testee.feed(more_data);

  // then - 2 (nothing changes)
  ASSERT_EQ(consumed2, 0);
  ASSERT_EQ(more_data.data(), data.data());
  ASSERT_EQ(more_data.size(), garbage_size);
}

// Deserialization (only) of compact-encoded data (for data types where we do not need serializer
// code).
template <typename BT>
void deserializeCompactAndCheckEqualityInOneGo(Buffer::Instance& buffer,
                                               const typename BT::result_type expected) {
  // given
  BT testee{};

  EncodingContext encoder{-1};
  const uint32_t written = buffer.length();
  // Insert garbage after serialized payload.
  const uint32_t garbage_size = encoder.encode(Bytes(10000), buffer);
  const char* raw_buffer_ptr =
      reinterpret_cast<const char*>(buffer.linearize(written + garbage_size));
  // Tell parser that there is more data, it should never consume more than written.
  const absl::string_view orig_data = {raw_buffer_ptr, written + garbage_size};
  absl::string_view data = orig_data;

  // when
  const uint32_t consumed = testee.feed(data);

  // then
  ASSERT_EQ(consumed, written);
  ASSERT_EQ(testee.ready(), true);
  ASSERT_EQ(testee.get(), expected);
  assertStringViewIncrement(data, orig_data, consumed);

  // when - 2
  const uint32_t consumed2 = testee.feed(data);

  // then - 2 (nothing changes)
  ASSERT_EQ(consumed2, 0);
  assertStringViewIncrement(data, orig_data, consumed);
}

// Does the same thing as the above test, but instead of providing whole data at one, it provides
// it in N one-byte chunks.
// This verifies if deserializer keeps state properly (no overwrites etc.).
template <typename BT>
void deserializeCompactAndCheckEqualityWithChunks(Buffer::Instance& buffer,
                                                  const typename BT::result_type expected) {
  // given
  BT testee{};

  EncodingContext encoder{-1};
  const uint32_t written = buffer.length();
  // Insert garbage after serialized payload.
  const uint32_t garbage_size = encoder.encode(Bytes(10000), buffer);

  const char* raw_buffer_ptr =
      reinterpret_cast<const char*>(buffer.linearize(written + garbage_size));
  // Tell parser that there is more data, it should never consume more than written.
  const absl::string_view orig_data = {raw_buffer_ptr, written + garbage_size};

  // when
  absl::string_view data = orig_data;
  uint32_t consumed = 0;
  for (uint32_t i = 0; i < written; ++i) {
    data = {data.data(), 1}; // Consume data byte-by-byte.
    uint32_t step = testee.feed(data);
    consumed += step;
    ASSERT_EQ(step, 1);
    ASSERT_EQ(data.size(), 0);
  }

  // then
  ASSERT_EQ(consumed, written);
  ASSERT_EQ(testee.ready(), true);
  ASSERT_EQ(testee.get(), expected);

  ASSERT_EQ(data.data(), orig_data.data() + consumed);

  // when - 2
  absl::string_view more_data = {data.data(), garbage_size};
  const uint32_t consumed2 = testee.feed(more_data);

  // then - 2 (nothing changes)
  ASSERT_EQ(consumed2, 0);
  ASSERT_EQ(more_data.data(), data.data());
  ASSERT_EQ(more_data.size(), garbage_size);
}

// Same thing as 'serializeThenDeserializeAndCheckEqualityInOneGo', just uses compact encoding.
template <typename BT>
void serializeCompactThenDeserializeAndCheckEqualityInOneGo(
    const typename BT::result_type expected) {
  Buffer::OwnedImpl buffer;
  EncodingContext encoder{-1};
  const uint32_t expected_written_size = encoder.computeCompactSize(expected);
  const uint32_t written = encoder.encodeCompact(expected, buffer);
  ASSERT_EQ(written, expected_written_size);
  deserializeCompactAndCheckEqualityInOneGo<BT>(buffer, expected);
}

// Same thing as 'serializeThenDeserializeAndCheckEqualityWithChunks', just uses compact encoding.
template <typename BT>
void serializeCompactThenDeserializeAndCheckEqualityWithChunks(
    const typename BT::result_type expected) {
  // given
  BT testee{};

  Buffer::OwnedImpl buffer;
  EncodingContext encoder{-1};
  const uint32_t expected_written_size = encoder.computeCompactSize(expected);
  const uint32_t written = encoder.encodeCompact(expected, buffer);
  ASSERT_EQ(written, expected_written_size);
  // Insert garbage after serialized payload.
  const uint32_t garbage_size = encoder.encode(Bytes(10000), buffer);

  const char* raw_buffer_ptr =
      reinterpret_cast<const char*>(buffer.linearize(written + garbage_size));
  // Tell parser that there is more data, it should never consume more than written.
  const absl::string_view orig_data = {raw_buffer_ptr, written + garbage_size};

  // when
  absl::string_view data = orig_data;
  uint32_t consumed = 0;
  for (uint32_t i = 0; i < written; ++i) {
    data = {data.data(), 1}; // Consume data byte-by-byte.
    uint32_t step = testee.feed(data);
    consumed += step;
    ASSERT_EQ(step, 1);
    ASSERT_EQ(data.size(), 0);
  }

  // then
  ASSERT_EQ(consumed, written);
  ASSERT_EQ(testee.ready(), true);
  ASSERT_EQ(testee.get(), expected);

  ASSERT_EQ(data.data(), orig_data.data() + consumed);

  // when - 2
  absl::string_view more_data = {data.data(), garbage_size};
  const uint32_t consumed2 = testee.feed(more_data);

  // then - 2 (nothing changes)
  ASSERT_EQ(consumed2, 0);
  ASSERT_EQ(more_data.data(), data.data());
  ASSERT_EQ(more_data.size(), garbage_size);
}

// Wrapper to run both tests for normal serialization.
template <typename BT>
void serializeThenDeserializeAndCheckEquality(const typename BT::result_type expected) {
  serializeThenDeserializeAndCheckEqualityInOneGo<BT>(expected);
  serializeThenDeserializeAndCheckEqualityWithChunks<BT>(expected);
}

// Wrapper to run both tests for compact serialization.
template <typename BT>
void serializeCompactThenDeserializeAndCheckEquality(const typename BT::result_type expected) {
  serializeCompactThenDeserializeAndCheckEqualityInOneGo<BT>(expected);
  serializeCompactThenDeserializeAndCheckEqualityWithChunks<BT>(expected);
}

// Wrapper to run both tests for compact deserialization (for non-serializable types).
template <typename BT>
void deserializeCompactAndCheckEquality(Buffer::Instance& buffer,
                                        const typename BT::result_type expected) {
  Buffer::OwnedImpl
      copy_for_chunking_test; // Tests modify input buffers, so let's just make a copy.
  copy_for_chunking_test.add(getRawData(buffer), buffer.length());
  deserializeCompactAndCheckEqualityInOneGo<BT>(buffer, expected);
  deserializeCompactAndCheckEqualityWithChunks<BT>(copy_for_chunking_test, expected);
}

/**
 * Message callback that captures the messages.
 */
template <typename Base, typename Message, typename Failure> class CapturingCallback : public Base {
public:
  /**
   * Stores the message.
   */
  void onMessage(Message message) override { captured_messages_.push_back(message); }

  /**
   * Returns the stored messages.
   */
  const std::vector<Message>& getCapturedMessages() const { return captured_messages_; }

  void onFailedParse(Failure failure_data) override { parse_failures_.push_back(failure_data); }

  const std::vector<Failure>& getParseFailures() const { return parse_failures_; }

private:
  std::vector<Message> captured_messages_;
  std::vector<Failure> parse_failures_;
};

template <typename Base, typename Message, typename Failure>
using CapturingCallbackSharedPtr = std::shared_ptr<CapturingCallback<Base, Message, Failure>>;

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
