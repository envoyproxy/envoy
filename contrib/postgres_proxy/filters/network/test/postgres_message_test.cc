#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "source/common/buffer/buffer_impl.h"

#include "contrib/postgres_proxy/filters/network/source/postgres_message.h"
#include "fmt/printf.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

// Tests for individual types used in Postgres messages.
//
// Integer types.

// Fixture class for testing Integer types.
template <typename T> class IntTest : public testing::Test {
public:
  T field_;
  Buffer::OwnedImpl data_;
};

using IntTypes = ::testing::Types<Int32, Int16, Int8>;
TYPED_TEST_SUITE(IntTest, IntTypes);

TYPED_TEST(IntTest, BasicRead) {
  this->data_.template writeBEInt<decltype(std::declval<TypeParam>().get())>(12);
  uint64_t pos = 0;
  uint64_t left;
  // Simulate that message is too short.
  left = sizeof(TypeParam) - 1;
  ASSERT_THAT(Message::ValidationFailed, this->field_.validate(this->data_, 0, pos, left));
  // Single 4-byte int. Message length is correct.
  left = sizeof(TypeParam);
  ASSERT_THAT(Message::ValidationOK, this->field_.validate(this->data_, 0, pos, left));

  // Read the value after successful validation.
  pos = 0;
  left = sizeof(TypeParam);
  ASSERT_TRUE(this->field_.read(this->data_, pos, left));

  ASSERT_THAT(this->field_.toString(), "[12]");
  // pos should be moved forward by the number of bytes read.
  ASSERT_THAT(pos, sizeof(TypeParam));
  ASSERT_THAT(12, this->field_.get());

  // Make sure that all bytes have been read from the buffer.
  ASSERT_THAT(left, 0);
}

TYPED_TEST(IntTest, ReadWithLeftovers) {
  this->data_.template writeBEInt<decltype(std::declval<TypeParam>().get())>(12);
  // Write 1 byte more.
  this->data_.template writeBEInt<uint8_t>(11);
  uint64_t pos = 0;
  uint64_t left = this->data_.length();
  ASSERT_THAT(Message::ValidationOK, this->field_.validate(this->data_, 0, pos, left));

  pos = 0;
  left = this->data_.length();
  ASSERT_TRUE(this->field_.read(this->data_, pos, left));
  ASSERT_THAT(this->field_.toString(), "[12]");
  // pos should be moved forward by the number of bytes read.
  ASSERT_THAT(pos, sizeof(TypeParam));

  // Make sure that all bytes have been read from the buffer.
  ASSERT_THAT(left, 1);
}

TYPED_TEST(IntTest, ReadAtOffset) {
  // write 1 byte before the actual value.
  this->data_.template writeBEInt<uint8_t>(11);
  this->data_.template writeBEInt<decltype(std::declval<TypeParam>().get())>(12);

  uint64_t pos = 1;
  uint64_t left = this->data_.length() - 1;
  ASSERT_THAT(Message::ValidationOK, this->field_.validate(this->data_, 1, pos, left));

  pos = 1;
  left = this->data_.length() - 1;
  ASSERT_TRUE(this->field_.read(this->data_, pos, left));
  ASSERT_THAT(this->field_.toString(), "[12]");
  // pos should be moved forward by the number of bytes read.
  ASSERT_THAT(pos, 1 + sizeof(TypeParam));
  // Nothing should be left to read.
  ASSERT_THAT(left, 0);
}

TYPED_TEST(IntTest, NotEnoughData) {
  this->data_.template writeBEInt<decltype(std::declval<TypeParam>().get())>(12);
  // Start from offset 1. There is not enough data in the buffer for the required type.
  uint64_t pos = 1;
  uint64_t left = this->data_.length();

  ASSERT_THAT(this->field_.validate(this->data_, 0, pos, left), Message::ValidationNeedMoreData);
}

// Byte1 should format content as char.
TEST(Byte1, Formatting) {
  Byte1 field;

  Buffer::OwnedImpl data;
  data.add("I");

  uint64_t pos = 0;
  uint64_t left = 1;
  ASSERT_THAT(Message::ValidationOK, field.validate(data, 0, pos, left));
  ASSERT_THAT(pos, 1);
  ASSERT_THAT(left, 0);

  pos = 0;
  left = 1;
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 1);
  ASSERT_THAT(left, 0);

  ASSERT_THAT(field.toString(), "[I]");
}

// Tests for String type.
TEST(StringType, SingleString) {
  String field;

  Buffer::OwnedImpl data;
  data.add("test");
  // Passed length 3 is too short.
  uint64_t pos = 0;
  uint64_t left = 3;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationFailed);
  // Correct length, but terminating zero is missing.
  left = 5;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationNeedMoreData);
  // Add terminating zero.
  data.writeBEInt<uint8_t>(0);
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  ASSERT_THAT(pos, 5);
  ASSERT_THAT(left, 0);

  pos = 0;
  left = 5;
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 5);
  ASSERT_THAT(left, 0);

  auto out = field.toString();
  ASSERT_THAT(out, "[test]");
}

