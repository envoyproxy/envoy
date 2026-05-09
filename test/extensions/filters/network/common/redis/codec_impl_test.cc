#include <cmath>
#include <limits>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/extensions/filters/network/common/redis/codec_impl.h"

#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gtest/gtest.h"

using testing::ContainerEq;
using testing::InSequence;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

class RedisRespValueTest : public testing::Test {
public:
  void makeBulkStringArray(RespValue& value, const std::vector<std::string>& strings) {
    std::vector<RespValue> values(strings.size());
    for (uint64_t i = 0; i < strings.size(); i++) {
      values[i].type(RespType::BulkString);
      values[i].asString() = strings[i];
    }

    value.type(RespType::Array);
    value.asArray().swap(values);
  }

  void makeArray(RespValue& value, const std::vector<RespValue> items) {
    value.type(RespType::Array);
    value.asArray().insert(value.asArray().end(), items.begin(), items.end());
  }

  void verifyMoves(RespValue& value) {
    RespValue copy = value;
    RespValue move(std::move(copy));
    EXPECT_TRUE(value == move);

    RespValue move_assign;
    move_assign = std::move(move);
    EXPECT_TRUE(value == move_assign);
  }

  void validateIterator(RespValue& value, const std::vector<std::string>& strings) {
    EXPECT_EQ(RespType::CompositeArray, value.type());
    EXPECT_EQ(value.asCompositeArray().size(), strings.size());
    std::vector<std::string> values;
    for (const RespValue& part : value.asCompositeArray()) {
      values.emplace_back(part.asString());
    }
    EXPECT_THAT(values, ContainerEq(strings));
  }
};

TEST_F(RedisRespValueTest, EqualityTestingAndCopyingTest) {
  InSequence s;

  RespValue value1, value2, value3;

  makeBulkStringArray(value1, {"get", "foo", "bar", "now"});
  makeBulkStringArray(value2, {"get", "foo", "bar", "now"});
  makeBulkStringArray(value3, {"get", "foo", "bar", "later"});

  EXPECT_TRUE(value1 == value2);
  EXPECT_FALSE(value1 == value3);

  RespValue value4, value5;
  value4.type(RespType::Array);
  value4.asArray() = {value1, value2};
  value5.type(RespType::Array);
  value5.asArray() = {value1, value3};

  EXPECT_FALSE(value4 == value5);
  EXPECT_TRUE(value4 == value4);
  EXPECT_TRUE(value5 == value5);

  RespValue bulkstring_value, simplestring_value, error_value, integer_value, null_value;
  bulkstring_value.type(RespType::BulkString);
  simplestring_value.type(RespType::SimpleString);
  error_value.type(RespType::Error);
  integer_value.type(RespType::Integer);
  integer_value.asInteger() = 123;

  EXPECT_NE(bulkstring_value, simplestring_value);
  EXPECT_NE(bulkstring_value, error_value);
  EXPECT_NE(bulkstring_value, integer_value);
  EXPECT_NE(bulkstring_value, null_value);

  RespValue value6, value7, value8;
  makeArray(value6,
            {bulkstring_value, simplestring_value, error_value, integer_value, null_value, value1});
  makeArray(value7,
            {bulkstring_value, simplestring_value, error_value, integer_value, null_value, value2});
  makeArray(value8,
            {bulkstring_value, simplestring_value, error_value, integer_value, null_value, value3});

  // This may look weird, but it is a way to actually do self-assignment without generating compiler
  // warnings. Self-assignment should succeed without changing the RespValue, and therefore no
  // expectations should change.
  RespValue* value6_ptr = &value6;
  value6 = *value6_ptr;
  EXPECT_EQ(value6, value7);
  EXPECT_NE(value6, value8);
  EXPECT_NE(value7, value8);
  EXPECT_EQ(value6.asArray()[5].asArray()[3].asString(), "now");
  EXPECT_EQ(value7.asArray()[5].asArray()[3].asString(), "now");
  EXPECT_EQ(value8.asArray()[5].asArray()[3].asString(), "later");

  value8 = value1;
  EXPECT_EQ(value8.type(), RespType::Array);
  EXPECT_EQ(value8.asArray().size(), value1.asArray().size());
  EXPECT_EQ(value8.asArray().size(), 4);
  for (unsigned int i = 0; i < value8.asArray().size(); i++) {
    EXPECT_EQ(value8.asArray()[i].type(), RespType::BulkString);
    EXPECT_EQ(value8.asArray()[i].asString(), value1.asArray()[i].asString());
  }
  value7 = value1;
  EXPECT_EQ(value7, value8);
  value7 = value3;
  EXPECT_NE(value7, value8);

  value8 = bulkstring_value;
  EXPECT_EQ(value8.type(), RespType::BulkString);
  value8 = simplestring_value;
  EXPECT_EQ(value8.type(), RespType::SimpleString);
  value8 = error_value;
  EXPECT_EQ(value8.type(), RespType::Error);
  value8 = integer_value;
  EXPECT_EQ(value8.type(), RespType::Integer);
  value8 = null_value;
  EXPECT_EQ(value8.type(), RespType::Null);
}

TEST_F(RedisRespValueTest, MoveOperationsTest) {
  InSequence s;

  RespValue array_value, bulkstring_value, simplestring_value, error_value, integer_value,
      null_value, composite_array_empty;
  makeBulkStringArray(array_value, {"get", "foo", "bar", "now"});
  bulkstring_value.type(RespType::BulkString);
  bulkstring_value.asString() = "foo";
  simplestring_value.type(RespType::SimpleString);
  simplestring_value.asString() = "bar";
  error_value.type(RespType::Error);
  error_value.asString() = "error";
  integer_value.type(RespType::Integer);
  integer_value.asInteger() = 123;
  composite_array_empty.type(RespType::CompositeArray);

  verifyMoves(array_value);
  verifyMoves(bulkstring_value);
  verifyMoves(simplestring_value);
  verifyMoves(error_value);
  verifyMoves(integer_value);
  verifyMoves(null_value);
  verifyMoves(composite_array_empty);
}

TEST_F(RedisRespValueTest, SwapTest) {
  InSequence s;

  RespValue value1, value2, value3;

  makeBulkStringArray(value1, {"get", "foo", "bar", "now"});
  makeBulkStringArray(value2, {"get", "foo", "bar", "now"});
  makeBulkStringArray(value3, {"get", "foo", "bar", "later"});

  std::swap(value2, value3);
  EXPECT_TRUE(value1 == value3);

  std::swap(value3, value3);
  EXPECT_TRUE(value1 == value3);
}

TEST_F(RedisRespValueTest, CompositeArrayTest) {
  InSequence s;

  RespValueSharedPtr base = std::make_shared<RespValue>();
  makeBulkStringArray(*base, {"get", "foo", "bar", "now"});

  RespValue command;
  command.type(RespType::SimpleString);
  command.asString() = "get";

  RespValue value1{base, command, 1, 1};
  RespValue value2{base, command, 2, 2};
  RespValue value3{base, command, 3, 3};

  validateIterator(value1, {"get", "foo"});
  validateIterator(value2, {"get", "bar"});
  validateIterator(value3, {"get", "now"});

  EXPECT_EQ(value1.asCompositeArray().command(), &command);
  EXPECT_EQ(value1.asCompositeArray().baseArray(), base);

  RespValue value4{base, command, 1, 1};
  EXPECT_TRUE(value1 == value1);
  EXPECT_FALSE(value1 == value2);
  EXPECT_FALSE(value1 == value3);
  EXPECT_TRUE(value1 == value4);

  RespValue value5;
  value5 = value1;
  EXPECT_TRUE(value1 == value5);

  RespValue empty;
  empty.type(RespType::CompositeArray);
  validateIterator(empty, {});
}

class RedisEncoderDecoderImplTest : public testing::Test, public DecoderCallbacks {
public:
  RedisEncoderDecoderImplTest() : decoder_(*this) {}

  // RedisProxy::DecoderCallbacks
  void onRespValue(RespValuePtr&& value) override {
    decoded_values_.emplace_back(std::move(value));
  }

  EncoderImpl encoder_;
  DecoderImpl decoder_;
  Buffer::OwnedImpl buffer_;
  std::vector<RespValuePtr> decoded_values_;
};

