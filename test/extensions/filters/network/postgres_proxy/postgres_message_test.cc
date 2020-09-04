#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/postgres_proxy/postgres_message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

// Tests for individual types used in Postgres messages.
// Integer types

// Fixture class for testing Integer types.
template <typename T> class IntTest : public testing::Test {
public:
  T field_;
  Buffer::OwnedImpl data_;
  char buf_[32];
};

using IntTypes = ::testing::Types<Int32, Int16, Int8, Byte1>;
TYPED_TEST_SUITE(IntTest, IntTypes);

TYPED_TEST(IntTest, BasicRead) {
  this->data_.template writeBEInt<decltype(std::declval<TypeParam>().get())>(12);
  uint64_t pos = 0;
  uint64_t left = this->data_.length();
  auto result = this->field_.read(this->data_, pos, left);
  ASSERT_TRUE(result);
  auto out = this->field_.to_string();

  sprintf(this->buf_, this->field_.getFormat(), 12);
  ASSERT_THAT(out, this->buf_);
  // pos should be moved forward by the number of bytes read
  ASSERT_THAT(pos, sizeof(TypeParam));

  // Make sure that all bytes have been read from the buffer.
  ASSERT_THAT(left, 0);
}

TYPED_TEST(IntTest, ReadWithLeftovers) {
  this->data_.template writeBEInt<decltype(std::declval<TypeParam>().get())>(12);
  // write 1 byte more
  this->data_.template writeBEInt<uint8_t>(11);
  uint64_t pos = 0;
  uint64_t left = this->data_.length();
  auto result = this->field_.read(this->data_, pos, left);
  ASSERT_TRUE(result);
  auto out = this->field_.to_string();
  sprintf(this->buf_, this->field_.getFormat(), 12);
  ASSERT_THAT(out, this->buf_);
  // pos should be moved forward by the number of bytes read
  ASSERT_THAT(pos, sizeof(TypeParam));

  // Make sure that all bytes have been read from the buffer.
  ASSERT_THAT(left, 1);
}

TYPED_TEST(IntTest, ReadAtOffset) {
  // write 1 byte before the actual value
  this->data_.template writeBEInt<uint8_t>(11);
  this->data_.template writeBEInt<decltype(std::declval<TypeParam>().get())>(12);
  uint64_t pos = 1;
  uint64_t left = this->data_.length() - 1;
  auto result = this->field_.read(this->data_, pos, left);
  ASSERT_TRUE(result);
  auto out = this->field_.to_string();
  sprintf(this->buf_, this->field_.getFormat(), 12);
  ASSERT_THAT(out, this->buf_);
  // pos should be moved forward by the number of bytes read
  ASSERT_THAT(pos, 1 + sizeof(TypeParam));
  // Nothing should be left to read
  ASSERT_THAT(left, 0);
}

TYPED_TEST(IntTest, NotEnoughData) {
  this->data_.template writeBEInt<decltype(std::declval<TypeParam>().get())>(12);
  // Start from offset 1. There is not enough data in the buffer for the required type.
  uint64_t pos = 1;
  uint64_t left = this->data_.length() - pos;
  auto result = this->field_.read(this->data_, pos, left);
  ASSERT_FALSE(result);
}

// Tests for String type.
TEST(StringType, SingleString) {
  String field;

  Buffer::OwnedImpl data;
  data.add("test");
  data.writeBEInt<uint8_t>(0);
  uint64_t pos = 0;
  uint64_t left = 5;
  auto result = field.read(data, pos, left);
  ASSERT_TRUE(result);
  ASSERT_THAT(pos, 5);
  ASSERT_THAT(left, 0);

  auto out = field.to_string();
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
  auto result = field.read(data, pos, left);
  ASSERT_TRUE(result);
  ASSERT_THAT(pos, 1 * 6);
  ASSERT_THAT(left, 2 * 6);
  auto out = field.to_string();
  ASSERT_THAT(out, "[test1]");

  // Read the second string.
  result = field.read(data, pos, left);
  ASSERT_TRUE(result);
  ASSERT_THAT(pos, 2 * 6);
  ASSERT_THAT(left, 1 * 6);
  out = field.to_string();
  ASSERT_THAT(out, "[test2]");

  // Read the third string.
  result = field.read(data, pos, left);
  ASSERT_TRUE(result);
  ASSERT_THAT(pos, 3 * 6);
  ASSERT_THAT(left, 0);
  out = field.to_string();
  ASSERT_THAT(out, "[test3]");
}

