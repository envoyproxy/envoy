#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/postgres_proxy/postgres_message.h"

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
  uint64_t left = this->data_.length();
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
  uint64_t left = this->data_.length() - pos;
  ASSERT_FALSE(this->field_.read(this->data_, pos, left));
}

// Byte1 should format content as char.
TEST(Byte1, Formatting) {
  Byte1 field;

  Buffer::OwnedImpl data;
  data.add("I");

  uint64_t pos = 0;
  uint64_t left = 1;
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
  data.writeBEInt<uint8_t>(0);
  uint64_t pos = 0;
  uint64_t left = 5;
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 5);
  ASSERT_THAT(left, 0);

  auto out = field.toString();
  ASSERT_THAT(out, "[test]");
}

TEST(StringType, MultipleStrings) {
  String field;

  // Add 3 strings.
  Buffer::OwnedImpl data;
  data.add("test1");
  data.writeBEInt<uint8_t>(0);
  data.add("test2");
  data.writeBEInt<uint8_t>(0);
  data.add("test3");
  data.writeBEInt<uint8_t>(0);
  uint64_t pos = 0;
  uint64_t left = 3 * 6;

  // Read the first string.
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 1 * 6);
  ASSERT_THAT(left, 2 * 6);
  auto out = field.toString();
  ASSERT_THAT(out, "[test1]");

  // Read the second string.
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 2 * 6);
  ASSERT_THAT(left, 1 * 6);
  out = field.toString();
  ASSERT_THAT(out, "[test2]");

  // Read the third string.
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 3 * 6);
  ASSERT_THAT(left, 0);
  out = field.toString();
  ASSERT_THAT(out, "[test3]");
}

TEST(StringType, NoTerminatingByte) {
  String field;

  Buffer::OwnedImpl data;
  data.add("test");
  uint64_t pos = 0;
  uint64_t left = 4;
  ASSERT_FALSE(field.read(data, pos, left));
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
  uint64_t left = 10;
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 10);
  // One byte should be left in the buffer.
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
  ASSERT_FALSE(field.read(data, pos, left));
}

TEST(ByteN, Empty) {
  ByteN field;

  Buffer::OwnedImpl data;
  // Write nothing to data buffer.
  uint64_t pos = 0;
  uint64_t left = 0;
  ASSERT_TRUE(field.read(data, pos, left));

  auto out = field.toString();
  ASSERT_THAT(out, "[]");
}

// VarByteN type. It contains 4 bytes length field with value which follows.
TEST(VarByteN, BasicTest) {
  VarByteN field;

  Buffer::OwnedImpl data;
  // Write VarByteN with length equal to zero. No value follows.
  data.writeBEInt<uint32_t>(0);

  // Write value with 5 bytes.
  data.writeBEInt<uint32_t>(5);
  for (auto i = 0; i < 5; i++) {
    data.writeBEInt<uint8_t>(10 + i);
  }

  // Write special case value with length -1. No value follows.
  data.writeBEInt<int32_t>(-1);

  uint64_t pos = 0;
  uint64_t left = 4 + 4 + 5 + 4;
  uint64_t expected_left = left;

  // Read the first value.
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 4);
  expected_left -= 4;
  ASSERT_THAT(left, expected_left);
  auto out = field.toString();
  ASSERT_TRUE(out.find("0 bytes") != std::string::npos);

  // Read the second value.
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 4 + 4 + 5);
  expected_left -= (4 + 5);
  ASSERT_THAT(left, expected_left);
  out = field.toString();
  ASSERT_TRUE(out.find("5 bytes") != std::string::npos);
  ASSERT_TRUE(out.find("10 11 12 13 14") != std::string::npos);

  // Read the third value.
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 4 + 4 + 5 + 4);
  expected_left -= 4;
  ASSERT_THAT(left, expected_left);
  out = field.toString();
  ASSERT_TRUE(out.find("-1 bytes") != std::string::npos);
}

TEST(VarByteN, NotEnoughLengthData) {
  VarByteN field;

  Buffer::OwnedImpl data;
  // Write 3 bytes. Minimum for this type is 4 bytes of length.
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint8_t>(1);
  data.writeBEInt<uint8_t>(2);

  uint64_t pos = 0;
  uint64_t left = 3;
  ASSERT_FALSE(field.read(data, pos, left));
}

TEST(VarByteN, NotEnoughValueData) {
  VarByteN field;

  Buffer::OwnedImpl data;
  // Write length of the value to be 5 bytes, but supply only 4 bytes.
  data.writeBEInt<int32_t>(5);
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint8_t>(1);
  data.writeBEInt<uint8_t>(2);
  data.writeBEInt<uint8_t>(3);

  uint64_t pos = 0;
  uint64_t left = 5 + 4;
  ASSERT_FALSE(field.read(data, pos, left));
}

// Array composite type tests.
TEST(Array, SingleInt) {
  Array<Int32> field;

  Buffer::OwnedImpl data;
  // Write the number of elements in the array.
  data.writeBEInt<uint16_t>(1);
  data.writeBEInt<uint32_t>(123);

  uint64_t pos = 0;
  uint64_t left = 2 + 4;
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
  // Write 3 elements into array.
  data.writeBEInt<uint16_t>(3);
  data.writeBEInt<uint8_t>(211);
  data.writeBEInt<uint8_t>(212);
  data.writeBEInt<uint8_t>(213);

  uint64_t pos = 0;
  uint64_t left = 2 + 3 * 1;
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
  ASSERT_FALSE(field.read(data, pos, left));
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
  ASSERT_FALSE(field.read(data, pos, left));
}

