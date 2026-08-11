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
#include "absl/strings/str_cat.h"
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

// Feed a frame exercising the RESP3 parser states (Boolean, Double, Attribute, Map, Push) one
// byte at a time. Each new parser state must resume correctly across slice boundaries.
TEST_F(RedisEncoderDecoderImplTest, Resp3TypesSegmentedDecode) {
  // |1\r\n+k\r\n+v\r\n  : attribute (discarded), then the real value:
  // %2\r\n+b\r\n#t\r\n+d\r\n,3.14\r\n  : Map{ b: true, d: 3.14 }
  const std::string frame = "|1\r\n+k\r\n+v\r\n%2\r\n+b\r\n#t\r\n+d\r\n,3.14\r\n";
  for (char c : frame) {
    Buffer::OwnedImpl temp_buffer(&c, 1);
    decoder_.decode(temp_buffer);
    EXPECT_EQ(0UL, temp_buffer.length());
  }
  ASSERT_EQ(1UL, decoded_values_.size());
  ASSERT_EQ(RespType::Map, decoded_values_[0]->type());
  const auto& arr = decoded_values_[0]->asArray();
  ASSERT_EQ(4UL, arr.size());
  EXPECT_EQ("b", arr[0].asString());
  EXPECT_EQ(RespType::Boolean, arr[1].type());
  EXPECT_TRUE(arr[1].asBoolean());
  EXPECT_EQ("d", arr[2].asString());
  ASSERT_EQ(RespType::Double, arr[3].type());
  EXPECT_EQ("3.14", arr[3].asString());
}

TEST_F(RedisEncoderDecoderImplTest, NullArray) {
  buffer_.add("*-1\r\n");
  decoder_.decode(buffer_);
  EXPECT_EQ(RespType::Null, decoded_values_[0]->type());
}

TEST_F(RedisEncoderDecoderImplTest, InvalidType) {
  buffer_.add("^");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "invalid value type");
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInteger) {
  buffer_.add(":-a");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "invalid integer character");
}

TEST_F(RedisEncoderDecoderImplTest, InvalidIntegerExpectLF) {
  buffer_.add(":-123\ra");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "expected new line");
}

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
  // Double — the payload is stored as raw wire bytes (pass-through).
  {
    RespValue v;
    v.type(RespType::Double);
    v.asString() = "0.5";
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
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "unbalanced quotes in request");
}

TEST_F(RedisEncoderDecoderImplTest, InlineCommandUnbalancedSingleQuote) {
  buffer_.add("'hello\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "unbalanced quotes in request");
}

// Cumulative-element budget on Map: a Map declaring N pairs counts as 2*N elements against
// ``kMaxTotalElements``. Walk five nested 800k arrays (consumes 4M of budget) and then declare
// a Map whose 2N count is below ``kMaxRespElements`` but pushes the cumulative total over
// ``kMaxTotalElements``. The cumulative check must reject before the element vector is
// allocated. (A single Map whose 2N storage size exceeds ``kMaxRespElements`` would hit the
// per-aggregate cap first — see ``Resp3MapCountExceedsMax``.)
TEST_F(RedisEncoderDecoderImplTest, MapTotalElementBudgetExceeded) {
  // Five nested *800k = 4_000_000 budget. Remaining = 4_194_304 - 4_000_000 = 194_304.
  // %97_200 → 2N = 194_400 > 194_304 → total-budget reject. 194_400 < kMaxRespElements (2M).
  std::string trip;
  for (int i = 0; i < 5; ++i) {
    trip += "*800000\r\n";
  }
  trip += "%97200\r\n";
  buffer_.add(trip);
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "total element count exceeds maximum");
}

// Map nesting depth: an outer Map containing nested Maps that reach kMaxNestingDepth is rejected
// at the pre-push check.
TEST_F(RedisEncoderDecoderImplTest, MapNestingDepthExceeded) {
  // Build a chain of "%1\r\n$1\r\nk\r\n" — each opens a Map of one pair whose value is the next
  // Map. The root frame counts toward depth, so a chain of kMaxNestingDepth aggregates drives the
  // pre-push check to the cap and is rejected, like the array NestingDepthRejected test.
  std::string nested;
  for (uint32_t i = 0; i < DecoderImpl::kMaxNestingDepth; ++i) {
    nested += "%1\r\n$1\r\nk\r\n";
  }
  buffer_.add(nested);
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "nesting depth exceeds maximum");
}

TEST_F(RedisEncoderDecoderImplTest, MapNestingDepthAtBoundaryAccepted) {
  // (kMaxNestingDepth - 1) nested single-pair Maps drive the stack to a depth of exactly
  // kMaxNestingDepth: the last accepted push happens when the pre-push depth is
  // kMaxNestingDepth - 1, producing a post-push depth of exactly kMaxNestingDepth. One additional
  // aggregate would be rejected (see MapNestingDepthExceeded). A leaf value for the innermost Map
  // lets the whole chain complete.
  std::string nested;
  for (uint32_t i = 0; i < DecoderImpl::kMaxNestingDepth - 1; ++i) {
    nested += "%1\r\n$1\r\nk\r\n";
  }
  nested += ":1\r\n"; // innermost Map's value: a leaf integer, completing the chain
  buffer_.add(nested);
  decoder_.decode(buffer_);
  ASSERT_EQ(1U, decoded_values_.size());
  EXPECT_EQ(RespType::Map, decoded_values_[0]->type());
}