TEST(StringType, NoTerminatingByte) {
  String field;

  Buffer::OwnedImpl data;
  data.add("test");
  uint64_t pos = 0;
  uint64_t left = 4;
  auto result = field.read(data, pos, left);
  ASSERT_FALSE(result);
}

// ByteN type is always placed at the end of Postgres message.
// There is no explicit message length. Length must be deduced from
// "length" field on Postgres message.
TEST(ByteN, BasicTest) {
  ByteN field;

  Buffer::OwnedImpl data;
  // Write 11 bytes. We will read only 10 to make sure
  // that len is used, not buffer's length.
  for (auto i = 0; i < 10; i++) {
    data.writeBEInt<uint8_t>(i);
  }
  uint64_t pos = 0;
  uint64_t len = 10;
  auto result = field.read(data, pos, len);
  ASSERT_TRUE(result);
  ASSERT_THAT(pos, 10);
  ASSERT_THAT(len, 0);

  auto out = field.to_string();
  ASSERT_THAT(out, "[00 01 02 03 04 05 06 07 08 09]");
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
  uint64_t len = 4 + 4 + 5 + 4;
  uint64_t expected_len = len;

  // Read the first value.
  auto result = field.read(data, pos, len);
  ASSERT_TRUE(result);
  ASSERT_THAT(pos, 4);
  expected_len -= 4;
  ASSERT_THAT(len, expected_len);
  auto out = field.to_string();
  ASSERT_TRUE(out.find("0 bytes") != std::string::npos);

  // Read the second value.
  result = field.read(data, pos, len);
  ASSERT_TRUE(result);
  ASSERT_THAT(pos, 4 + 4 + 5);
  expected_len -= (4 + 5);
  ASSERT_THAT(len, expected_len);
  out = field.to_string();
  ASSERT_TRUE(out.find("5 bytes") != std::string::npos);
  ASSERT_TRUE(out.find("10 11 12 13 14") != std::string::npos);

  // Read the third value.
  result = field.read(data, pos, len);
  ASSERT_TRUE(result);
  ASSERT_THAT(pos, 4 + 4 + 5 + 4);
  expected_len -= 4;
  ASSERT_THAT(len, expected_len);
  out = field.to_string();
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
  uint64_t len = 3;
  auto result = field.read(data, pos, len);
  ASSERT_FALSE(result);
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
  uint64_t len = 5 + 4;
  auto result = field.read(data, pos, len);
  ASSERT_FALSE(result);
}

// Test basic 2 fields message
TEST(PostgresMessage, DISABLED_Basic) {
  Sequence<> msg;

  auto out = msg.to_string();
  ASSERT_THAT(out, "");
}

TEST(PostgresMessage, SingleField) {
  // Sequence<Int32> msg('B', 5);

  // std::unique_ptr<MessageI> msg = std::make_unique<Sequence<Int32>>('B', 5);
  std::unique_ptr<MessageI> msg = createMsg<Int32>();

  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(12);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[12]");
}

TEST(PostgresMessage, SingleStringViaBind) {
  auto f = std::bind(createMsg<Int32>);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(12);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[12]");
}

TEST(PostgresMessage, SingleByte1) {
  auto f = std::bind(createMsg<Byte1>);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;
  data.writeBEInt<uint8_t>('S');
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[S]");
}

TEST(PostgresMessage, SingleByteN) {
  auto f = std::bind(createMsg<ByteN>);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint8_t>(1);
  data.writeBEInt<uint8_t>(2);
  data.writeBEInt<uint8_t>(3);
  data.writeBEInt<uint8_t>(4);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[00 01 02 03 04]");
}