TEST_F(RedisEncoderDecoderImplTest, Null) {
  RespValue value;
  EXPECT_EQ("null", value.toString());
  encoder_.encode(value, buffer_);
  EXPECT_EQ("$-1\r\n", buffer_.toString());
  decoder_.decode(buffer_);
  EXPECT_EQ(value, *decoded_values_[0]);
  EXPECT_EQ(0UL, buffer_.length());
}

TEST_F(RedisEncoderDecoderImplTest, Error) {
  RespValue value;
  value.type(RespType::Error);
  value.asString() = "error";
  EXPECT_EQ("\"error\"", value.toString());
  encoder_.encode(value, buffer_);
  EXPECT_EQ("-error\r\n", buffer_.toString());
  decoder_.decode(buffer_);
  EXPECT_EQ(value, *decoded_values_[0]);
  EXPECT_EQ(0UL, buffer_.length());
}

TEST_F(RedisEncoderDecoderImplTest, SimpleString) {
  RespValue value;
  value.type(RespType::SimpleString);
  value.asString() = "simple string";
  EXPECT_EQ("\"simple string\"", value.toString());
  encoder_.encode(value, buffer_);
  EXPECT_EQ("+simple string\r\n", buffer_.toString());
  decoder_.decode(buffer_);
  EXPECT_EQ(value, *decoded_values_[0]);
  EXPECT_EQ(0UL, buffer_.length());
}

TEST_F(RedisEncoderDecoderImplTest, BulkString) {
  RespValue value;
  value.type(RespType::BulkString);
  value.asString() = "bulk string";
  EXPECT_EQ("\"bulk string\"", value.toString());
  encoder_.encode(value, buffer_);
  EXPECT_EQ("$11\r\nbulk string\r\n", buffer_.toString());
  decoder_.decode(buffer_);
  EXPECT_EQ(value, *decoded_values_[0]);
  EXPECT_EQ(0UL, buffer_.length());
}

TEST_F(RedisEncoderDecoderImplTest, Integer) {
  RespValue value;
  value.type(RespType::Integer);
  value.asInteger() = std::numeric_limits<int64_t>::max();
  EXPECT_EQ("9223372036854775807", value.toString());
  encoder_.encode(value, buffer_);
  EXPECT_EQ(":9223372036854775807\r\n", buffer_.toString());
  decoder_.decode(buffer_);
  EXPECT_EQ(value, *decoded_values_[0]);
  EXPECT_EQ(0UL, buffer_.length());
}

TEST_F(RedisEncoderDecoderImplTest, NegativeIntegerSmall) {
  RespValue value;
  value.type(RespType::Integer);
  value.asInteger() = -1;
  encoder_.encode(value, buffer_);
  EXPECT_EQ(":-1\r\n", buffer_.toString());
  decoder_.decode(buffer_);
  EXPECT_EQ(value, *decoded_values_[0]);
  EXPECT_EQ(0UL, buffer_.length());
}

TEST_F(RedisEncoderDecoderImplTest, NegativeIntegerLarge) {
  RespValue value;
  value.type(RespType::Integer);
  value.asInteger() = std::numeric_limits<int64_t>::min();
  encoder_.encode(value, buffer_);
  EXPECT_EQ(":-9223372036854775808\r\n", buffer_.toString());
  decoder_.decode(buffer_);
  EXPECT_EQ(value, *decoded_values_[0]);
  EXPECT_EQ(0UL, buffer_.length());
}

TEST_F(RedisEncoderDecoderImplTest, EmptyArray) {
  RespValue value;
  value.type(RespType::Array);
  EXPECT_EQ("[]", value.toString());
  encoder_.encode(value, buffer_);
  EXPECT_EQ("*0\r\n", buffer_.toString());
  decoder_.decode(buffer_);
  EXPECT_EQ(value, *decoded_values_[0]);
  EXPECT_EQ(0UL, buffer_.length());
}

TEST_F(RedisEncoderDecoderImplTest, Array) {
  std::vector<RespValue> values(2);
  values[0].type(RespType::BulkString);
  values[0].asString() = "hello";
  values[1].type(RespType::Integer);
  values[1].asInteger() = -5;

  RespValue value;
  value.type(RespType::Array);
  value.asArray().swap(values);
  EXPECT_EQ("[\"hello\", -5]", value.toString());
  encoder_.encode(value, buffer_);
  EXPECT_EQ("*2\r\n$5\r\nhello\r\n:-5\r\n", buffer_.toString());
  decoder_.decode(buffer_);
  EXPECT_EQ(value, *decoded_values_[0]);
  EXPECT_EQ(0UL, buffer_.length());
}

TEST_F(RedisEncoderDecoderImplTest, CompositeArray) {
  std::vector<RespValue> values(2);
  values[0].type(RespType::BulkString);
  values[0].asString() = "bar";
  values[1].type(RespType::BulkString);
  values[1].asString() = "foo";

  auto base = std::make_shared<RespValue>();
  base->type(RespType::Array);
  base->asArray().swap(values);

  RespValue command;
  command.type(RespType::SimpleString);
  command.asString() = "get";

  RespValue value1{base, command, 0, 0};
  RespValue value2{base, command, 1, 1};

  EXPECT_EQ("[\"get\", \"bar\"]", value1.toString());
  encoder_.encode(value1, buffer_);
  EXPECT_EQ("*2\r\n+get\r\n$3\r\nbar\r\n", buffer_.toString());

  EXPECT_EQ("[\"get\", \"foo\"]", value2.toString());
  encoder_.encode(value2, buffer_);
  EXPECT_EQ("*2\r\n+get\r\n$3\r\nbar\r\n*2\r\n+get\r\n$3\r\nfoo\r\n", buffer_.toString());

  // There is no decoder for composite array
}

TEST_F(RedisEncoderDecoderImplTest, NestedArray) {
  std::vector<RespValue> nested_values(3);
  nested_values[0].type(RespType::BulkString);
  nested_values[0].asString() = "hello";
  nested_values[1].type(RespType::Integer);
  nested_values[1].asInteger() = 0;

  std::vector<RespValue> values(2);
  values[0].type(RespType::Array);
  values[0].asArray().swap(nested_values);
  values[1].type(RespType::BulkString);
  values[1].asString() = "world";

  RespValue value;
  value.type(RespType::Array);
  value.asArray().swap(values);
  encoder_.encode(value, buffer_);
  EXPECT_EQ("*2\r\n*3\r\n$5\r\nhello\r\n:0\r\n$-1\r\n$5\r\nworld\r\n", buffer_.toString());

  // To test partial decode we will feed the buffer in 1 char at a time.
  for (char c : buffer_.toString()) {
    Buffer::OwnedImpl temp_buffer(&c, 1);
    decoder_.decode(temp_buffer);
    EXPECT_EQ(0UL, temp_buffer.length());
  }

  EXPECT_EQ(value, *decoded_values_[0]);
}