TEST(StringType, NoTerminatingByte) {
  String field;

  Buffer::OwnedImpl data;
  data.add("test");
  uint64_t pos = 0;
  uint64_t left = 4;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationFailed);
  left = 5;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationNeedMoreData);
}

// ByteN type is always placed at the end of Postgres message.
// There is no explicit message length. Length must be deduced from
// "length" field on Postgres message.
TEST(ByteN, BasicTest) {
  ByteN field;

  Buffer::OwnedImpl data;
  // Write 11 bytes. We will read only 10 to make sure
  // that len is used, not buffer's length.
  for (auto i = 0; i < 11; i++) {
    data.writeBEInt<uint8_t>(i);
  }
  uint64_t pos = 0;
  uint64_t left;

  // Since ByteN structure does not contain length field, any
  // value less than number of bytes in the buffer should
  // pass validation.
  pos = 0;
  left = 0;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  ASSERT_THAT(pos, 0);
  ASSERT_THAT(left, 0);
  pos = 0;
  left = 1;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  ASSERT_THAT(pos, 1);
  ASSERT_THAT(left, 0);
  pos = 0;
  left = 4;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  ASSERT_THAT(pos, 4);
  ASSERT_THAT(left, 0);

  pos = 0;
  left = 10;
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 10);
  ASSERT_THAT(left, 0);

  auto out = field.toString();
  ASSERT_THAT(out, "[0 1 2 3 4 5 6 7 8 9]");
}

TEST(ByteN, NotEnoughData) {
  ByteN field;

  Buffer::OwnedImpl data;
  // Write 10 bytes, but set message length to be 11.
  for (auto i = 0; i < 10; i++) {
    data.writeBEInt<uint8_t>(i);
  }
  uint64_t pos = 0;
  uint64_t left = 11;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationNeedMoreData);
}

TEST(ByteN, Empty) {
  ByteN field;

  Buffer::OwnedImpl data;
  // Write nothing to data buffer.
  uint64_t pos = 0;
  uint64_t left = 0;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  ASSERT_TRUE(field.read(data, pos, left));

  auto out = field.toString();
  ASSERT_THAT(out, "[]");
}

// VarByteN type. It contains 4 bytes length field with value which follows.
TEST(VarByteN, BasicTest) {
  VarByteN field;
  Buffer::OwnedImpl data;

  uint64_t pos = 0;
  uint64_t left = 0;
  // Simulate that message ended and VarByteN's length fields  sticks past the
  // message boundary.
  data.writeBEInt<int32_t>(5);
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationFailed);

  // Write VarByteN with length equal to zero. No value follows.
  // Set structure length to be -1 (means no payload).
  left = 4;
  data.drain(data.length());
  data.writeBEInt<int32_t>(-1);
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  // The same for structure length 0.
  pos = 0;
  left = 4;
  data.drain(data.length());
  data.writeBEInt<int32_t>(0);
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);

  // Simulate that VarByteN would extend past message boundary.
  data.drain(data.length());
  data.writeBEInt<int32_t>(30);
  pos = 0;
  left = 4;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationFailed);

  // Simulate that VarByteN length is 6, there are 6 bytes left to the
  // message boundary, but buffer contains only 4 bytes.
  data.drain(data.length());
  data.writeBEInt<int32_t>(6);
  data.writeBEInt<uint32_t>(16);
  pos = 0;
  left = 6;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationNeedMoreData);

  data.drain(data.length());
  // Write first value.
  data.writeBEInt<int32_t>(0);

  // Write 2nd value with 5 bytes.
  data.writeBEInt<uint32_t>(5);
  for (auto i = 0; i < 5; i++) {
    data.writeBEInt<uint8_t>(10 + i);
  }

  // Write special case value with length -1. No value follows.
  data.writeBEInt<int32_t>(-1);

  pos = 0;
  left = 4 + 4 + 5 + 4;
  uint64_t expected_left = left;
  uint64_t orig_pos = pos;
  uint64_t orig_left = left;
  // Read the first value.
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  pos = orig_pos;
  left = orig_left;
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 4);
  expected_left -= 4;
  ASSERT_THAT(left, expected_left);
  auto out = field.toString();
  ASSERT_TRUE(out.find("0 bytes") != std::string::npos);

  // Read the second value.
  orig_pos = pos;
  orig_left = left;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  pos = orig_pos;
  left = orig_left;
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 4 + 4 + 5);
  expected_left -= (4 + 5);
  ASSERT_THAT(left, expected_left);
  out = field.toString();
  ASSERT_TRUE(out.find("5 bytes") != std::string::npos);
  ASSERT_TRUE(out.find("10 11 12 13 14") != std::string::npos);

  // Read the third value.
  orig_pos = pos;
  orig_left = left;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  pos = orig_pos;
  left = orig_left;
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 4 + 4 + 5 + 4);
  expected_left -= 4;
  ASSERT_THAT(left, expected_left);
  out = field.toString();
  ASSERT_TRUE(out.find("-1 bytes") != std::string::npos);
}