// Empty integer/length lines must be rejected — without the digit_seen_ guard, ``:\r\n``,
// ``:-\r\n``, ``*\r\n``, ``$\r\n``, ``%-\r\n`` etc. would all be silently accepted as zero
// (indistinguishable on the wire from ``:0`` / ``*0`` / ``$0``), letting an attacker inject
// ambiguous frames the rest of the parser cannot tell apart from a legitimate zero-valued one.
TEST_F(RedisEncoderDecoderImplTest, IntegerWithNoDigitsRejected) {
  buffer_.add(":\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "integer with no digits");
}

TEST_F(RedisEncoderDecoderImplTest, NegativeIntegerWithNoDigitsRejected) {
  buffer_.add(":-\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "integer with no digits");
}

TEST_F(RedisEncoderDecoderImplTest, ArrayCountWithNoDigitsRejected) {
  buffer_.add("*\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "integer with no digits");
}

TEST_F(RedisEncoderDecoderImplTest, BulkStringLengthWithNoDigitsRejected) {
  buffer_.add("$\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "integer with no digits");
}

// The RESP3 count/length headers share the integer-parse path, so a missing digit must be
// rejected by the same ``digit_seen_`` guard rather than coerced to 0.
TEST_F(RedisEncoderDecoderImplTest, Resp3HeadersWithNoDigitsRejected) {
  for (const char* frame : {"%\r\n", "~\r\n", ">\r\n", "!\r\n", "=\r\n"}) {
    SCOPED_TRACE(frame);
    Buffer::OwnedImpl buffer;
    buffer.add(frame);
    DecoderImpl decoder(*this);
    EXPECT_THROW_WITH_MESSAGE(decoder.decode(buffer), ProtocolError, "integer with no digits");
  }
}

TEST_F(RedisEncoderDecoderImplTest, InvalidBulkStringExpectCR) {
  buffer_.add("$1\r\nab");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "expected carriage return");
}

TEST_F(RedisEncoderDecoderImplTest, InvalidBulkStringExpectLF) {
  buffer_.add("$1\r\na\ra");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "expected new line");
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
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "unbalanced quotes in request");
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandQuoting2) {
  buffer_ = Buffer::OwnedImpl();
  buffer_.add("\"\"\"\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "unbalanced quotes in request");
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandQuoting3) {
  buffer_ = Buffer::OwnedImpl();
  buffer_.add("\"foo\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "unbalanced quotes in request");
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandQuoting4) {
  buffer_ = Buffer::OwnedImpl();
  buffer_.add("\" foo\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "unbalanced quotes in request");
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandQuoting5) {
  buffer_ = Buffer::OwnedImpl();
  buffer_.add("\"fo\"o\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "unbalanced quotes in request");
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandSingleQuoting1) {
  buffer_.add("'\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "unbalanced quotes in request");
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandSingleQuoting2) {
  buffer_ = Buffer::OwnedImpl();
  buffer_.add("'''\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "unbalanced quotes in request");
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandSingleQuoting3) {
  buffer_ = Buffer::OwnedImpl();
  buffer_.add("'foo\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "unbalanced quotes in request");
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandSingleQuoting4) {
  buffer_ = Buffer::OwnedImpl();
  buffer_.add("' foo\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "unbalanced quotes in request");
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInlineCommandSingleQuoting5) {
  buffer_ = Buffer::OwnedImpl();
  buffer_.add("'fo'o\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "unbalanced quotes in request");
}

TEST_F(RedisEncoderDecoderImplTest, InvalidInterjectedInlineCommand) {
  buffer_.add("*1\r\nECHO\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "invalid value type");
}

// RESP3 Boolean tests
TEST_F(RedisEncoderDecoderImplTest, Resp3BooleanTrue) {
  // Decode #t\r\n
  buffer_.add("#t\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Boolean, decoded_values_[0]->type());
  EXPECT_TRUE(decoded_values_[0]->asBoolean());
  EXPECT_EQ("true", decoded_values_[0]->toString());
  EXPECT_EQ(0UL, buffer_.length());

  // Encode round-trip. The encoder defaults to RESP2; the RESP3 native wire form is only
  // emitted after it is explicitly put in RESP3 mode (matching how a real downstream
  // connection flips after HELLO 3 negotiation).
  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl out;
  encoder_.encode(*decoded_values_[0], out);
  EXPECT_EQ("#t\r\n", out.toString());
}

TEST_F(RedisEncoderDecoderImplTest, Resp3BooleanFalse) {
  buffer_.add("#f\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());
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
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "invalid boolean value");
}

// RESP3 Double pass-through: payload stored as raw bytes; round-trip is byte-identical.
TEST_F(RedisEncoderDecoderImplTest, Resp3Double) {
  buffer_.add(",3.14\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Double, decoded_values_[0]->type());
  EXPECT_EQ("3.14", decoded_values_[0]->asString());

  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl out;
  encoder_.encode(*decoded_values_[0], out);
  EXPECT_EQ(",3.14\r\n", out.toString());
}

// Non-canonical-but-parseable representations survive intact, byte-for-byte. Each of these forms
// changes under a parse-then-reformat round-trip ({:.17g}) — 3.140 and 1.2300 carry trailing
// zeros, +1e2 a leading sign and exponent, .5 an omitted integer part — so preserving them proves
// the decoder stores the raw wire bytes rather than re-synthesizing from the parsed double.
TEST_F(RedisEncoderDecoderImplTest, Resp3DoublePreservesUpstreamRepresentation) {
  const std::vector<std::string> raw_forms = {"3.140", "+1e2", ".5", "1.2300"};
  std::string frame;
  for (const std::string& raw : raw_forms) {
    frame += "," + raw + "\r\n";
  }
  buffer_.add(frame);
  decoder_.decode(buffer_);
  ASSERT_EQ(raw_forms.size(), decoded_values_.size());

  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  for (size_t i = 0; i < raw_forms.size(); ++i) {
    SCOPED_TRACE(raw_forms[i]);
    EXPECT_EQ(RespType::Double, decoded_values_[i]->type());
    EXPECT_EQ(raw_forms[i], decoded_values_[i]->asString());
    Buffer::OwnedImpl out;
    encoder_.encode(*decoded_values_[i], out);
    EXPECT_EQ("," + raw_forms[i] + "\r\n", out.toString());
  }
}

// RESP2 down-conversion wraps the raw payload as a bulk string (length = payload bytes).
TEST_F(RedisEncoderDecoderImplTest, Resp3DoubleDownconvertsToBulkStringVerbatim) {
  buffer_.add(",3.14\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());

  encoder_.setProtocolVersion(RespProtocolVersion::Resp2);
  Buffer::OwnedImpl out;
  encoder_.encode(*decoded_values_[0], out);
  EXPECT_EQ("$4\r\n3.14\r\n", out.toString());
}

TEST_F(RedisEncoderDecoderImplTest, Resp3DoubleNegative) {
  buffer_.add(",-1.5\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Double, decoded_values_[0]->type());
  EXPECT_EQ("-1.5", decoded_values_[0]->asString());
}

TEST_F(RedisEncoderDecoderImplTest, Resp3DoubleInf) {
  buffer_.add(",inf\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Double, decoded_values_[0]->type());
  EXPECT_EQ("inf", decoded_values_[0]->asString());
}

// RESP3 BigNumber test
TEST_F(RedisEncoderDecoderImplTest, Resp3BigNumber) {
  buffer_.add("(3492890328409238509324850943850943825024385\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());
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
  ASSERT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Null, decoded_values_[0]->type());
  EXPECT_EQ("null", decoded_values_[0]->toString());
}

// RESP3 BlobError test
TEST_F(RedisEncoderDecoderImplTest, Resp3BlobError) {
  buffer_.add("!12\r\nSYNTAX error\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());
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
  ASSERT_EQ(1UL, decoded_values_.size());
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
  ASSERT_EQ(1UL, decoded_values_.size());
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
  ASSERT_EQ(1UL, decoded_values_.size());
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
  ASSERT_EQ(1UL, decoded_values_.size());
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
  ASSERT_EQ(1UL, decoded_values_.size());
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
  ASSERT_EQ(1UL, decoded_values_.size());
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
  ASSERT_EQ(1UL, decoded_values_.size());
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
  double_val.asString() = "3.14";
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
  double1.asString() = "3.14";
  double2.type(RespType::Double);
  double2.asString() = "2.71";
  EXPECT_NE(double1, double2);
  double2.asString() = "3.14";
  EXPECT_EQ(double1, double2);

  // Byte-for-byte equality (stricter than IEEE): two payloads representing the same
  // numeric value but with different decimal forms compare unequal.
  RespValue d_short, d_long;
  d_short.type(RespType::Double);
  d_short.asString() = "3.14";
  d_long.type(RespType::Double);
  d_long.asString() = "3.1400000000000001";
  EXPECT_NE(d_short, d_long);

  // Cross-type inequality
  EXPECT_NE(bool1, double1);
}

// Multiple RESP3 values in sequence
TEST_F(RedisEncoderDecoderImplTest, Resp3MultipleValues) {
  buffer_.add("#t\r\n,3.14\r\n_\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(3UL, decoded_values_.size());
  EXPECT_EQ(RespType::Boolean, decoded_values_[0]->type());
  EXPECT_EQ(RespType::Double, decoded_values_[1]->type());
  EXPECT_EQ(RespType::Null, decoded_values_[2]->type());
}

// Nested RESP3: Map containing Push
TEST_F(RedisEncoderDecoderImplTest, Resp3NestedTypes) {
  // Map with one entry: key="ch", value=Push[message, ch, hello]
  buffer_.add("%1\r\n$2\r\nch\r\n>3\r\n$7\r\nmessage\r\n$2\r\nch\r\n$5\r\nhello\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Map, decoded_values_[0]->type());
  EXPECT_EQ(2UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ("ch", decoded_values_[0]->asArray()[0].asString());
  EXPECT_EQ(RespType::Push, decoded_values_[0]->asArray()[1].type());
  EXPECT_EQ(3UL, decoded_values_[0]->asArray()[1].asArray().size());
}

TEST_F(RedisEncoderDecoderImplTest, Resp3InvalidBigNumber) {
  buffer_.add("(123abc\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "invalid big number value");
}

TEST_F(RedisEncoderDecoderImplTest, Resp3NegativeBlobErrorLengthRejected) {
  buffer_.add("!-1\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "invalid negative length");
}

TEST_F(RedisEncoderDecoderImplTest, Resp3NegativeVerbatimStringLengthRejected) {
  buffer_.add("=-1\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "invalid negative length");
}

TEST_F(RedisEncoderDecoderImplTest, Resp3InvalidVerbatimStringPrefixRejected) {
  buffer_.add("=8\r\ntxthello\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "invalid verbatim string value");
}

// RESP3 Attribute: root-level attribute is parsed and discarded
// RESP3 Attribute: nested inside an array is parsed and discarded
TEST_F(RedisEncoderDecoderImplTest, Resp3AttributeNestedInArray) {
  // Array of 2 elements where element 0 is preceded by an attribute
  // *2\r\n |1\r\n+k\r\n+v\r\n :42\r\n :99\r\n
  buffer_.add("*2\r\n|1\r\n+k\r\n+v\r\n:42\r\n:99\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(2UL, decoded_values_[0]->asArray().size());
  EXPECT_EQ(42, decoded_values_[0]->asArray()[0].asInteger());
  EXPECT_EQ(99, decoded_values_[0]->asArray()[1].asInteger());
}

// RESP3 Attribute: empty attribute (0 entries)
TEST_F(RedisEncoderDecoderImplTest, Resp3AttributeEmpty) {
  buffer_.add("|0\r\n$3\r\nfoo\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::BulkString, decoded_values_[0]->type());
  EXPECT_EQ("foo", decoded_values_[0]->asString());
}

// Negative counts for RESP3 aggregate types produce Null
// RESP3 Map/Set/Push have NO null form. The RESP3 null type byte (``_``)
// is separate. Reject any negative count for these types — accepting
// ``%-1`` etc. as null would silently let an attacker smuggle ambiguous
// frames past callers that expect the documented type.
TEST_F(RedisEncoderDecoderImplTest, Resp3NegativeMapCountRejected) {
  buffer_.add("%-1\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "invalid negative count");
}

TEST_F(RedisEncoderDecoderImplTest, Resp3NegativeSetCountRejected) {
  buffer_.add("~-1\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "invalid negative count");
}

TEST_F(RedisEncoderDecoderImplTest, Resp3NegativePushCountRejected) {
  buffer_.add(">-1\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "invalid negative count");
}

// Only ``$-1`` and ``*-1`` are the RESP-defined null forms. Other negative
// counts/lengths are malformed and must be rejected.
TEST_F(RedisEncoderDecoderImplTest, BulkStringNegativeTwoRejected) {
  buffer_.add("$-2\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "invalid negative length");
}

TEST_F(RedisEncoderDecoderImplTest, ArrayNegativeTwoRejected) {
  buffer_.add("*-2\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "invalid negative count");
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
  ASSERT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Double, decoded_values_[0]->type());
  EXPECT_EQ("nan", decoded_values_[0]->asString());

  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl out;
  encoder_.encode(*decoded_values_[0], out);
  EXPECT_EQ(",nan\r\n", out.toString());
}

TEST_F(RedisEncoderDecoderImplTest, Resp3DoubleNegativeInf) {
  buffer_.add(",-inf\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Double, decoded_values_[0]->type());
  EXPECT_EQ("-inf", decoded_values_[0]->asString());

  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  Buffer::OwnedImpl out;
  encoder_.encode(*decoded_values_[0], out);
  EXPECT_EQ(",-inf\r\n", out.toString());
}

TEST_F(RedisEncoderDecoderImplTest, Resp3DoubleInvalid) {
  buffer_.add(",abc\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "invalid double value");
}

TEST_F(RedisEncoderDecoderImplTest, Resp3BigNumberPositive) {
  buffer_.add("(+12345\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::BigNumber, decoded_values_[0]->type());
  EXPECT_EQ("+12345", decoded_values_[0]->asString());
}

TEST_F(RedisEncoderDecoderImplTest, Resp3ArrayCountExceedsMax) {
  // One past kMaxRespElements. Reference the constant symbolically so this stays a boundary
  // test if the limit ever changes (sibling limit tests already do this).
  buffer_.add(absl::StrCat("*", DecoderImpl::kMaxRespElements + 1, "\r\n"));
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "element count exceeds maximum");
}

// Inline commands carry no element-count header, so the per-aggregate cap is enforced per
// appended word instead (the ``*``-framed equivalent is rejected at its length line above).
// Without the cap, an endless space-separated word stream with no CRLF allocates one RespValue
// per word without bound. kMaxRespElements words fill the array to the cap; word cap+1 throws.
TEST_F(RedisEncoderDecoderImplTest, InlineCommandElementCountExceedsMax) {
  std::string words((DecoderImpl::kMaxRespElements + 1) * 2, 'a');
  for (size_t i = 1; i < words.size(); i += 2) {
    words[i] = ' ';
  }
  buffer_.add(words);
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "inline command element count exceeds maximum");
}

// A discarded root-level attribute releases its element storage, so the cumulative element
// budget must reset for the value that follows. A single aggregate is capped at
// kMaxRespElements — too small to cross the cumulative budget together with one more
// maximum-count array — so the attribute uses one level of nesting to consume
// 2 + 2*kNested slots. Combined with the trailing maximum-count array header this crosses
// kMaxTotalElements; before the reset that header was rejected with "total element count
// exceeds maximum" even though the attribute's storage was already freed.
TEST_F(RedisEncoderDecoderImplTest, Resp3RootAttributeDiscardResetsTotalElementBudget) {
  constexpr uint64_t kNested = 1097152;
  static_assert(kNested <= DecoderImpl::kMaxRespElements,
                "each aggregate must individually satisfy the per-aggregate cap");
  static_assert(2 + 2 * kNested + DecoderImpl::kMaxRespElements > DecoderImpl::kMaxTotalElements,
                "attribute + array must cross the cumulative budget for this test to bite");
  // |1 -> [key = null, value = outer array(kNested) whose first element is inner
  // array(kNested)]; the remaining slots are filled with nulls.
  buffer_.add(absl::StrCat("|1\r\n_\r\n*", kNested, "\r\n*", kNested, "\r\n"));
  std::string nulls;
  nulls.reserve((2 * kNested - 1) * 3);
  for (uint64_t i = 0; i < 2 * kNested - 1; i++) {
    nulls += "_\r\n";
  }
  buffer_.add(nulls);
  decoder_.decode(buffer_); // The attribute completes and is discarded; nothing is delivered.
  EXPECT_TRUE(decoded_values_.empty());

  buffer_.add(absl::StrCat("*", DecoderImpl::kMaxRespElements, "\r\n_\r\n"));
  decoder_.decode(buffer_); // Must not throw; the large array is still buffering.
  EXPECT_TRUE(decoded_values_.empty());
}

// Per-aggregate cap on Map: a Map header whose post-multiply 2N exceeds ``kMaxRespElements``
// is rejected at the first cap (before the cumulative cap). N = kMaxRespElements/2 + 1 → 2N > max.
TEST_F(RedisEncoderDecoderImplTest, Resp3MapCountExceedsMax) {
  buffer_.add(absl::StrCat("%", DecoderImpl::kMaxRespElements / 2 + 1, "\r\n"));
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "element count exceeds maximum");
}

TEST_F(RedisEncoderDecoderImplTest, Resp3SetCountExceedsMax) {
  buffer_.add(absl::StrCat("~", DecoderImpl::kMaxRespElements + 1, "\r\n"));
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "element count exceeds maximum");
}

TEST_F(RedisEncoderDecoderImplTest, Resp3PushCountExceedsMax) {
  buffer_.add(absl::StrCat(">", DecoderImpl::kMaxRespElements + 1, "\r\n"));
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "element count exceeds maximum");
}

// Pre-multiply integer overflow guards. uint64_t::max is 20 digits
// (18446744073709551615); 25-digit inputs would wrap during accumulation
// and slip past every post-LF semantic check (kMaxRespElements,
// kMaxTotalElements, Map *2 cap), letting an attacker land any chosen value
// in the integer accumulator.
TEST_F(RedisEncoderDecoderImplTest, Resp2ArrayCountIntegerOverflow) {
  buffer_.add("*9999999999999999999999999\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "integer overflow");
}

TEST_F(RedisEncoderDecoderImplTest, Resp2BulkStringLengthIntegerOverflow) {
  buffer_.add("$9999999999999999999999999\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "integer overflow");
}

TEST_F(RedisEncoderDecoderImplTest, Resp3MapCountIntegerOverflow) {
  buffer_.add("%9999999999999999999999999\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "integer overflow");
}

TEST_F(RedisEncoderDecoderImplTest, Resp3SetCountIntegerOverflow) {
  buffer_.add("~9999999999999999999999999\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "integer overflow");
}

// Signed RESP integer (`:N\r\n`) range checks. The accumulator is uint64_t
// but asInteger() is int64_t — wire values outside [INT64_MIN, INT64_MAX]
// would silently wrap on cast. Reject at parse time.
TEST_F(RedisEncoderDecoderImplTest, RespIntegerInt64MaxAccepted) {
  buffer_.add(":9223372036854775807\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Integer, decoded_values_[0]->type());
  EXPECT_EQ(std::numeric_limits<int64_t>::max(), decoded_values_[0]->asInteger());
}

TEST_F(RedisEncoderDecoderImplTest, RespIntegerInt64MaxPlusOneRejected) {
  buffer_.add(":9223372036854775808\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "integer out of range");
}

TEST_F(RedisEncoderDecoderImplTest, RespIntegerInt64MinAccepted) {
  buffer_.add(":-9223372036854775808\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Integer, decoded_values_[0]->type());
  EXPECT_EQ(std::numeric_limits<int64_t>::min(), decoded_values_[0]->asInteger());
}

TEST_F(RedisEncoderDecoderImplTest, RespIntegerInt64MinMinusOneRejected) {
  buffer_.add(":-9223372036854775809\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "integer out of range");
}

TEST_F(RedisEncoderDecoderImplTest, RespIntegerNegativeZeroAcceptedAsZero) {
  // Wire "-0" → 0. Tolerate for backward-compat with senders that emit it.
  buffer_.add(":-0\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::Integer, decoded_values_[0]->type());
  EXPECT_EQ(0, decoded_values_[0]->asInteger());
}

// Bulk-string length cap (512 MiB, matches Redis proto-max-bulk-len). The
// header is rejected BEFORE the body-read loop so an attacker-supplied
// length cannot drive unbounded string growth.
TEST_F(RedisEncoderDecoderImplTest, BulkStringLengthAboveCapRejected) {
  // 512 MiB + 1 byte.
  buffer_.add("$536870913\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "bulk string length exceeds maximum");
}

TEST_F(RedisEncoderDecoderImplTest, VerbatimStringLengthAboveCapRejected) {
  buffer_.add("=536870913\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "bulk string length exceeds maximum");
}

TEST_F(RedisEncoderDecoderImplTest, BlobErrorLengthAboveCapRejected) {
  buffer_.add("!536870913\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "bulk string length exceeds maximum");
}

TEST_F(RedisEncoderDecoderImplTest, Resp3BigNumberLoneSign) {
  buffer_.add("(-\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "invalid big number value");
}

TEST_F(RedisEncoderDecoderImplTest, Resp3BigNumberLonePositiveSign) {
  buffer_.add("(+\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "invalid big number value");
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

// The Double payload is a raw pass-through of the decoded bytes; the RESP2 down-conversion wraps
// exactly those bytes as a bulk string and RESP3 re-frames them under ``,``.
TEST_F(RespEncoderDownconversionTest, DoubleAsBulkString) {
  RespValue v;
  v.type(RespType::Double);
  v.asString() = "3.14";

  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ("$4\r\n3.14\r\n", out.toString());

  Buffer::OwnedImpl resp3_out;
  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  encoder_.encode(v, resp3_out);
  EXPECT_EQ(",3.14\r\n", resp3_out.toString());
}

// Bare-integer Double payloads (the form Redis itself emits for integral values) survive both
// frames without gaining a decimal point.
TEST_F(RespEncoderDownconversionTest, DoubleIntegerValuedHasNoDecimal) {
  RespValue v;
  v.type(RespType::Double);
  v.asString() = "1";
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ("$1\r\n1\r\n", out.toString());

  Buffer::OwnedImpl resp3_out;
  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  encoder_.encode(v, resp3_out);
  EXPECT_EQ(",1\r\n", resp3_out.toString());
}

TEST_F(RespEncoderDownconversionTest, DoubleInfAsBulkString) {
  RespValue v;
  v.type(RespType::Double);
  v.asString() = "inf";
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ("$3\r\ninf\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, DoubleNegInfAsBulkString) {
  RespValue v;
  v.type(RespType::Double);
  v.asString() = "-inf";
  Buffer::OwnedImpl out;
  encodeResp2(v, out);
  EXPECT_EQ("$4\r\n-inf\r\n", out.toString());
}

TEST_F(RespEncoderDownconversionTest, DoubleNanAsBulkString) {
  RespValue v;
  v.type(RespType::Double);
  v.asString() = "nan";
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
  // Inside a 1-element array, an attribute (|1) annotates the value that follows it. The attribute
  // and its key/value payload must be discarded, leaving only the post-attribute value as the sole
  // array element. Distinct strings make the discard unambiguous: were the parser to surface the
  // attribute's value ("meta-val") — or leave its bytes unconsumed as a spurious extra root — the
  // assertions below would catch it.
  buffer_.add("*1\r\n|1\r\n+meta-key\r\n+meta-val\r\n+real\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1U, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  ASSERT_EQ(1U, decoded_values_[0]->asArray().size());
  EXPECT_EQ(RespType::SimpleString, decoded_values_[0]->asArray()[0].type());
  EXPECT_EQ("real", decoded_values_[0]->asArray()[0].asString());
}

TEST_F(RedisEncoderDecoderImplTest, AttributeFloodRejected) {
  // One more than kMaxConsecutiveAttributes consecutive attributes trips the flood guard.
  std::string flood;
  for (uint32_t i = 0; i < DecoderImpl::kMaxConsecutiveAttributes + 1; ++i) {
    flood += "|0\r\n";
  }
  flood += "+OK\r\n";
  buffer_.add(flood);
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "too many consecutive RESP3 attributes");
}

// The flood guard must count NON-EMPTY attributes too: ``|1 <k> <v>`` repeated. A child value
// completing inside an attribute frame must not reset consecutive_attributes_ — otherwise the
// guard only ever catches the ``|0`` case. One past the cap trips it before the cumulative
// element budget would.
TEST_F(RedisEncoderDecoderImplTest, NonEmptyAttributeFloodRejected) {
  std::string flood;
  for (uint32_t i = 0; i < DecoderImpl::kMaxConsecutiveAttributes + 1; ++i) {
    flood += "|1\r\n+k\r\n+v\r\n";
  }
  flood += "+OK\r\n";
  buffer_.add(flood);
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "too many consecutive RESP3 attributes");
}

// A non-empty attribute followed by its annotated value is accepted and the value is delivered:
// the reset happens on the value OUTSIDE any attribute, so a normal ``|1 k v <value>`` decodes.
TEST_F(RedisEncoderDecoderImplTest, NonEmptyAttributeThenValueAccepted) {
  buffer_.add("|1\r\n+key\r\n+val\r\n+OK\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1UL, decoded_values_.size());
  EXPECT_EQ(RespType::SimpleString, decoded_values_[0]->type());
  EXPECT_EQ("OK", decoded_values_[0]->asString());
}

TEST_F(RedisEncoderDecoderImplTest, MapCountOverflowRejected) {
  // A map count that would overflow when doubled for the flat 2N layout.
  // uint64_max/2 + 1 pairs -> element count would wrap.
  buffer_.add("%9223372036854775808\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "map element count overflow");
}

TEST_F(RedisEncoderDecoderImplTest, VerbatimStringMissingColonRejected) {
  // RESP3 verbatim strings require a 3-char format prefix + ':' at byte 3.
  buffer_.add("=6\r\ntxtXXX\r\n");
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "invalid verbatim string value");
}

TEST_F(RedisEncoderDecoderImplTest, DoubleTooLongRejected) {
  // The Double payload is capped at kMaxDoubleTokenLength — a double is a small fixed-width
  // scalar, so anything longer is outside the accepted scalar-token bound used to protect against
  // unterminated growth. One byte over the cap trips the guard.
  std::string long_double = ",";
  long_double.append(DecoderImpl::kMaxDoubleTokenLength + 1, '9');
  long_double += "\r\n";
  buffer_.add(long_double);
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "double value too long");
}

TEST_F(RedisEncoderDecoderImplTest, DoubleAtBoundaryAccepted) {
  // A Double payload of exactly kMaxDoubleTokenLength bytes is accepted; one byte over the cap is
  // what trips the guard (see DoubleTooLongRejected). An all-digit token is a valid (large) double.
  const std::string digits(DecoderImpl::kMaxDoubleTokenLength, '9');
  buffer_.add("," + digits + "\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1U, decoded_values_.size());
  EXPECT_EQ(RespType::Double, decoded_values_[0]->type());
  EXPECT_EQ(digits, decoded_values_[0]->asString());
}

// A Double payload containing whitespace or a control byte must be rejected during
// accumulation, not silently accepted by the double parser (which tolerates surrounding whitespace)
// and re-emitted verbatim. An embedded bare ``\n`` is the dangerous case: only ``\r`` ends the
// token, so ``,1.5\n\r\n`` would otherwise store "1.5\n" and desynchronize a newline-splitting
// reader.
TEST_F(RedisEncoderDecoderImplTest, DoubleWithWhitespaceOrControlByteRejected) {
  for (const char* frame : {",1.5\n\r\n", ", 1.5\r\n", ",1.5 \r\n", ",1\t5\r\n"}) {
    SCOPED_TRACE(frame);
    Buffer::OwnedImpl buffer;
    buffer.add(frame);
    DecoderImpl decoder(*this);
    EXPECT_THROW_WITH_MESSAGE(decoder.decode(buffer), ProtocolError, "invalid double value");
  }
}

// The rejection is a charset check, not a numeric-format check: exponents, signs, and the
// special inf/nan tokens are still accepted (the double parser accepts them; none contain
// whitespace).
TEST_F(RedisEncoderDecoderImplTest, DoubleAcceptsExponentAndSpecialForms) {
  for (const char* frame : {",3.0e2\r\n", ",-2.5E-3\r\n", ",inf\r\n", ",-inf\r\n", ",nan\r\n"}) {
    SCOPED_TRACE(frame);
    Buffer::OwnedImpl buffer;
    DecoderImpl decoder(*this);
    buffer.add(frame);
    decoder.decode(buffer);
  }
}

TEST_F(RedisEncoderDecoderImplTest, BigNumberTooLongRejected) {
  // BigNumber is arbitrary-precision, so a long value is not malformed; the payload is capped at
  // kMaxBigNumberTokenLength purely to bound unbounded CRLF-less line growth. Exceed the cap by
  // one byte to trip the guard.
  std::string long_bignum = "(";
  long_bignum.append(DecoderImpl::kMaxBigNumberTokenLength + 1, '9');
  long_bignum += "\r\n";
  buffer_.add(long_bignum);
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError, "big number value too long");
}

TEST_F(RedisEncoderDecoderImplTest, BigNumberAtBoundaryAccepted) {
  // A BigNumber payload of exactly kMaxBigNumberTokenLength bytes is accepted; one byte over the
  // cap is what trips the guard (see BigNumberTooLongRejected).
  const std::string digits(DecoderImpl::kMaxBigNumberTokenLength, '9');
  buffer_.add("(" + digits + "\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1U, decoded_values_.size());
  EXPECT_EQ(RespType::BigNumber, decoded_values_[0]->type());
  EXPECT_EQ(digits, decoded_values_[0]->asString());
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
  // 128 nested ``*1`` frames: the pre-push check
  // (``pending_value_stack_depth_ >= kMaxNestingDepth``) rejects the 128th ``*1`` because
  // the prior frames (root setup at depth=1 plus 126 successful nested-array pushes)
  // already drove the depth counter to ``kMaxNestingDepth`` (= 128). No 129th-level push
  // happens; the counter is never incremented past 128.
  std::string deep;
  for (uint32_t i = 0; i < DecoderImpl::kMaxNestingDepth; ++i) {
    deep += "*1\r\n";
  }
  deep += "+x\r\n";
  buffer_.add(deep);
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "nesting depth exceeds maximum");
}

TEST_F(RedisEncoderDecoderImplTest, NestingDepthAtBoundaryAccepted) {
  // 127 nested *1 frames + leaf reach a stack depth of 128 (root + 127 pushes),
  // which is exactly kMaxNestingDepth. The pre-push check
  // (depth >= kMaxNestingDepth) lets the 127th push through (depth was 127
  // before) and rejects a 128th push (depth 128 already at the cap).
  std::string deep;
  for (uint32_t i = 0; i < DecoderImpl::kMaxNestingDepth - 1; ++i) {
    deep += "*1\r\n";
  }
  deep += "+x\r\n";
  buffer_.add(deep);
  decoder_.decode(buffer_);
  ASSERT_EQ(1U, decoded_values_.size());
}

TEST_F(RedisEncoderDecoderImplTest, TotalElementBudgetRejected) {
  // Stack five 800k aggregates: each is below the per-aggregate 2M cap, but the running budget
  // reaches 4M after the fifth (only ~194k of the 4_194_304 kMaxTotalElements remains). A sixth
  // 800k aggregate exceeds that remainder, so the cumulative check rejects it before its element
  // vector is allocated.
  // Nesting works because the parser accepts a nested aggregate as the first element of its
  // parent — we don't need to provide actual leaf bytes for the budget check to fire.
  std::string trip;
  for (int i = 0; i < 5; ++i) {
    trip += "*800000\r\n";
  }
  trip += "*800001\r\n";
  buffer_.add(trip);
  EXPECT_THROW_WITH_MESSAGE(decoder_.decode(buffer_), ProtocolError,
                            "total element count exceeds maximum");
}

TEST_F(RedisEncoderDecoderImplTest, TotalElementBudgetResetsBetweenRootValues) {
  // The cumulative-element budget must reset to zero at the start of each top-level value, so a
  // pipelined second request is judged on its own size rather than inheriting the first request's
  // spend. Root 1 is a tiny array that completes and leaves total_elements_ at 3. Root 2 then
  // stacks the same five 800k aggregates as TotalElementBudgetRejected plus a final 194_304
  // aggregate, summing to exactly kMaxTotalElements (4_000_000 + 194_304 = 4_194_304). That final
  // declaration is accepted ONLY if the budget reset when root 2 began; without the reset, root 1's
  // leftover 3 pushes the running total one past the cap and it throws. Root 2's leaves are never
  // supplied — the budget check fires at declaration, so we assert root 2 was admitted (no throw)
  // and is still awaiting its elements rather than completing.
  buffer_.add("*3\r\n+a\r\n+b\r\n+c\r\n");
  std::string root2;
  for (int i = 0; i < 5; ++i) {
    root2 += "*800000\r\n";
  }
  root2 += "*194304\r\n";
  buffer_.add(root2);
  decoder_.decode(buffer_);
  // Root 1 fully decoded; root 2 accepted all declarations (proving the reset) and awaits elements,
  // so it has not yet been emitted.
  ASSERT_EQ(1U, decoded_values_.size());
  EXPECT_EQ(RespType::Array, decoded_values_[0]->type());
  EXPECT_EQ(3U, decoded_values_[0]->asArray().size());
}

TEST_F(RedisEncoderDecoderImplTest, MapStorageIsFlat2N) {
  // Verify the storage invariant: a %N Map is stored as a 2N-element vector.
  buffer_.add("%2\r\n+a\r\n:1\r\n+b\r\n:2\r\n");
  decoder_.decode(buffer_);
  ASSERT_EQ(1U, decoded_values_.size());
  EXPECT_EQ(RespType::Map, decoded_values_[0]->type());
  EXPECT_EQ(4U, decoded_values_[0]->asArray().size());
}

TEST_F(RedisEncoderDecoderImplTest, EncodeMapOddSizedTripsEnvoyBug) {
  // A Map RespValue must store a flat 2N key/value vector. An odd-sized array is a caller bug;
  // encodeMap trips ENVOY_BUG (fatal in debug; release-safe truncate-to-even in production) rather
  // than emitting a frame whose declared count omits the trailing element and shifts the decoder.
  RespValue map;
  map.type(RespType::Map);
  RespValue k, v, orphan;
  k.type(RespType::SimpleString);
  k.asString() = "k";
  v.type(RespType::SimpleString);
  v.asString() = "v";
  orphan.type(RespType::SimpleString);
  orphan.asString() = "orphan";
  map.asArray() = {k, v, orphan};

  Buffer::OwnedImpl out;
  encoder_.setProtocolVersion(RespProtocolVersion::Resp3);
  EXPECT_ENVOY_BUG(encoder_.encode(map, out), "Map storage must have even length");
#if defined(NDEBUG) || defined(ENVOY_CONFIG_COVERAGE)
  // Where ENVOY_BUG is non-fatal (release / coverage builds), the encode continues past the bug and
  // emits the release-safe, truncated-to-even Map — the orphan element is dropped, leaving a
  // correctly framed 1-pair Map. In a debug build EXPECT_ENVOY_BUG aborts the encode before this
  // point (it is a death test), so the output check only applies in non-debug builds.
  EXPECT_EQ("%1\r\n+k\r\n+v\r\n", out.toString());
#endif
}

// The same odd-Map caller bug must be release-safe on the RESP2 down-convert path too: the
// even-length invariant is enforced in encode()'s Map case (before the version branch), so the
// orphan element is dropped for both %-framed RESP3 output and the *-framed RESP2 array — a
// prior version enforced it only inside encodeMap, letting RESP2 emit the stray trailing frame.
TEST_F(RedisEncoderDecoderImplTest, EncodeMapOddSizedToResp2DropsOrphanElement) {
  RespValue map;
  map.type(RespType::Map);
  RespValue k, v, orphan;
  k.type(RespType::SimpleString);
  k.asString() = "k";
  v.type(RespType::SimpleString);
  v.asString() = "v";
  orphan.type(RespType::SimpleString);
  orphan.asString() = "orphan";
  map.asArray() = {k, v, orphan};

  Buffer::OwnedImpl out;
  encoder_.setProtocolVersion(RespProtocolVersion::Resp2);
  EXPECT_ENVOY_BUG(encoder_.encode(map, out), "Map storage must have even length");
#if defined(NDEBUG) || defined(ENVOY_CONFIG_COVERAGE)
  // RESP2 has no map type, so the pairs are emitted as a flat array; the orphan is dropped so
  // the ``*2`` count matches the element count.
  EXPECT_EQ("*2\r\n+k\r\n+v\r\n", out.toString());
#endif
}

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