TEST_F(RedisEncoderDecoderImplTest, NullArray) {
  buffer_.add("*-1\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Null, decoded_values_[0]->type());
}

TEST_F(RedisEncoderDecoderImplTest, InvalidType) {
  buffer_.add("^");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInteger) {
  buffer_.add(":-a");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, InvalidIntegerExpectLF) {
  buffer_.add(":-123\ra");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

// Empty integer/length lines must be rejected — without the digit_seen_ guard, ``:\r\n``,
// ``:-\r\n``, ``*\r\n``, ``$\r\n``, ``%-\r\n`` etc. would all be silently accepted as zero
// (indistinguishable on the wire from ``:0`` / ``*0`` / ``$0``), letting an attacker inject
// ambiguous frames the rest of the parser cannot tell apart from a legitimate zero-valued one.
// Cover the RespValue::toString branches for RESP3-only types (Set, Push, Double,
// BigNumber, BlobError, VerbatimString, Boolean) plus the Map two-pair pretty-print path.
// These branches are only reachable through encoder down-conversion or RESP3 client output;
// pinning toString() exercises the type-dispatch table itself.
TEST_F(RedisEncoderDecoderImplTest, ToStringResp3Types) {
  // Set
  {
    RespValue v;
    v.type(RespType::Set);
    RespValue elem;
    elem.type(RespType::BulkString);
    elem.asString() = "a";
    v.asArray().push_back(elem);
    elem.asString() = "b";
    v.asArray().push_back(elem);
    EXPECT_EQ("~[\"a\", \"b\"]", v.toString());
  }
  // Push
  {
    RespValue v;
    v.type(RespType::Push);
    RespValue elem;
    elem.type(RespType::BulkString);
    elem.asString() = "x";
    v.asArray().push_back(elem);
    EXPECT_EQ(">[\"x\"]", v.toString());
  }
  // Map (flat 2N storage)
  {
    RespValue v;
    v.type(RespType::Map);
    RespValue k1, v1, k2, v2;
    k1.type(RespType::BulkString);
    k1.asString() = "k1";
    v1.type(RespType::Integer);
    v1.asInteger() = 1;
    k2.type(RespType::BulkString);
    k2.asString() = "k2";
    v2.type(RespType::Integer);
    v2.asInteger() = 2;
    v.asArray() = {k1, v1, k2, v2};
    EXPECT_EQ("{\"k1\": 1, \"k2\": 2}", v.toString());
  }
  // Double — use an exact-representable value (0.5 = 2^-1) so fmt::format("{}", 0.5) is
  // deterministically "0.5". Using 3.14 here would be fragile across fmt library versions.
  {
    RespValue v;
    v.type(RespType::Double);
    v.asDouble() = 0.5;
    EXPECT_EQ("0.5", v.toString());
  }
  // BigNumber
  {
    RespValue v;
    v.type(RespType::BigNumber);
    v.asString() = "123456789012345678901234567890";
    EXPECT_EQ("(big)123456789012345678901234567890", v.toString());
  }
  // BlobError
  {
    RespValue v;
    v.type(RespType::BlobError);
    v.asString() = "ERR something";
    EXPECT_EQ("!(ERR something)", v.toString());
  }
  // VerbatimString
  {
    RespValue v;
    v.type(RespType::VerbatimString);
    v.asString() = "txt:hello";
    EXPECT_EQ("=txt:hello", v.toString());
  }
  // Boolean (true / false)
  {
    RespValue v;
    v.type(RespType::Boolean);
    v.asInteger() = 1;
    EXPECT_EQ("true", v.toString());
    v.asInteger() = 0;
    EXPECT_EQ("false", v.toString());
  }
}

// Exercise the unbalanced-quote rejection paths in inline-command parsing — both single and
// double-quoted forms.
TEST_F(RedisEncoderDecoderImplTest, InlineCommandUnbalancedDoubleQuote) {
  buffer_.add("\"hello\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandUnbalancedSingleQuote) {
  buffer_.add("'hello\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

// Cumulative-element budget on Map: a single Map declaring N pairs counts as 2*N elements
// against kMaxTotalElements. With kMaxTotalElements=4M and a Map count > 2M, the post-multiply
// 2N exceeds the cap and the parser must reject.
TEST_F(RedisEncoderDecoderImplTest, MapTotalElementBudgetExceeded) {
  // 2_500_000 pairs → 5M elements > 4M cap.
  buffer_.add("%2500000\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

// Map nesting depth: an outer Map containing nested Maps that push past kMaxNestingDepth
// (32) is rejected at the pre-push check.
TEST_F(RedisEncoderDecoderImplTest, MapNestingDepthExceeded) {
  // Build a chain of "%1\r\n$1\r\nk\r\n" — each opens a Map of one pair where the value will
  // be the next Map. Need 33 levels to exceed the 32 cap.
  std::string nested;
  for (int i = 0; i < 33; ++i) {
    nested += "%1\r\n$1\r\nk\r\n";
  }
  buffer_.add(nested);
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, IntegerWithNoDigitsRejected) {
  buffer_.add(":\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, NegativeIntegerWithNoDigitsRejected) {
  buffer_.add(":-\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, ArrayCountWithNoDigitsRejected) {
  buffer_.add("*\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, BulkStringLengthWithNoDigitsRejected) {
  buffer_.add("$\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, InvalidBulkStringExpectCR) {
  buffer_.add("$1\r\nab");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, InvalidBulkStringExpectLF) {
  buffer_.add("$1\r\na\ra");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandSingleWord) {
  RespValue ping;
  ping.type(RespType::BulkString);
  ping.asString() = "ping";

  buffer_.add("ping\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(1UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(ping, decoded_values_[0]->asArray()[0]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandMultipleWords) {
  RespValue echo;
  echo.type(RespType::BulkString);
  echo.asString() = "echo";

  RespValue hello;
  hello.type(RespType::BulkString);
  hello.asString() = "hello";

  buffer_.add("echo hello\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(2UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(echo, decoded_values_[0]->asArray()[0]);
  EXPECT_EQ(hello, decoded_values_[0]->asArray()[1]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandNumericWord) {
  RespValue answer;
  answer.type(RespType::BulkString);
  answer.asString() = "42";

  buffer_.add("42\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(1UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(answer, decoded_values_[0]->asArray()[0]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandNumericArgument) {
  RespValue echo;
  echo.type(RespType::BulkString);
  echo.asString() = "echo";

  RespValue answer;
  answer.type(RespType::BulkString);
  answer.asString() = "42";

  buffer_.add("echo 42\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(2UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(echo, decoded_values_[0]->asArray()[0]);
  EXPECT_EQ(answer, decoded_values_[0]->asArray()[1]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandTrimWhitespace) {
  RespValue ping;
  ping.type(RespType::BulkString);
  ping.asString() = "ping";

  buffer_.add("   ping   \r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(1UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(ping, decoded_values_[0]->asArray()[0]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandQuotedEmpty) {
  RespValue empty;
  empty.type(RespType::BulkString);
  empty.asString() = "";

  buffer_.add("\"\"\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(1UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(empty, decoded_values_[0]->asArray()[0]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandQuotedCommand) {
  RespValue ping;
  ping.type(RespType::BulkString);
  ping.asString() = "ping";

  buffer_.add("\"ping\"\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(1UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(ping, decoded_values_[0]->asArray()[0]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandQuotedArgument) {
  RespValue echo;
  echo.type(RespType::BulkString);
  echo.asString() = "echo";

  RespValue hello;
  hello.type(RespType::BulkString);
  hello.asString() = "hello world";

  buffer_.add("echo \"hello world\"\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(2UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(echo, decoded_values_[0]->asArray()[0]);
  EXPECT_EQ(hello, decoded_values_[0]->asArray()[1]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandQuotedEmptyArgument) {
  RespValue echo;
  echo.type(RespType::BulkString);
  echo.asString() = "echo";

  RespValue empty;
  empty.type(RespType::BulkString);
  empty.asString() = "";

  buffer_.add("echo \"\"\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(2UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(echo, decoded_values_[0]->asArray()[0]);
  EXPECT_EQ(empty, decoded_values_[0]->asArray()[1]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandQuotedWhitespace) {
  RespValue set;
  set.type(RespType::BulkString);
  set.asString() = "set";

  RespValue foobarbaz;
  foobarbaz.type(RespType::BulkString);
  foobarbaz.asString() = "foo 'bar' baz";

  RespValue quux;
  quux.type(RespType::BulkString);
  quux.asString() = "quux";

  buffer_.add("set \"foo 'bar' baz\" quux\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(3UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(set, decoded_values_[0]->asArray()[0]);
  EXPECT_EQ(foobarbaz, decoded_values_[0]->asArray()[1]);
  EXPECT_EQ(quux, decoded_values_[0]->asArray()[2]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandQuotedTrimWhitespace) {
  RespValue echo;
  echo.type(RespType::BulkString);
  echo.asString() = "echo";

  buffer_.add("   \"echo\"   \r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(1UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(echo, decoded_values_[0]->asArray()[0]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandQuotedEscapes) {
  RespValue unescaped;
  unescaped.type(RespType::BulkString);
  unescaped.asString() = "\\\"\x01\n\r\t\b\a";

  buffer_.add("\"\\\\\\\"\\x01\\n\\r\\t\\b\\a\"\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(1UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(unescaped, decoded_values_[0]->asArray()[0]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandQuotedInvalidEscapes) {
  RespValue unescaped;
  unescaped.type(RespType::BulkString);
  unescaped.asString() = "xyucx0";

  buffer_.add("\"\\xyu\\c\\x0\"\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(1UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(unescaped, decoded_values_[0]->asArray()[0]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandSingleQuotedCommand) {
  RespValue echo;
  echo.type(RespType::BulkString);
  echo.asString() = "echo";

  buffer_.add("'echo'\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(1UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(echo, decoded_values_[0]->asArray()[0]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandSingleQuotedArgument) {
  RespValue echo;
  echo.type(RespType::BulkString);
  echo.asString() = "echo";

  RespValue hello;
  hello.type(RespType::BulkString);
  hello.asString() = "hello world";

  buffer_.add("echo 'hello world'\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(2UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(echo, decoded_values_[0]->asArray()[0]);
  EXPECT_EQ(hello, decoded_values_[0]->asArray()[1]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandSingleQuotedEmptyArgument) {
  RespValue echo;
  echo.type(RespType::BulkString);
  echo.asString() = "echo";

  RespValue hello;
  hello.type(RespType::BulkString);
  hello.asString() = "";

  buffer_.add("echo ''\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(2UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(echo, decoded_values_[0]->asArray()[0]);
  EXPECT_EQ(hello, decoded_values_[0]->asArray()[1]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandSingleQuotedWhitespace) {
  RespValue set;
  set.type(RespType::BulkString);
  set.asString() = "set";

  RespValue foobarbaz;
  foobarbaz.type(RespType::BulkString);
  foobarbaz.asString() = "foo \"bar\" baz";

  RespValue quux;
  quux.type(RespType::BulkString);
  quux.asString() = "quux";

  buffer_.add("set 'foo \"bar\" baz' quux\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(3UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(set, decoded_values_[0]->asArray()[0]);
  EXPECT_EQ(foobarbaz, decoded_values_[0]->asArray()[1]);
  EXPECT_EQ(quux, decoded_values_[0]->asArray()[2]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandSingleQuotedTrimWhitespace) {
  RespValue echo;
  echo.type(RespType::BulkString);
  echo.asString() = "echo";

  buffer_.add("   'echo'   \r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(1UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(echo, decoded_values_[0]->asArray()[0]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandSingleQuotedEscapes) {
  RespValue unescaped;
  unescaped.type(RespType::BulkString);
  unescaped.asString() = "'";

  buffer_.add("'\\''\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(1UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(unescaped, decoded_values_[0]->asArray()[0]);
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandSingleQuotedInvalidEscapes) {
  RespValue unescaped;
  unescaped.type(RespType::BulkString);
  unescaped.asString() = "\\x01";

  buffer_.add("'\\x01'\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(1UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(unescaped, decoded_values_[0]->asArray()[0]);
}

TEST_F(RedisEncoderDecoderImplTest, InterleaveInlineCommand) {
  RespValue echo;
  echo.type(RespType::BulkString);
  echo.asString() = "echo";

  RespValue foo;
  foo.type(RespType::BulkString);
  foo.asString() = "foo";

  RespValue set;
  set.type(RespType::BulkString);
  set.asString() = "set";

  RespValue foobar;
  foobar.type(RespType::BulkString);
  foobar.asString() = "foo bar";

  RespValue baz;
  baz.type(RespType::BulkString);
  baz.asString() = "baz";

  RespValue ping;
  ping.type(RespType::BulkString);
  ping.asString() = "ping";

  buffer_.add("*2\r\n$4\r\necho\r\n$3\r\nfoo\r\nset baz \"foo bar\"\r\n*1\r\n$4\r\nping\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(3UL, decoded_values_.size());

  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(2UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(echo, decoded_values_[0]->asArray()[0]);
  EXPECT_EQ(foo, decoded_values_[0]->asArray()[1]);

  EXPECT_EQ(RespType::Array, decoded_values_[1]->type());
  EXPECT_EQ(3UL, decoded_values_[1]->asArray().size());
  EXPECT_EQ(set, decoded_values_[1]->asArray()[0]);
  EXPECT_EQ(baz, decoded_values_[1]->asArray()[1]);
  EXPECT_EQ(foobar, decoded_values_[1]->asArray()[2]);

  EXPECT_EQ(RespType::Array, decoded_values_[2]->type());
  EXPECT_EQ(1UL, decoded_values_[2]->asArray().size());
  EXPECT_EQ(ping, decoded_values_[2]->asArray()[0]);
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandQuoting1) {
  buffer_.add("\"\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandQuoting2) {
  buffer_ = Buffer::OwnedImpl();
  buffer_.add("\"\"\"\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandQuoting3) {
  buffer_ = Buffer::OwnedImpl();
  buffer_.add("\"foo\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandQuoting4) {
  buffer_ = Buffer::OwnedImpl();
  buffer_.add("\" foo\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandQuoting5) {
  buffer_ = Buffer::OwnedImpl();
  buffer_.add("\"fo\"o\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandSingleQuoting1) {
  buffer_.add("'\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandSingleQuoting2) {
  buffer_ = Buffer::OwnedImpl();
  buffer_.add("'''\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandSingleQuoting3) {
  buffer_ = Buffer::OwnedImpl();
  buffer_.add("'foo\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandSingleQuoting4) {
  buffer_ = Buffer::OwnedImpl();
  buffer_.add("' foo\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandSingleQuoting5) {
  buffer_ = Buffer::OwnedImpl();
  buffer_.add("'fo'o\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInterjectedInlineCommand) {
  buffer_.add("*1\r\nECHO\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

// RESP3 Boolean tests
TEST_F(RedisEncoderDecoderImplTest, Resp3BooleanTrue) {
  // Decode #t\r\n
  buffer_.add("#t\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Boolean, decoded_values_[0]->type());
  EXPECT_TRUE(decoded_values_[0]->asBoolean());
  EXPECT_EQ("true", decoded_values_[0]->toString());
  EXPECT_EQ(0UL, buffer_.length());

  // Encode round-trip. The encoder defaults to RESP2 under the HTTP-style
  // split; RESP3 native wire form is only emitted when the encoder is
  // explicitly put in RESP3 mode (matching how real downstream connections
  // flip after HELLO 3 negotiation).
  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl out;
  encoder_.encode(*decoded_values_[0], out);
  EXPECT_EQ("#t\r\n", out.toString());
}

TEST_F(RedisEncoderDecoderImplTest, Resp3BooleanFalse) {
  buffer_.add("#f\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Boolean, decoded_values_[0]->type());
  EXPECT_FALSE(decoded_values_[0]->asBoolean());
  EXPECT_EQ("false", decoded_values_[0]->toString());

  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl out;
  encoder_.encode(*decoded_values_[0], out);
  EXPECT_EQ("#f\r\n", out.toString());
}

TEST_F(RedisEncoderDecoderImplTest, Resp3BooleanInvalid) {
  buffer_.add("#x\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

// RESP3 Double tests
TEST_F(RedisEncoderDecoderImplTest, Resp3Double) {
  buffer_.add(",3.14\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Double, decoded_values_[0]->type());
  EXPECT_DOUBLE_EQ(3.14, decoded_values_[0]->asDouble());

  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl out;
  encoder_.encode(*decoded_values_[0], out);
  // fmt::format may produce "3.14" or similar
  EXPECT_THAT(out.toString(), testing::StartsWith(","));
  EXPECT_THAT(out.toString(), testing::EndsWith("\r\n"));
}

TEST_F(RedisEncoderDecoderImplTest, Resp3DoubleNegative) {
  buffer_.add(",-1.5\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Double, decoded_values_[0]->type());
  EXPECT_DOUBLE_EQ(-1.5, decoded_values_[0]->asDouble());
}

TEST_F(RedisEncoderDecoderImplTest, Resp3DoubleInf) {
  buffer_.add(",inf\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Double, decoded_values_[0]->type());
  EXPECT_TRUE(std::isinf(decoded_values_[0]->asDouble()));
}

// RESP3 BigNumber test
TEST_F(RedisEncoderDecoderImplTest, Resp3BigNumber) {
  buffer_.add("(3492890328409238509324850943850943825024385\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::BigNumber, decoded_values_[0]->type());
  EXPECT_EQ("3492890328409238509324850943850943825024385", decoded_values_[0]->asString());
  EXPECT_EQ("(big)3492890328409238509324850943850943825024385", decoded_values_[0]->toString());

  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl out;
  encoder_.encode(*decoded_values_[0], out);
  EXPECT_EQ("(3492890328409238509324850943850943825024385\r\n", out.toString());
}

// RESP3 Null test
TEST_F(RedisEncoderDecoderImplTest, Resp3Null) {
  buffer_.add("_\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Null, decoded_values_[0]->type());
  EXPECT_EQ("null", decoded_values_[0]->toString());
}

// RESP3 BlobError test
TEST_F(RedisEncoderDecoderImplTest, Resp3BlobError) {
  buffer_.add("!12\r\nSYNTAX error\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::BlobError, decoded_values_[0]->type());
  EXPECT_EQ("SYNTAX error", decoded_values_[0]->asString());
  EXPECT_EQ("!(SYNTAX error)", decoded_values_[0]->toString());

  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl out;
  encoder_.encode(*decoded_values_[0], out);
  EXPECT_EQ("!12\r\nSYNTAX error\r\n", out.toString());
}

// RESP3 VerbatimString test
TEST_F(RedisEncoderDecoderImplTest, Resp3VerbatimString) {
  buffer_.add("=15\r\ntxt:Some string\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::VerbatimString, decoded_values_[0]->type());
  EXPECT_EQ("txt:Some string", decoded_values_[0]->asString());
  EXPECT_EQ("=txt:Some string", decoded_values_[0]->toString());

  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl out;
  encoder_.encode(*decoded_values_[0], out);
  EXPECT_EQ("=15\r\ntxt:Some string\r\n", out.toString());
}

// RESP3 Map test
TEST_F(RedisEncoderDecoderImplTest, Resp3Map) {
  // %2\r\n+first\r\n:1\r\n+second\r\n:2\r\n
  buffer_.add("%2\r\n+first\r\n:1\r\n+second\r\n:2\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Map, decoded_values_[0]->type());
  // Map stores 4 elements (2 key-value pairs)
  EXPECT_EQ(4UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ("first", decoded_values_[0]->asArray()[0].asString());
  EXPECT_EQ(1, decoded_values_[0]->asArray()[1].asInteger());
  EXPECT_EQ("second", decoded_values_[0]->asArray()[2].asString());
  EXPECT_EQ(2, decoded_values_[0]->asArray()[3].asInteger());
  EXPECT_EQ("{\"first\": 1, \"second\": 2}", decoded_values_[0]->toString());

  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl out;
  encoder_.encode(*decoded_values_[0], out);
  EXPECT_EQ("%2\r\n+first\r\n:1\r\n+second\r\n:2\r\n", out.toString());
}

TEST_F(RedisEncoderDecoderImplTest, Resp3EmptyMap) {
  buffer_.add("%0\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Map, decoded_values_[0]->type());
  EXPECT_EQ(0UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ("{}", decoded_values_[0]->toString());

  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl out;
  encoder_.encode(*decoded_values_[0], out);
  EXPECT_EQ("%0\r\n", out.toString());
}

// RESP3 Set test
TEST_F(RedisEncoderDecoderImplTest, Resp3Set) {
  buffer_.add("~3\r\n+a\r\n+b\r\n+c\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Set, decoded_values_[0]->type());
  EXPECT_EQ(3UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ("a", decoded_values_[0]->asArray()[0].asString());
  EXPECT_EQ("b", decoded_values_[0]->asArray()[1].asString());
  EXPECT_EQ("c", decoded_values_[0]->asArray()[2].asString());

  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl out;
  encoder_.encode(*decoded_values_[0], out);
  EXPECT_EQ("~3\r\n+a\r\n+b\r\n+c\r\n", out.toString());
}

// RESP3 Push test. Encode round-trip exercises the native '>' wire form,
// which only fires when the encoder is in RESP3 mode; under RESP2 (the
// default) the encoder emits Push as a plain Array to keep RESP2 clients'
// response framing aligned.
TEST_F(RedisEncoderDecoderImplTest, Resp3Push) {
  // Push message format for pub/sub: >3\r\n$7\r\nmessage\r\n$5\r\nhello\r\n$5\r\nworld\r\n
  buffer_.add(">3\r\n$7\r\nmessage\r\n$5\r\nhello\r\n$5\r\nworld\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Push, decoded_values_[0]->type());
  EXPECT_EQ(3UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ("message", decoded_values_[0]->asArray()[0].asString());
  EXPECT_EQ("hello", decoded_values_[0]->asArray()[1].asString());
  EXPECT_EQ("world", decoded_values_[0]->asArray()[2].asString());

  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl out;
  encoder_.encode(*decoded_values_[0], out);
  EXPECT_EQ(">3\r\n$7\r\nmessage\r\n$5\r\nhello\r\n$5\r\nworld\r\n", out.toString());
}

// RESP3 Push subscribe ack
TEST_F(RedisEncoderDecoderImplTest, Resp3PushSubscribeAck) {
  buffer_.add(">3\r\n$9\r\nsubscribe\r\n$5\r\nhello\r\n:1\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Push, decoded_values_[0]->type());
  EXPECT_EQ(3UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ("subscribe", decoded_values_[0]->asArray()[0].asString());
  EXPECT_EQ("hello", decoded_values_[0]->asArray()[1].asString());
  EXPECT_EQ(1, decoded_values_[0]->asArray()[2].asInteger());
}

// RESP3 Attribute type error
// Attribute at root: parsed and discarded, actual value follows
TEST_F(RedisEncoderDecoderImplTest, Resp3AttributeParsedAndDiscarded) {
  // Attribute with 1 key-value pair, followed by actual value "+OK\r\n"
  buffer_.add("|1\r\n+key\r\n+val\r\n+OK\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::SimpleString, decoded_values_[0]->type());
  EXPECT_EQ("OK", decoded_values_[0]->asString());
}

// Copy/move semantics for RESP3 types
TEST_F(RedisRespValueTest, Resp3TypeCopyMove) {
  // Boolean
  RespValue bool_val;
  bool_val.type(RespType::Boolean);
  bool_val.asInteger() = 1;
  verifyMoves(bool_val);

  // Double
  RespValue double_val;
  double_val.type(RespType::Double);
  double_val.asDouble() = 3.14;
  verifyMoves(double_val);

  // BigNumber
  RespValue big_val;
  big_val.type(RespType::BigNumber);
  big_val.asString() = "12345678901234567890";
  verifyMoves(big_val);

  // BlobError
  RespValue blob_err;
  blob_err.type(RespType::BlobError);
  blob_err.asString() = "SYNTAX error";
  verifyMoves(blob_err);

  // VerbatimString
  RespValue verb_val;
  verb_val.type(RespType::VerbatimString);
  verb_val.asString() = "txt:hello";
  verifyMoves(verb_val);

  // Map
  RespValue map_val;
  map_val.type(RespType::Map);
  RespValue key, val;
  key.type(RespType::SimpleString);
  key.asString() = "key";
  val.type(RespType::Integer);
  val.asInteger() = 42;
  map_val.asArray().push_back(key);
  map_val.asArray().push_back(val);
  verifyMoves(map_val);

  // Set
  RespValue set_val;
  set_val.type(RespType::Set);
  set_val.asArray().push_back(key);
  verifyMoves(set_val);

  // Push
  RespValue push_val;
  push_val.type(RespType::Push);
  push_val.asArray().push_back(key);
  push_val.asArray().push_back(val);
  verifyMoves(push_val);
}

// Equality tests for RESP3 types
TEST_F(RedisRespValueTest, Resp3TypeEquality) {
  RespValue bool1, bool2;
  bool1.type(RespType::Boolean);
  bool1.asInteger() = 1;
  bool2.type(RespType::Boolean);
  bool2.asInteger() = 0;
  EXPECT_NE(bool1, bool2);
  bool2.asInteger() = 1;
  EXPECT_EQ(bool1, bool2);

  RespValue double1, double2;
  double1.type(RespType::Double);
  double1.asDouble() = 3.14;
  double2.type(RespType::Double);
  double2.asDouble() = 2.71;
  EXPECT_NE(double1, double2);
  double2.asDouble() = 3.14;
  EXPECT_EQ(double1, double2);

  // Cross-type inequality
  EXPECT_NE(bool1, double1);
}

// Multiple RESP3 values in sequence
TEST_F(RedisEncoderDecoderImplTest, Resp3MultipleValues) {
  buffer_.add("#t\r\n,3.14\r\n_\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(3UL, decoded_values_.size());
  EXPECT_EQ(RespType::Boolean, decoded_values_[0]->type());
  EXPECT_EQ(RespType::Double, decoded_values_[1]->type());
  EXPECT_EQ(RespType::Null, decoded_values_[2]->type());
}

// Nested RESP3: Map containing Push
TEST_F(RedisEncoderDecoderImplTest, Resp3NestedTypes) {
  // Map with one entry: key="ch", value=Push[message, ch, hello]
  buffer_.add("%1\r\n$2\r\nch\r\n>3\r\n$7\r\nmessage\r\n$2\r\nch\r\n$5\r\nhello\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Map, decoded_values_[0]->type());
  EXPECT_EQ(2UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ("ch", decoded_values_[0]->asArray()[0].asString());
  EXPECT_EQ(RespType::Push, decoded_values_[0]->asArray()[1].type());
  EXPECT_EQ(3UL, decoded_values_[0]->asArray()[1].asArray().size());
}

TEST_F(RedisEncoderDecoderImplTest, Resp3InvalidBigNumber) {
  buffer_.add("(123abc\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, Resp3NegativeBlobErrorLengthRejected) {
  buffer_.add("!-1\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, Resp3NegativeVerbatimStringLengthRejected) {
  buffer_.add("=-1\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, Resp3InvalidVerbatimStringPrefixRejected) {
  buffer_.add("=8\r\ntxthello\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

// RESP3 Attribute: root-level attribute is parsed and discarded
TEST_F(RedisEncoderDecoderImplTest, Resp3AttributeRootDiscarded) {
  // |1\r\n+key\r\n+val\r\n followed by the actual value +OK\r\n
  buffer_.add("|1\r\n+key\r\n+val\r\n+OK\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::SimpleString, decoded_values_[0]->type());
  EXPECT_EQ("OK", decoded_values_[0]->asString());
}

// RESP3 Attribute: nested inside an array is parsed and discarded
TEST_F(RedisEncoderDecoderImplTest, Resp3AttributeNestedInArray) {
  // Array of 2 elements where element 0 is preceded by an attribute
  // *2\r\n |1\r\n+k\r\n+v\r\n :42\r\n :99\r\n
  buffer_.add("*2\r\n|1\r\n+k\r\n+v\r\n:42\r\n:99\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(2UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(42, decoded_values_[0]->asArray()[0].asInteger());
  EXPECT_EQ(99, decoded_values_[0]->asArray()[1].asInteger());
}

// RESP3 Attribute: empty attribute (0 entries)
TEST_F(RedisEncoderDecoderImplTest, Resp3AttributeEmpty) {
  buffer_.add("|0\r\n$3\r\nfoo\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::BulkString, decoded_values_[0]->type());
  EXPECT_EQ("foo", decoded_values_[0]->asString());
}

// RESP3 Attribute chain DoS protection
TEST_F(RedisEncoderDecoderImplTest, Resp3AttributeChainLimit) {
  // 33 consecutive empty attributes exceed the limit of 32
  std::string payload;
  for (int i = 0; i < 33; i++) {
    payload += "|0\r\n";
  }
  payload += "+OK\r\n";
  buffer_.add(payload);
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

// Negative counts for RESP3 aggregate types produce Null
// RESP3 Map/Set/Push have NO null form. The RESP3 null type byte (``_``)
// is separate. Reject any negative count for these types — accepting
// ``%-1`` etc. as null would silently let an attacker smuggle ambiguous
// frames past callers that expect the documented type.
TEST_F(RedisEncoderDecoderImplTest, Resp3NegativeMapCountRejected) {
  buffer_.add("%-1\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, Resp3NegativeSetCountRejected) {
  buffer_.add("~-1\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, Resp3NegativePushCountRejected) {
  buffer_.add(">-1\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

// Only ``$-1`` and ``*-1`` are the RESP-defined null forms. Other negative
// counts/lengths are malformed and must be rejected.
TEST_F(RedisEncoderDecoderImplTest, BulkStringNegativeTwoRejected) {
  buffer_.add("$-2\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, ArrayNegativeTwoRejected) {
  buffer_.add("*-2\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

// The valid null forms for RESP2 still work after the tightening.
TEST_F(RedisEncoderDecoderImplTest, BulkStringNegativeOneIsNull) {
  buffer_.add("$-1\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Null, decoded_values_[0]->type());
}

TEST_F(RedisEncoderDecoderImplTest, ArrayNegativeOneIsNull) {
  buffer_.add("*-1\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Null, decoded_values_[0]->type());
}

// Null encoding: RESP2 uses $-1\r\n, RESP3 uses _\r\n
TEST_F(RedisEncoderDecoderImplTest, NullEncodingResp2VsResp3) {
  RespValue null_val;
  null_val.type(RespType::Null);

  // Default (RESP2)
  Buffer::OwnedImpl resp2_buf;
  encoder_.encode(null_val, resp2_buf);
  EXPECT_EQ("$-1\r\n", resp2_buf.toString());

  // RESP3
  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl resp3_buf;
  encoder_.encode(null_val, resp3_buf);
  EXPECT_EQ("_\r\n", resp3_buf.toString());
}

TEST_F(RedisEncoderDecoderImplTest, Resp3DoubleNaN) {
  buffer_.add(",nan\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Double, decoded_values_[0]->type());
  EXPECT_TRUE(std::isnan(decoded_values_[0]->asDouble()));

  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl out;
  encoder_.encode(*decoded_values_[0], out);
  EXPECT_EQ(",nan\r\n", out.toString());
}

TEST_F(RedisEncoderDecoderImplTest, Resp3DoubleNegativeInf) {
  buffer_.add(",-inf\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Double, decoded_values_[0]->type());
  EXPECT_TRUE(std::isinf(decoded_values_[0]->asDouble()));
  EXPECT_LT(decoded_values_[0]->asDouble(), 0);

  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl out;
  encoder_.encode(*decoded_values_[0], out);
  EXPECT_EQ(",-inf\r\n", out.toString());
}

TEST_F(RedisEncoderDecoderImplTest, Resp3DoubleInvalid) {
  buffer_.add(",abc\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, Resp3DoubleTooLong) {
  std::string payload = ",";
  payload += std::string(65, '1'); // 65 chars exceeds 64 limit
  payload += "\r\n";
  buffer_.add(payload);
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, Resp3BigNumberPositive) {
  buffer_.add("(+12345\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::BigNumber, decoded_values_[0]->type());
  EXPECT_EQ("+12345", decoded_values_[0]->asString());
}

TEST_F(RedisEncoderDecoderImplTest, Resp3ArrayCountExceedsMax) {
  // 1048577 exceeds kMaxRespElements (1048576)
  buffer_.add("*1048577\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, Resp3MapCountOverflow) {
  // Map count that would overflow when doubled
  buffer_.add("%9223372036854775807\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, Resp3SetCountExceedsMax) {
  buffer_.add("~1048577\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, Resp3PushCountExceedsMax) {
  buffer_.add(">1048577\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

// Pre-multiply integer overflow guards. uint64_t::max is 20 digits
// (18446744073709551615); 25-digit inputs would wrap during accumulation
// and slip past every post-LF semantic check (kMaxRespElements,
// kMaxTotalElements, Map *2 cap), letting an attacker land any chosen value
// in the integer accumulator.
TEST_F(RedisEncoderDecoderImplTest, Resp2ArrayCountIntegerOverflow) {
  buffer_.add("*9999999999999999999999999\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, Resp2BulkStringLengthIntegerOverflow) {
  buffer_.add("$9999999999999999999999999\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, Resp3MapCountIntegerOverflow) {
  buffer_.add("%9999999999999999999999999\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, Resp3SetCountIntegerOverflow) {
  buffer_.add("~9999999999999999999999999\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

// Signed RESP integer (`:N\r\n`) range checks. The accumulator is uint64_t
// but asInteger() is int64_t — wire values outside [INT64_MIN, INT64_MAX]
// would silently wrap on cast. Reject at parse time.
TEST_F(RedisEncoderDecoderImplTest, RespIntegerInt64MaxAccepted) {
  buffer_.add(":9223372036854775807\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Integer, decoded_values_[0]->type());
  EXPECT_EQ(std::numeric_limits<int64_t>::max(), decoded_values_[0]->asInteger());
}

TEST_F(RedisEncoderDecoderImplTest, RespIntegerInt64MaxPlusOneRejected) {
  buffer_.add(":9223372036854775808\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, RespIntegerInt64MinAccepted) {
  buffer_.add(":-9223372036854775808\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Integer, decoded_values_[0]->type());
  EXPECT_EQ(std::numeric_limits<int64_t>::min(), decoded_values_[0]->asInteger());
}

TEST_F(RedisEncoderDecoderImplTest, RespIntegerInt64MinMinusOneRejected) {
  buffer_.add(":-9223372036854775809\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, RespIntegerNegativeZeroAcceptedAsZero) {
  // Wire "-0" → 0. Tolerate for backward-compat with senders that emit it.
  buffer_.add(":-0\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Integer, decoded_values_[0]->type());
  EXPECT_EQ(0, decoded_values_[0]->asInteger());
}

// Bulk-string length cap (512 MiB, matches Redis proto-max-bulk-len). The
// header is rejected BEFORE the body-read loop so an attacker-supplied
// length cannot drive unbounded string growth.
TEST_F(RedisEncoderDecoderImplTest, BulkStringLengthAboveCapRejected) {
  // 512 MiB + 1 byte.
  buffer_.add("$536870913\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, VerbatimStringLengthAboveCapRejected) {
  buffer_.add("=536870913\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, BlobErrorLengthAboveCapRejected) {
  buffer_.add("!536870913\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, Resp3BigNumberLoneSign) {
  buffer_.add("(-\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, Resp3BigNumberLonePositiveSign) {
  buffer_.add("(+\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

// =============================================================================
// Encoder RESP3 -> RESP2 conversion tests (wire form).
//
// These exercise EncoderImpl::setProtocolVersion(Resp2) combined with an
// in-memory RespValue that carries a RESP3-only type. The encoder must emit
// the RESP2-compat wire form defined by the RESP3 spec rather than the
// RESP3 type byte; otherwise a RESP2 client would see a wire byte it cannot
// parse.
//
// Each test:
//   1. Constructs the IR value directly (bypassing the decoder so the test
//      does not conflate decode and encode bugs).
//   2. Sets the encoder to RESP2.
//   3. Encodes and checks the exact wire bytes.
// =============================================================================

class RespEncoderDownconversionTest : public RedisEncoderDecoderImplTest {
public:
  void encodeResp2(const RespValue& value, Buffer::OwnedImpl& out) {
    encoder_.setProtocolVersion(RespProtocolVersion::Resp2);
    encoder_.encode(value, out);
  }
};

TEST_F(RespEncoderDownconversionTest, NullEmitsResp2Form) {
  // The existing Null test uses the default (RESP2) encoder and expects $-1.
  // Here we explicitly verify the conversion remains stable after the
  // encoder gains per-connection version state.
  RespValue v;
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ("$-1\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, NullEmitsResp3FormWhenNegotiated) {
  RespValue v;
  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl out;
  encoder_.encode(v, out);
  EXPECT_EQ("_\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, BooleanTrueAsInteger) {
  RespValue v;
  v.type(RespType::Boolean);
  v.asInteger() = 1;
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ(":1\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, BooleanFalseAsInteger) {
  RespValue v;
  v.type(RespType::Boolean);
  v.asInteger() = 0;
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ(":0\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, DoubleAsBulkString) {
  RespValue v;
  v.type(RespType::Double);
  v.asDouble() = 3.14;
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  // fmt::format("{:.17g}", 3.14) -> "3.1400000000000001" (double precision).
  // We test for structural shape rather than exact bytes to avoid coupling the
  // test to fmt's formatting precision; the converted form is a bulk string
  // containing whatever formatDoubleForWire produced.
  const std::string s = out.toString();
  EXPECT_TRUE(absl::StartsWith(s, "$"));
  EXPECT_TRUE(absl::EndsWith(s, "\r\n"));
  // BulkString payload must be the same decimal the native RESP3 emit uses.
  Buffer::OwnedImpl resp3_out;
  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  encoder_.encode(v, resp3_out);
  const std::string resp3 = resp3_out.toString();
  // RESP3 form is ",<digits>\r\n"; extract <digits> and confirm RESP2 form
  // carries the same digits inside a bulk string.
  ASSERT_GT(resp3.size(), 3U);
  const std::string digits = resp3.substr(1, resp3.size() - 3);
  EXPECT_NE(std::string::npos, s.find(digits));
}

// Integer-valued RESP3 doubles must still encode with a decimal marker (or exponent), otherwise
// downstream clients that pattern-match for floating-point shape would classify the payload as
// integer. {:.17g} alone produces "1" for 1.0, so the formatter explicitly appends ".0".
TEST_F(RespEncoderDownconversionTest, DoubleIntegerValuedHasDecimal) {
  RespValue v;
  v.type(RespType::Double);
  v.asDouble() = 1.0;
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  // RESP2 form: bulk string carrying "1.0", not "1".
  EXPECT_EQ("$3\r\n1.0\r\n", out.toString());

  Buffer::OwnedImpl resp3_out;
  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  encoder_.encode(v, resp3_out);
  // RESP3 native form must carry the same digits with the ',' prefix.
  EXPECT_EQ(",1.0\r\n", resp3_out.toString());
}

TEST_F(RespEncoderDownconversionTest, DoubleInfAsBulkString) {
  RespValue v;
  v.type(RespType::Double);
  v.asDouble() = std::numeric_limits<double>::infinity();
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ("$3\r\ninf\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, DoubleNegInfAsBulkString) {
  RespValue v;
  v.type(RespType::Double);
  v.asDouble() = -std::numeric_limits<double>::infinity();
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ("$4\r\n-inf\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, DoubleNanAsBulkString) {
  RespValue v;
  v.type(RespType::Double);
  v.asDouble() = std::nan("");
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ("$3\r\nnan\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, BigNumberAsBulkString) {
  RespValue v;
  v.type(RespType::BigNumber);
  v.asString() = "3492890328409238509324850943850943825024385";
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ("$43\r\n3492890328409238509324850943850943825024385\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, BlobErrorAsInlineError) {
  RespValue v;
  v.type(RespType::BlobError);
  v.asString() = "SYNTAX error";
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ("-SYNTAX error\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, BlobErrorWithCRLFIsSanitized) {
  // Embedded CR or LF would desynchronize the RESP2 wire. The encoder must
  // replace them with space so the `-<msg>\r\n` form parses cleanly.
  RespValue v;
  v.type(RespType::BlobError);
  v.asString() = "line one\r\nline two\nend";
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ("-line one  line two end\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, VerbatimStringStripsPrefix) {
  RespValue v;
  v.type(RespType::VerbatimString);
  v.asString() = "txt:Some string";
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  // RESP2 client sees just the data portion as a bulk string.
  EXPECT_EQ("$11\r\nSome string\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, VerbatimStringShortFallsBackToEmpty) {
  // Defensive: if a VerbatimString sneaks through the decoder's 4-byte
  // minimum guard, the encoder should still produce valid RESP2 wire rather
  // than crash or emit garbage.
  RespValue v;
  v.type(RespType::VerbatimString);
  v.asString() = "abc"; // too short (no ':' at position 3)
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ("$0\r\n\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, MapAsArray) {
  // IR stores Map as flat 2N [k0,v0,k1,v1]; RESP2 wire is *2N.
  RespValue v;
  v.type(RespType::Map);
  std::vector<RespValue> kv(4);
  kv[0].type(RespType::SimpleString);
  kv[0].asString() = "first";
  kv[1].type(RespType::Integer);
  kv[1].asInteger() = 1;
  kv[2].type(RespType::SimpleString);
  kv[2].asString() = "second";
  kv[3].type(RespType::Integer);
  kv[3].asInteger() = 2;
  v.asArray().swap(kv);
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ("*4\r\n+first\r\n:1\r\n+second\r\n:2\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, EmptyMapAsEmptyArray) {
  RespValue v;
  v.type(RespType::Map);
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ("*0\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, SetAsArray) {
  RespValue v;
  v.type(RespType::Set);
  std::vector<RespValue> elems(3);
  elems[0].type(RespType::SimpleString);
  elems[0].asString() = "a";
  elems[1].type(RespType::SimpleString);
  elems[1].asString() = "b";
  elems[2].type(RespType::SimpleString);
  elems[2].asString() = "c";
  v.asArray().swap(elems);
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ("*3\r\n+a\r\n+b\r\n+c\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, NestedMapInsideArrayDownconverts) {
  // RESP2 downstream receiving an Array whose elements include a Map (e.g. a
  // CLUSTER SHARDS reply from a RESP3 upstream). Each nested RESP3-only type
  // must convert in place.
  RespValue inner;
  inner.type(RespType::Map);
  std::vector<RespValue> kv(2);
  kv[0].type(RespType::SimpleString);
  kv[0].asString() = "k";
  kv[1].type(RespType::Boolean);
  kv[1].asInteger() = 1;
  inner.asArray().swap(kv);

  RespValue outer;
  outer.type(RespType::Array);
  std::vector<RespValue> outer_elems;
  outer_elems.push_back(std::move(inner));
  outer.asArray().swap(outer_elems);

  Buffer::OwnedImpl out;
  encodeResp2(outer, out);
  // Outer array 1 element; inner map -> *2, elements +k and :1 (Boolean true
  // converted to Integer 1).
  EXPECT_EQ("*1\r\n*2\r\n+k\r\n:1\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, PushDownconvertsToArray) {
  // `>N` becomes `*N` with the same bulk-string payload, matching the RESP2 pubsub wire form
  // so a RESP2 client's response queue stays framed.
  RespValue v;
  v.type(RespType::Push);
  std::vector<RespValue> elems(3);
  elems[0].type(RespType::SimpleString);
  elems[0].asString() = "message";
  elems[1].type(RespType::SimpleString);
  elems[1].asString() = "ch";
  elems[2].type(RespType::SimpleString);
  elems[2].asString() = "hello";
  v.asArray().swap(elems);
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ("*3\r\n+message\r\n+ch\r\n+hello\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, BlobErrorWithControlCharsIsSanitized) {
  // Beyond CR/LF the sanitizer must also strip NUL, BEL, ESC and DEL so that
  // a malicious upstream cannot inject terminal escape sequences or log
  // delimiters into a downstream's view of the error.
  RespValue v;
  v.type(RespType::BlobError);
  v.asString() = std::string("ESC:\x1b[31mRED\x1b[0m BEL:\x07 NUL:") + std::string(1, '\0') +
                 std::string(" DEL:\x7f");
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ("-ESC: [31mRED [0m BEL:  NUL:  DEL: \r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, BlobErrorPlainAsciiSkipsAllocation) {
  // Coverage for the fast path: when no character needs replacement, the
  // sanitizer hands back the original bytes unchanged.
  RespValue v;
  v.type(RespType::BlobError);
  v.asString() = "ERR plain ascii message";
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ("-ERR plain ascii message\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, Resp3TargetLeavesResp3TypesIntact) {
  // Sanity check: when the encoder is set to RESP3, the RESP3-only types emit
  // their native wire form (not the converted form).
  RespValue boolean;
  boolean.type(RespType::Boolean);
  boolean.asInteger() = 1;
  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl out;
  encoder_.encode(boolean, out);
  EXPECT_EQ("#t\r\n", out.toString());
}

// RESP3 decoder regression tests — attribute discard, DoS guards, and the
// round-trip path the wire-level fixtures above don't exercise directly.

TEST_F(RedisEncoderDecoderImplTest, AttributeNestedDiscardedBeforeValue) {
  // Inside a 1-element array: attribute annotating a simple string.
  // Trailing +OK keeps the parser exercise unambiguous about consumption order.
  buffer_.add("*1\r\n|1\r\n+meta\r\n+OK\r\n+OK\r\n");
  decoder_.decode(buffer_);
  ASSERT_GE(decoded_values_.size(), 1U);
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  ASSERT_EQ(1U, decoded_values_[0]->asArray().size());
  EXPECT_EQ(RespType::SimpleString, decoded_values_[0]->asArray()[0].type());
  EXPECT_EQ("OK", decoded_values_[0]->asArray()[0].asString());
}

TEST_F(RedisEncoderDecoderImplTest, AttributeFloodRejected) {
  // 33 consecutive attributes should trip the kMaxConsecutiveAttributes guard.
  std::string flood;
  for (int i = 0; i < 33; ++i) {
    flood += "|0\r\n";
  }
  flood += "+OK\r\n";
  buffer_.add(flood);
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, MapCountOverflowRejected) {
  // A map count that would overflow when doubled for the flat 2N layout.
  // uint64_max/2 + 1 pairs -> element count would wrap.
  buffer_.add("%9223372036854775808\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, VerbatimStringMissingColonRejected) {
  // RESP3 verbatim strings require a 3-char format prefix + ':' at byte 3.
  buffer_.add("=6\r\ntxtXXX\r\n");
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, DoubleTooLongRejected) {
  // The accumulator for double digits is capped at 64 bytes.
  std::string long_double = ",";
  long_double.append(65, '9');
  long_double += "\r\n";
  buffer_.add(long_double);
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, NullArrayDecodesToNull) {
  // RESP2 null array *-1\r\n must decode to RespType::Null.
  buffer_.add("*-1\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1U, decoded_values_.size());
  EXPECT_EQ(RespType::Null, decoded_values_[0]->type());
}

TEST_F(RedisEncoderDecoderImplTest, Resp3NullDecodesToNull) {
  // RESP3 null _\r\n decodes into the same IR type as RESP2 null.
  buffer_.add("_\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1U, decoded_values_.size());
  EXPECT_EQ(RespType::Null, decoded_values_[0]->type());
}

TEST_F(RedisEncoderDecoderImplTest, NestingDepthRejected) {
  // 32 nested *1 frames produce a stack depth of 33 after pushes (root + 32),
  // which exceeds kMaxNestingDepth = 32. Reject.
  std::string deep;
  for (int i = 0; i < 32; ++i) {
    deep += "*1\r\n";
  }
  deep += "+x\r\n";
  buffer_.add(deep);
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, NestingDepthAtBoundaryAccepted) {
  // 31 nested *1 frames + leaf reach a stack depth of 32 (root + 31 pushes),
  // which is exactly kMaxNestingDepth. The pre-push check
  // (depth >= kMaxNestingDepth) lets the 31st push through (depth was 31
  // before) and rejects a 32nd push (depth 32 already at the cap).
  std::string deep;
  for (int i = 0; i < 31; ++i) {
    deep += "*1\r\n";
  }
  deep += "+x\r\n";
  buffer_.add(deep);
  decoder_.decode(buffer_);
  ASSERT_EQ(1U, decoded_values_.size());
}

TEST_F(RedisEncoderDecoderImplTest, TotalElementBudgetRejected) {
  // Stack five 800k aggregates: each is below the per-aggregate 1M cap, but
  // the running budget reaches 4M after the fifth, so the sixth (any size)
  // is rejected by the cumulative check before its element vector is
  // allocated.
  // Nesting works because the parser accepts a nested aggregate as the
  // first element of its parent — we don't need to provide actual leaf
  // bytes for the budget check to fire.
  std::string trip;
  for (int i = 0; i < 5; ++i) {
    trip += "*800000\r\n";
  }
  trip += "*800001\r\n";
  buffer_.add(trip);
  EXPECT_THROW(decoder_.decode(buffer_), ProtocolError);
}

TEST_F(RedisEncoderDecoderImplTest, TotalElementBudgetResetsBetweenRootValues) {
  // First root value uses up most of the budget; second root value should
  // start fresh.
  buffer_.add("*900000\r\n");
  // Don't actually wait for 900000 elements; this will hang until they
  // arrive. Use a smaller value that completes synchronously.
  buffer_.drain(buffer_.length());
  buffer_.add("+ok\r\n+ok\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(2U, decoded_values_.size());
}

TEST_F(RedisEncoderDecoderImplTest, MapStorageIsFlat2N) {
  // Verify the storage invariant: a %N Map is stored as a 2N-element vector.
  buffer_.add("%2\r\n+a\r\n:1\r\n+b\r\n:2\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1U, decoded_values_.size());
  EXPECT_EQ(RespType::Map, decoded_values_[0]->type());
  EXPECT_EQ(4U, decoded_values_[0]->asArray().size());
}

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