// Array composite type tests.
TEST(Array, SingleInt) {
  Array<Int32> field;

  Buffer::OwnedImpl data;
  // Simulate that message ends before the array.
  uint64_t pos = 0;
  uint64_t left = 1;
  data.writeBEInt<int8_t>(1);
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationFailed);

  // Write the value of the element into the array.
  data.drain(data.length());
  data.writeBEInt<int16_t>(1);
  data.writeBEInt<uint32_t>(123);
  // Simulate that message length end before end of array.
  left = 5;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationFailed);

  left = 6;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  ASSERT_THAT(pos, 6);
  ASSERT_THAT(left, 0);
  pos = 0;
  left = 6;
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 6);
  ASSERT_THAT(left, 0);

  auto out = field.toString();
  ASSERT_TRUE(out.find("Array of 1") != std::string::npos);
  ASSERT_TRUE(out.find("123") != std::string::npos);
}

TEST(Array, MultipleInts) {
  Array<Int8> field;

  Buffer::OwnedImpl data;
  // Write 3 as size of array, but add only 2 elements into array.
  data.writeBEInt<uint16_t>(3);
  data.writeBEInt<uint8_t>(211);
  data.writeBEInt<uint8_t>(212);

  uint64_t pos = 0;
  uint64_t left = 2 + 3 * 1;

  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationNeedMoreData);

  // Add the third element.
  data.writeBEInt<uint8_t>(213);

  // Simulate that message ends before end of the array.
  left = 2 + 3 * 1 - 1;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationFailed);

  left = 2 + 3 * 1;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  ASSERT_THAT(pos, 5);
  ASSERT_THAT(left, 0);
  pos = 0;
  left = 2 + 3 * 1;
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 5);
  ASSERT_THAT(left, 0);

  auto out = field.toString();
  ASSERT_TRUE(out.find("Array of 3") != std::string::npos);
  ASSERT_TRUE(out.find("211") != std::string::npos);
  ASSERT_TRUE(out.find("212") != std::string::npos);
  ASSERT_TRUE(out.find("213") != std::string::npos);
}

TEST(Array, Empty) {
  Array<Int16> field;

  Buffer::OwnedImpl data;
  // Write 0 elements into array.
  data.writeBEInt<uint16_t>(0);

  uint64_t pos = 0;
  uint64_t left = 2;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  ASSERT_THAT(pos, 2);
  ASSERT_THAT(left, 0);
  pos = 0;
  left = 2;
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 2);
  ASSERT_THAT(left, 0);

  auto out = field.toString();
  ASSERT_TRUE(out.find("Array of 0") != std::string::npos);
}

// Test situation when there is not enough data to read the length of the Array.
TEST(Array, NotEnoughDataForLength) {
  Array<Int16> field;

  Buffer::OwnedImpl data;
  // Data field is 2 bytes long. Write just one byte.
  data.writeBEInt<uint8_t>(1);

  uint64_t pos = 0;
  uint64_t left = 1;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationFailed);
}

// Test situation when there is not enough data in the buffer to read one of the elements
// in the array.
TEST(Array, NotEnoughDataForValues) {
  Array<Int32> field;

  Buffer::OwnedImpl data;
  // There will be 2 elements in the array.
  // The first element is 4 bytes long.
  // The second element should be 4 bytes long but is only 2 bytes long.
  data.writeBEInt<uint16_t>(2);
  data.writeBEInt<uint32_t>(101);
  data.writeBEInt<uint16_t>(102);

  uint64_t pos = 0;
  uint64_t left = 2 + 4 + 2;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationFailed);
}

// Repeated composite type tests.
TEST(Repeated, BasicTestWithStrings) {
  Repeated<String> field;

  Buffer::OwnedImpl data;
  // Write some data to simulate message header.
  // It will be ignored.
  data.writeBEInt<uint32_t>(101);
  data.writeBEInt<uint8_t>(102);
  uint64_t pos = 5;
  uint64_t left = 5;
  // Write the first string without terminating zero.
  data.add("test1");
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationFailed);
  left = 6;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationNeedMoreData);
  // Add terminating zero.
  data.writeBEInt<int8_t>(0);
  left = 5;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationFailed);
  left = 7;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationNeedMoreData);
  left = 6;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  // Add two additional strings
  data.add("test2");
  data.writeBEInt<uint8_t>(0);
  data.add("test3");
  data.writeBEInt<uint8_t>(0);
  pos = 5;
  left = 3 * 6 - 1;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationFailed);
  left = 3 * 6 + 1;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationNeedMoreData);
  left = 3 * 6;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  ASSERT_THAT(pos, 5 + 3 * 6);
  ASSERT_THAT(left, 0);
  pos = 5;
  left = 3 * 6;
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 5 + 3 * 6);
  ASSERT_THAT(left, 0);

  auto out = field.toString();
  ASSERT_TRUE(out.find("test1") != std::string::npos);
  ASSERT_TRUE(out.find("test2") != std::string::npos);
  ASSERT_TRUE(out.find("test3") != std::string::npos);
}