// Repeated composite type tests.
TEST(Repeated, BasicTestWithStrings) {
  Repeated<String> field;

  Buffer::OwnedImpl data;
  // Write some data to simulate message header.
  // It will be ignored.
  data.writeBEInt<uint32_t>(101);
  data.writeBEInt<uint8_t>(102);
  // Now write 3 strings. Each terminated by zero byte.
  data.add("test1");
  data.writeBEInt<uint8_t>(0);
  data.add("test2");
  data.writeBEInt<uint8_t>(0);
  data.add("test3");
  data.writeBEInt<uint8_t>(0);
  uint64_t pos = 5;
  uint64_t left = 3 * 6;
  ASSERT_TRUE(field.read(data, pos, left));
  ASSERT_THAT(pos, 5 + 3 * 6);
  ASSERT_THAT(left, 0);

  auto out = field.toString();
  ASSERT_TRUE(out.find("test1") != std::string::npos);
  ASSERT_TRUE(out.find("test2") != std::string::npos);
  ASSERT_TRUE(out.find("test3") != std::string::npos);
}

// Test verifies that read fails when there is less
// bytes in the buffer than bytes needed to read to the end of the message.
TEST(Repeated, NotEnoughData) {
  Repeated<String> field;

  Buffer::OwnedImpl data;
  // Write some data to simulate message header.
  // It will be ignored.
  data.writeBEInt<uint32_t>(101);
  data.writeBEInt<uint8_t>(102);
  data.add("test");

  // "test" with terminating zero is 5 bytes.
  // Set "left" to indicate that 6 bytes are needed.
  uint64_t pos = 5;
  uint64_t left = 5 + 6;
  ASSERT_FALSE(field.read(data, pos, left));
}

// Test verifies that entire read fails when one of
// subordinate reads fails.
TEST(Repeated, NotEnoughDataForSecondString) {
  Repeated<String> field;

  Buffer::OwnedImpl data;
  // Write some data to simulate message header.
  // It will be ignored.
  data.writeBEInt<uint32_t>(101);
  data.writeBEInt<uint8_t>(102);
  // Now write 3 strings. Each terminated by zero byte.
  data.add("test1");
  data.writeBEInt<uint8_t>(0);
  data.add("test2");
  // Do not write terminating zero.
  // Read should fail here.
  uint64_t pos = 5;
  uint64_t left = 6 + 5;
  ASSERT_FALSE(field.read(data, pos, left));
}

// Sequence composite type tests.
TEST(Sequence, Int32SingleValue) {
  Sequence<Int32> field;

  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(101);

  uint64_t pos = 0;
  uint64_t left = 4;
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
  ASSERT_FALSE(field.read(data, pos, left));
}

// Tests for Message interface and helper function createMsgBodyReader.
TEST(PostgresMessage, SingleFieldInt32) {
  std::unique_ptr<Message> msg = createMsgBodyReader<Int32>();

  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(12);
  ASSERT_TRUE(msg->read(data, 4));
  auto out = msg->toString();
  ASSERT_THAT(out, "[12]");
}

TEST(PostgresMessage, SingleFieldInt16) {
  std::unique_ptr<Message> msg = createMsgBodyReader<Int16>();

  Buffer::OwnedImpl data;
  data.writeBEInt<uint16_t>(12);
  ASSERT_TRUE(msg->read(data, 2));
  auto out = msg->toString();
  ASSERT_THAT(out, "[12]");
}

TEST(PostgresMessage, SingleByteN) {
  std::unique_ptr<Message> msg = createMsgBodyReader<ByteN>();

  Buffer::OwnedImpl data;
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint8_t>(1);
  data.writeBEInt<uint8_t>(2);
  data.writeBEInt<uint8_t>(3);
  data.writeBEInt<uint8_t>(4);
  ASSERT_TRUE(msg->read(data, 5 * 1));
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
  data.writeBEInt<uint32_t>(12);
  data.writeBEInt<uint16_t>(13);
  ASSERT_TRUE(msg->read(data, 4 + 2));
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
  ASSERT_TRUE(msg->read(data, 2 + 4 + 2));
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
  ASSERT_TRUE(msg->read(data, 4 + 2 + 4 + 2));
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
  ASSERT_TRUE(msg->read(data, 2 + 4 + 2 + 4 + 2));
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
  ASSERT_TRUE(msg->read(data, 4 + 2 + 4 + 2 + 4 + 2));
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
  ASSERT_TRUE(msg->read(data, 5 + 4 + 2 + 4 + 2 + 4 + 2));
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
  ASSERT_TRUE(msg->read(data, 5 + 2 + 3 * 4));
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
  ASSERT_TRUE(msg->read(data, 2 + 3 * 2));
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

  ASSERT_TRUE(msg->read(data, 2 + 4 + 5 + 2));
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

  ASSERT_TRUE(msg->read(data, 2 + 3 * 2 + 2 + 4 + 5 + 2));
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

  ASSERT_TRUE(msg->read(data, 2 + 4 + 5 + 2 + 2 * 2));
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

  ASSERT_TRUE(msg->read(data, 2 + 3 * 2 + 2 + 4 + 5 + 2 + 2 * 2));
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

  ASSERT_TRUE(msg->read(data, 5 + 2 + 3 * 2 + 2 + 4 + 5 + 2 + 2 * 2));
  auto out = msg->toString();
  ASSERT_TRUE(out.find("test") != std::string::npos);
  ASSERT_TRUE(out.find("13") != std::string::npos);
  ASSERT_TRUE(out.find("114") != std::string::npos);
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

  ASSERT_TRUE(msg->read(data, 3 * 6));
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

  ASSERT_TRUE(msg->read(data, 4 + 3 * 6));
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

  ASSERT_FALSE(msg->read(data, 3));
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