TEST(PostgresMessage, EmptyArray) {
  auto f = std::bind(createMsg<Array<Int32>>);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;

  data.writeBEInt<uint16_t>(0);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[Array of 0:{}]");
}

TEST(PostgresMessage, NoEmptyArrayOf32bitInts) {
  auto f = std::bind(createMsg<Array<Int32>>);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;

  // Add 5 Int32 elements
  data.writeBEInt<uint16_t>(5);
  // Elements
  data.writeBEInt<uint32_t>(0);
  data.writeBEInt<uint32_t>(1);
  data.writeBEInt<uint32_t>(2);
  data.writeBEInt<uint32_t>(3);
  data.writeBEInt<uint32_t>(4);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[Array of 5:{[[00]][[01]][[02]][[03]][[04]]}]");
}

TEST(PostgresMessage, NoEmptyArrayOf16bitInts) {
  auto f = std::bind(createMsg<Array<Int16>>);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;

  // Add 5 Int32 elements
  data.writeBEInt<uint16_t>(5);
  // Elements
  data.writeBEInt<uint16_t>(0);
  data.writeBEInt<uint16_t>(1);
  data.writeBEInt<uint16_t>(2);
  data.writeBEInt<uint16_t>(3);
  data.writeBEInt<uint16_t>(4);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[Array of 5:{[[00]][[01]][[02]][[03]][[04]]}]");
}

TEST(PostgresMessage, NoEmptyArrayOf8bitInts) {
  auto f = std::bind(createMsg<Array<Int8>>);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;

  // Add 5 Int32 elements
  data.writeBEInt<uint16_t>(5);
  // Elements
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint8_t>(1);
  data.writeBEInt<uint8_t>(2);
  data.writeBEInt<uint8_t>(3);
  data.writeBEInt<uint8_t>(4);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[Array of 5:{[[00]][[01]][[02]][[03]][[04]]}]");
}

TEST(PostgresMessage, NoEmptyArrayOfStruct) {
  auto f = std::bind(createMsg<Array<Sequence<Int8>>>);
  // std::unique_ptr<MessageI> msg = std::make_unique<Array<Sequence<Int32>>>('B', 5);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;

  // Add 5 Int32 elements
  data.writeBEInt<uint16_t>(5);
  // Elements
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint8_t>(1);
  data.writeBEInt<uint8_t>(2);
  data.writeBEInt<uint8_t>(3);
  data.writeBEInt<uint8_t>(4);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[Array of 5:{[[00]][[01]][[02]][[03]][[04]]}]");
}

TEST(PostgresMessage, NoEmptyArrayOfStruct1) {
  auto f = std::bind(createMsg<Array<Sequence<Int8, Int16>>>);
  // std::unique_ptr<MessageI> msg = std::make_unique<Array<Sequence<Int32>>>('B', 5);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;

  // Add 5 Int32 elements
  data.writeBEInt<uint16_t>(5);
  // Elements
  data.writeBEInt<uint8_t>(0);
  data.writeBEInt<uint16_t>(100);
  data.writeBEInt<uint8_t>(1);
  data.writeBEInt<uint16_t>(101);
  data.writeBEInt<uint8_t>(2);
  data.writeBEInt<uint16_t>(102);
  data.writeBEInt<uint8_t>(3);
  data.writeBEInt<uint16_t>(103);
  data.writeBEInt<uint8_t>(4);
  data.writeBEInt<uint16_t>(104);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[Array of 5:{[[00][100]][[01][101]][[02][102]][[03][103]][[04][104]]}]");
}

TEST(PostgresMessage, EmptySingleByteN) {
  auto f = std::bind(createMsg<ByteN>);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[]");
}
TEST(PostgresMessage, Byte1AndString) {
  auto f = std::bind(createMsg<Byte1, String>);

  std::unique_ptr<MessageI> msg = f();
  Buffer::OwnedImpl data;
  data.writeBEInt<uint8_t>('S');
  data.add("test");
  data.writeBEInt<uint32_t>(0);
  msg->read(data);
  auto out = msg->to_string();
  ASSERT_THAT(out, "[S][test]");
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