// Sequence composite type tests.
TEST(Sequence, Int32SingleValue) {
  Sequence<Int32> field;

  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(101);

  uint64_t pos = 0;
  uint64_t left = 4;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  ASSERT_THAT(pos, 4);
  ASSERT_THAT(left, 0);
  pos = 0;
  left = 4;
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 4);
  ASSERT_THAT(left, 0);

  auto out = field.toString();
  ASSERT_TRUE(out.find("101") != std::string::npos);
}

TEST(Sequence, Int16SingleValue) {
  Sequence<Int16> field;

  Buffer::OwnedImpl data;
  data.writeBEInt<uint16_t>(101);

  uint64_t pos = 0;
  uint64_t left = 2;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  ASSERT_THAT(pos, 2);
  ASSERT_THAT(left, 0);
  pos = 0;
  left = 2;
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 2);
  ASSERT_THAT(left, 0);

  auto out = field.toString();
  ASSERT_TRUE(out.find("101") != std::string::npos);
}

TEST(Sequence, BasicMultipleValues1) {
  Sequence<Int32, String> field;

  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(101);
  data.add("test");
  data.writeBEInt<uint8_t>(0);

  uint64_t pos = 0;
  uint64_t left = 4 + 5;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  ASSERT_THAT(pos, 4 + 5);
  ASSERT_THAT(left, 0);
  pos = 0;
  left = 4 + 5;
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 4 + 5);
  ASSERT_THAT(left, 0);

  auto out = field.toString();
  ASSERT_TRUE(out.find("101") != std::string::npos);
  ASSERT_TRUE(out.find("test") != std::string::npos);
}

TEST(Sequence, BasicMultipleValues2) {
  Sequence<Int32, Int16> field;

  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(100);
  data.writeBEInt<uint16_t>(101);

  uint64_t pos = 0;
  uint64_t left = 4 + 2;
  uint64_t expected_pos = left;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  ASSERT_THAT(pos, expected_pos);
  ASSERT_THAT(left, 0);
  pos = 0;
  left = 4 + 2;
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, expected_pos);
  ASSERT_THAT(left, 0);

  auto out = field.toString();
  ASSERT_TRUE(out.find("100") != std::string::npos);
  ASSERT_TRUE(out.find("101") != std::string::npos);
}

TEST(Sequence, BasicMultipleValues3) {
  Sequence<Int32, Int16, Int32, Int16> field;

  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(100);
  data.writeBEInt<uint16_t>(101);
  data.writeBEInt<uint32_t>(102);
  data.writeBEInt<uint16_t>(103);

  uint64_t pos = 0;
  uint64_t left = 4 + 2 + 4 + 2;
  uint64_t expected_pos = left;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationOK);
  ASSERT_THAT(pos, expected_pos);
  ASSERT_THAT(left, 0);
  pos = 0;
  left = 4 + 2 + 4 + 2;
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, expected_pos);
  ASSERT_THAT(left, 0);

  auto out = field.toString();
  ASSERT_TRUE(out.find("100") != std::string::npos);
  ASSERT_TRUE(out.find("101") != std::string::npos);
  ASSERT_TRUE(out.find("102") != std::string::npos);
  ASSERT_TRUE(out.find("103") != std::string::npos);
}

// Test versifies that read fails when reading of one element
// in Sequence fails.
TEST(Sequence, NotEnoughData) {
  Sequence<Int32, String> field;

  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(101);
  // Do not write terminating zero for the string.
  data.add("test");

  uint64_t pos = 0;
  uint64_t left = 4 + 4;
  ASSERT_THAT(field.validate(data, 0, pos, left), Message::ValidationFailed);
}

// Tests for Message interface and helper function createMsgBodyReader.
TEST(PostgresMessage, SingleFieldInt32) {
  std::unique_ptr<Message> msg = createMsgBodyReader<Int32>();

  Buffer::OwnedImpl data;
  // Validation of empty message should complain that there
  // is not enough data in the buffer.
  ASSERT_THAT(msg->validate(data, 0, 4), Message::ValidationNeedMoreData);

  data.writeBEInt<uint32_t>(12);

  // Simulate that message is longer than In32.
  ASSERT_THAT(msg->validate(data, 0, 5), Message::ValidationFailed);

  ASSERT_THAT(msg->validate(data, 0, 4), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, 4));
  auto out = msg->toString();
  ASSERT_THAT(out, "[12]");
}

TEST(PostgresMessage, SingleFieldInt16) {
  std::unique_ptr<Message> msg = createMsgBodyReader<Int16>();

  Buffer::OwnedImpl data;

  // Validation of empty message should complain that there
  // is not enough data in the buffer.
  ASSERT_THAT(msg->validate(data, 0, 2), Message::ValidationNeedMoreData);

  data.writeBEInt<uint16_t>(12);
  ASSERT_THAT(msg->validate(data, 0, 2), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, 2));
  auto out = msg->toString();
  ASSERT_THAT(out, "[12]");
}

TEST(PostgresMessage, SingleByteN) {
  std::unique_ptr<Message> msg = createMsgBodyReader<ByteN>();

  Buffer::OwnedImpl data;
  // Validation of empty message should complain that there
  // is not enough data in the buffer.
  ASSERT_THAT(msg->validate(data, 0, 4), Message::ValidationNeedMoreData);

  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint8_t>(1);
  data.writeBEInt<uint8_t>(2);
  data.writeBEInt<uint8_t>(3);
  data.writeBEInt<uint8_t>(4);
  const uint64_t length = 5 * 1;
  ASSERT_THAT(msg->validate(data, 0, length), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, length));
  auto out = msg->toString();
  ASSERT_TRUE(out.find("0") != std::string::npos); // NOLINT
  ASSERT_TRUE(out.find("1") != std::string::npos); // NOLINT
  ASSERT_TRUE(out.find("2") != std::string::npos); // NOLINT
  ASSERT_TRUE(out.find("3") != std::string::npos); // NOLINT
  ASSERT_TRUE(out.find("4") != std::string::npos); // NOLINT
}

TEST(PostgresMessage, MultipleValues1) {
  std::unique_ptr<Message> msg = createMsgBodyReader<Int32, Int16>();

  Buffer::OwnedImpl data;

  // Validation of empty message should complain that there
  // is not enough data in the buffer.
  ASSERT_THAT(msg->validate(data, 0, 4), Message::ValidationNeedMoreData);

  data.writeBEInt<uint32_t>(12);
  data.writeBEInt<uint16_t>(13);
  const uint64_t length = 4 + 2;
  ASSERT_THAT(msg->validate(data, 0, length), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, length));
  auto out = msg->toString();
  ASSERT_TRUE(out.find("12") != std::string::npos);
  ASSERT_TRUE(out.find("13") != std::string::npos);
}

TEST(PostgresMessage, MultipleValues2) {
  std::unique_ptr<Message> msg = createMsgBodyReader<Int16, Int32, Int16>();

  Buffer::OwnedImpl data;
  data.writeBEInt<uint16_t>(13);
  data.writeBEInt<uint32_t>(14);
  data.writeBEInt<uint16_t>(15);
  const uint64_t length = 2 + 4 + 2;
  ASSERT_THAT(msg->validate(data, 0, length), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, length));
  auto out = msg->toString();
  ASSERT_TRUE(out.find("13") != std::string::npos);
  ASSERT_TRUE(out.find("14") != std::string::npos);
  ASSERT_TRUE(out.find("15") != std::string::npos);
}

TEST(PostgresMessage, MultipleValues3) {
  std::unique_ptr<Message> msg = createMsgBodyReader<Int32, Int16, Int32, Int16>();

  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(12);
  data.writeBEInt<uint16_t>(13);
  data.writeBEInt<uint32_t>(14);
  data.writeBEInt<uint16_t>(15);
  const uint64_t length = 4 + 2 + 4 + 2;
  ASSERT_THAT(msg->validate(data, 0, length), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, length));
  auto out = msg->toString();
  ASSERT_TRUE(out.find("12") != std::string::npos);
  ASSERT_TRUE(out.find("13") != std::string::npos);
  ASSERT_TRUE(out.find("14") != std::string::npos);
  ASSERT_TRUE(out.find("15") != std::string::npos);
}

TEST(PostgresMessage, MultipleValues4) {
  std::unique_ptr<Message> msg = createMsgBodyReader<Int16, Int32, Int16, Int32, Int16>();

  Buffer::OwnedImpl data;
  data.writeBEInt<uint16_t>(13);
  data.writeBEInt<uint32_t>(14);
  data.writeBEInt<uint16_t>(15);
  data.writeBEInt<uint32_t>(16);
  data.writeBEInt<uint16_t>(17);
  const uint64_t length = 2 + 4 + 2 + 4 + 2;
  ASSERT_THAT(msg->validate(data, 0, length), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, length));
  auto out = msg->toString();
  ASSERT_TRUE(out.find("13") != std::string::npos);
  ASSERT_TRUE(out.find("14") != std::string::npos);
  ASSERT_TRUE(out.find("15") != std::string::npos);
  ASSERT_TRUE(out.find("16") != std::string::npos);
  ASSERT_TRUE(out.find("17") != std::string::npos);
}

TEST(PostgresMessage, MultipleValues5) {
  std::unique_ptr<Message> msg = createMsgBodyReader<Int32, Int16, Int32, Int16, Int32, Int16>();

  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(12);
  data.writeBEInt<uint16_t>(13);
  data.writeBEInt<uint32_t>(14);
  data.writeBEInt<uint16_t>(15);
  data.writeBEInt<uint32_t>(16);
  data.writeBEInt<uint16_t>(17);
  const uint64_t length = 4 + 2 + 4 + 2 + 4 + 2;
  ASSERT_THAT(msg->validate(data, 0, length), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, length));
  auto out = msg->toString();
  ASSERT_TRUE(out.find("12") != std::string::npos);
  ASSERT_TRUE(out.find("13") != std::string::npos);
  ASSERT_TRUE(out.find("14") != std::string::npos);
  ASSERT_TRUE(out.find("15") != std::string::npos);
  ASSERT_TRUE(out.find("16") != std::string::npos);
  ASSERT_TRUE(out.find("17") != std::string::npos);
}

TEST(PostgresMessage, MultipleValues6) {
  std::unique_ptr<Message> msg =
      createMsgBodyReader<String, Int32, Int16, Int32, Int16, Int32, Int16>();

  Buffer::OwnedImpl data;
  data.add("test");
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint32_t>(12);
  data.writeBEInt<uint16_t>(13);
  data.writeBEInt<uint32_t>(14);
  data.writeBEInt<uint16_t>(15);
  data.writeBEInt<uint32_t>(16);
  data.writeBEInt<uint16_t>(17);
  const uint64_t length = 5 + 4 + 2 + 4 + 2 + 4 + 2;
  ASSERT_THAT(msg->validate(data, 0, length), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, length));
  auto out = msg->toString();
  ASSERT_TRUE(out.find("test") != std::string::npos);
  ASSERT_TRUE(out.find("12") != std::string::npos);
  ASSERT_TRUE(out.find("13") != std::string::npos);
  ASSERT_TRUE(out.find("14") != std::string::npos);
  ASSERT_TRUE(out.find("15") != std::string::npos);
  ASSERT_TRUE(out.find("16") != std::string::npos);
  ASSERT_TRUE(out.find("17") != std::string::npos);
}

TEST(PostgresMessage, MultipleValues7) {
  std::unique_ptr<Message> msg = createMsgBodyReader<String, Array<Int32>>();

  Buffer::OwnedImpl data;
  data.add("test");
  data.writeBEInt<uint8_t>(0);

  // Array of 3 elements.
  data.writeBEInt<int16_t>(3);
  data.writeBEInt<uint32_t>(13);
  data.writeBEInt<uint32_t>(14);
  data.writeBEInt<uint32_t>(15);
  const uint64_t length = 5 + 2 + 3 * 4;
  ASSERT_THAT(msg->validate(data, 0, length), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, length));
  auto out = msg->toString();
  ASSERT_TRUE(out.find("test") != std::string::npos);
  ASSERT_TRUE(out.find("13") != std::string::npos);
  ASSERT_TRUE(out.find("14") != std::string::npos);
  ASSERT_TRUE(out.find("15") != std::string::npos);
}

TEST(PostgresMessage, ArraySet1) {
  std::unique_ptr<Message> msg = createMsgBodyReader<Array<Int16>>();

  Buffer::OwnedImpl data;
  // There will be 3 elements in the array.
  data.writeBEInt<int16_t>(3);
  data.writeBEInt<uint16_t>(13);
  data.writeBEInt<uint16_t>(14);
  data.writeBEInt<uint16_t>(15);
  const uint64_t length = 2 + 3 * 2;
  ASSERT_THAT(msg->validate(data, 0, length), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, length));
  auto out = msg->toString();
  ASSERT_TRUE(out.find("13") != std::string::npos);
  ASSERT_TRUE(out.find("14") != std::string::npos);
  ASSERT_TRUE(out.find("15") != std::string::npos);
}

TEST(PostgresMessage, ArraySet2) {
  std::unique_ptr<Message> msg = createMsgBodyReader<Array<VarByteN>, Int16>();

  Buffer::OwnedImpl data;
  // Array of 1 element of VarByteN.
  data.writeBEInt<int16_t>(1);
  // VarByteN of 5 bytes long.
  data.writeBEInt<int32_t>(5);
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint8_t>(1);
  data.writeBEInt<uint8_t>(2);
  data.writeBEInt<uint8_t>(3);
  data.writeBEInt<uint8_t>(114);

  // 16-bits value.
  data.writeBEInt<uint16_t>(115);
  const uint64_t length = 2 + 4 + 5 + 2;
  ASSERT_THAT(msg->validate(data, 0, length), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, length));
  auto out = msg->toString();
  ASSERT_TRUE(out.find("114") != std::string::npos);
  ASSERT_TRUE(out.find("115") != std::string::npos);
}

TEST(PostgresMessage, ArraySet3) {
  std::unique_ptr<Message> msg = createMsgBodyReader<Array<Int16>, Array<VarByteN>, Int16>();

  Buffer::OwnedImpl data;
  // There will be 3 elements in the array.
  data.writeBEInt<int16_t>(3);
  data.writeBEInt<uint16_t>(13);
  data.writeBEInt<uint16_t>(14);
  data.writeBEInt<uint16_t>(15);

  // Array of 1 element of VarByteN.
  data.writeBEInt<int16_t>(1);
  // VarByteN of 5 bytes long.
  data.writeBEInt<int32_t>(5);
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint8_t>(1);
  data.writeBEInt<uint8_t>(2);
  data.writeBEInt<uint8_t>(3);
  data.writeBEInt<uint8_t>(4);

  // 16-bits value.
  data.writeBEInt<uint16_t>(115);
  const uint64_t length = 2 + 3 * 2 + 2 + 4 + 5 + 2;
  ASSERT_THAT(msg->validate(data, 0, length), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, length));
  auto out = msg->toString();
  ASSERT_TRUE(out.find("13") != std::string::npos);
  ASSERT_TRUE(out.find("115") != std::string::npos);
}

TEST(PostgresMessage, ArraySet4) {
  std::unique_ptr<Message> msg = createMsgBodyReader<Array<VarByteN>, Array<Int16>>();

  Buffer::OwnedImpl data;
  // Array of 1 element of VarByteN.
  data.writeBEInt<int16_t>(1);
  // VarByteN of 5 bytes long.
  data.writeBEInt<int32_t>(5);
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint8_t>(111);
  data.writeBEInt<uint8_t>(2);
  data.writeBEInt<uint8_t>(3);
  data.writeBEInt<uint8_t>(4);

  // Array of 2 elements in the second array.
  data.writeBEInt<int16_t>(2);
  data.writeBEInt<uint16_t>(113);
  data.writeBEInt<uint16_t>(114);
  const uint64_t length = 2 + 4 + 5 + 2 + 2 * 2;
  ASSERT_THAT(msg->validate(data, 0, length), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, length));
  auto out = msg->toString();
  ASSERT_TRUE(out.find("111") != std::string::npos);
  ASSERT_TRUE(out.find("114") != std::string::npos);
}

TEST(PostgresMessage, ArraySet5) {
  std::unique_ptr<Message> msg = createMsgBodyReader<Array<Int16>, Array<VarByteN>, Array<Int16>>();

  Buffer::OwnedImpl data;
  // There will be 3 elements in the first array.
  data.writeBEInt<int16_t>(3);
  data.writeBEInt<uint16_t>(13);
  data.writeBEInt<uint16_t>(14);
  data.writeBEInt<uint16_t>(15);

  // Array of 1 element of VarByteN.
  data.writeBEInt<int16_t>(1);
  // VarByteN of 5 bytes long.
  data.writeBEInt<int32_t>(5);
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint8_t>(1);
  data.writeBEInt<uint8_t>(2);
  data.writeBEInt<uint8_t>(3);
  data.writeBEInt<uint8_t>(4);

  // Array of 2 elements in the third array.
  data.writeBEInt<int16_t>(2);
  data.writeBEInt<uint16_t>(113);
  data.writeBEInt<uint16_t>(114);
  const uint64_t length = 2 + 3 * 2 + 2 + 4 + 5 + 2 + 2 * 2;
  ASSERT_THAT(msg->validate(data, 0, length), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, length));
  auto out = msg->toString();
  ASSERT_TRUE(out.find("13") != std::string::npos);
  ASSERT_TRUE(out.find("114") != std::string::npos);
}

TEST(PostgresMessage, ArraySet6) {
  std::unique_ptr<Message> msg =
      createMsgBodyReader<String, Array<Int16>, Array<VarByteN>, Array<Int16>>();

  Buffer::OwnedImpl data;
  // Write string.
  data.add("test");
  data.writeBEInt<int8_t>(0);

  // There will be 3 elements in the first array.
  data.writeBEInt<int16_t>(3);
  data.writeBEInt<uint16_t>(13);
  data.writeBEInt<uint16_t>(14);
  data.writeBEInt<uint16_t>(15);

  // Array of 1 element of VarByteN.
  data.writeBEInt<int16_t>(1);
  // VarByteN of 5 bytes long.
  data.writeBEInt<int32_t>(5);
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint8_t>(1);
  data.writeBEInt<uint8_t>(2);
  data.writeBEInt<uint8_t>(3);
  data.writeBEInt<uint8_t>(4);

  // Array of 2 elements in the third array.
  data.writeBEInt<int16_t>(2);
  data.writeBEInt<uint16_t>(113);
  data.writeBEInt<uint16_t>(114);

  const uint64_t length = 5 + 2 + 3 * 2 + 2 + 4 + 5 + 2 + 2 * 2;
  ASSERT_THAT(msg->validate(data, 0, length), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, length));
  auto out = msg->toString();
  ASSERT_TRUE(out.find("test") != std::string::npos);
  ASSERT_TRUE(out.find("13") != std::string::npos);
  ASSERT_TRUE(out.find("114") != std::string::npos);
}

TEST(PostgresMessage, ArraySet7) {
  // Array of Sequences
  std::unique_ptr<Message> msg = createMsgBodyReader<Array<Sequence<String, Int16>>>();

  Buffer::OwnedImpl data;
  // There will be 3 sequences in the array.
  data.writeBEInt<int16_t>(3);

  // 1st sequence.
  data.add("seq1");
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint16_t>(111);

  // 2nd sequence.
  data.add("seq2");
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint16_t>(222);

  // 3rd sequence.
  data.add("seq3");
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint16_t>(333);

  const uint64_t length = 2 + 3 * (5 + 2);
  ASSERT_THAT(msg->validate(data, 0, length), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, length));
  auto out = msg->toString();
  ASSERT_TRUE(out.find("seq1") != std::string::npos);
  ASSERT_TRUE(out.find("seq2") != std::string::npos);
  ASSERT_TRUE(out.find("seq3") != std::string::npos);
  ASSERT_TRUE(out.find("111") != std::string::npos);
  ASSERT_TRUE(out.find("222") != std::string::npos);
  ASSERT_TRUE(out.find("333") != std::string::npos);
}

TEST(PostgresMessage, Repeated1) {
  std::unique_ptr<Message> msg = createMsgBodyReader<Repeated<String>>();

  Buffer::OwnedImpl data;
  // Write 3 strings.
  data.add("test1");
  data.writeBEInt<int8_t>(0);
  data.add("test2");
  data.writeBEInt<int8_t>(0);
  data.add("test3");
  data.writeBEInt<int8_t>(0);

  const uint64_t length = 3 * 6;
  ASSERT_THAT(msg->validate(data, 0, length), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, length));
  auto out = msg->toString();
  ASSERT_TRUE(out.find("test1") != std::string::npos);
  ASSERT_TRUE(out.find("test2") != std::string::npos);
  ASSERT_TRUE(out.find("test3") != std::string::npos);
}

TEST(PostgresMessage, Repeated2) {
  std::unique_ptr<Message> msg = createMsgBodyReader<Int32, Repeated<String>>();

  Buffer::OwnedImpl data;
  data.writeBEInt<int32_t>(115);
  // Write 3 strings.
  data.add("test1");
  data.writeBEInt<int8_t>(0);
  data.add("test2");
  data.writeBEInt<int8_t>(0);
  data.add("test3");
  data.writeBEInt<int8_t>(0);

  const uint64_t length = 4 + 3 * 6;
  ASSERT_THAT(msg->validate(data, 0, length), Message::ValidationOK);
  ASSERT_TRUE(msg->read(data, length));
  auto out = msg->toString();
  ASSERT_TRUE(out.find("115") != std::string::npos);
  ASSERT_TRUE(out.find("test1") != std::string::npos);
  ASSERT_TRUE(out.find("test2") != std::string::npos);
  ASSERT_TRUE(out.find("test3") != std::string::npos);
}

TEST(PostgresMessage, NotEnoughData) {
  std::unique_ptr<Message> msg = createMsgBodyReader<Int32, String>();
  Buffer::OwnedImpl data;
  // Write only 3 bytes into the buffer.
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint8_t>(1);
  data.writeBEInt<uint8_t>(2);

  ASSERT_THAT(msg->validate(data, 0, 4), Message::ValidationNeedMoreData);
  ASSERT_THAT(msg->validate(data, 0, 2), Message::ValidationFailed);
}

// Test checks validating a properly formatted message
// which starts at some offset in data buffer.
TEST(PostgresMessage, ValidateFromOffset) {
  std::unique_ptr<Message> msg = createMsgBodyReader<Int32, String>();
  Buffer::OwnedImpl data;

  // Write some data which should be skipped by validator.
  data.add("skip");
  data.writeBEInt<int8_t>(0);

  // Write valid data according to message syntax.
  data.writeBEInt<uint32_t>(110);
  data.add("test123");
  data.writeBEInt<int8_t>(0);

  // Skip first 5 bytes in the buffer.
  ASSERT_THAT(msg->validate(data, 5, 4 + 8), Message::ValidationOK);
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
