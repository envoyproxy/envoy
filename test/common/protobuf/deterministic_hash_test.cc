#include "source/common/protobuf/deterministic_hash.h"

#include "test/common/protobuf/deterministic_hash_test.pb.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace DeterministicProtoHash {

TEST(HashTest, EmptyMessageHashDiffersByMessageType) {
  deterministichashtest::Maps empty1;
  deterministichashtest::SingleFields empty2;
  EXPECT_NE(hash(empty1), hash(empty2));
}

TEST(HashTest, EmptyMessageHashMatches) {
  deterministichashtest::Maps empty1, empty2;
  EXPECT_EQ(hash(empty1), hash(empty2));
}

TEST(HashTest, MapWithRemovedValueBehavesTheSameAsEmptyMap) {
  // This test is from an abundance of caution, to make sure that
  // reflection can't be driven to traverse a map field with no values
  // in it (there would be an ASSERT in that path.)
  deterministichashtest::Maps empty1, empty2;
  (*empty1.mutable_bool_string())[false] = "false";
  (*empty1.mutable_bool_string()).erase(false);
  EXPECT_EQ(hash(empty1), hash(empty2));
}

TEST(HashTest, BoolKeyedMapIsInsertionOrderAgnostic) {
  deterministichashtest::Maps map1, map2;
  (*map1.mutable_bool_string())[false] = "false";
  (*map1.mutable_bool_string())[true] = "true";
  (*map2.mutable_bool_string())[true] = "true";
  (*map2.mutable_bool_string())[false] = "false";
  EXPECT_EQ(hash(map1), hash(map2));
}

TEST(HashTest, StringKeyedMapIsInsertionOrderAgnostic) {
  deterministichashtest::Maps map1, map2;
  (*map1.mutable_string_bool())["false"] = false;
  (*map1.mutable_string_bool())["true"] = true;
  (*map2.mutable_string_bool())["true"] = true;
  (*map2.mutable_string_bool())["false"] = false;
  EXPECT_EQ(hash(map1), hash(map2));
}

TEST(HashTest, Int32KeyedMapIsInsertionOrderAgnostic) {
  deterministichashtest::Maps map1, map2;
  (*map1.mutable_int32_uint32())[-5] = 5;
  (*map1.mutable_int32_uint32())[-8] = 8;
  (*map2.mutable_int32_uint32())[-8] = 8;
  (*map2.mutable_int32_uint32())[-5] = 5;
  EXPECT_EQ(hash(map1), hash(map2));
}

TEST(HashTest, UInt32KeyedMapIsInsertionOrderAgnostic) {
  deterministichashtest::Maps map1, map2;
  (*map1.mutable_uint32_int32())[5] = -5;
  (*map1.mutable_uint32_int32())[8] = -8;
  (*map2.mutable_uint32_int32())[8] = -8;
  (*map2.mutable_uint32_int32())[5] = -5;
  EXPECT_EQ(hash(map1), hash(map2));
}

TEST(HashTest, Int64KeyedMapIsInsertionOrderAgnostic) {
  deterministichashtest::Maps map1, map2;
  (*map1.mutable_int64_uint64())[-5] = 5;
  (*map1.mutable_int64_uint64())[-8] = 8;
  (*map2.mutable_int64_uint64())[-8] = 8;
  (*map2.mutable_int64_uint64())[-5] = 5;
  EXPECT_EQ(hash(map1), hash(map2));
}

TEST(HashTest, UInt64KeyedMapIsInsertionOrderAgnostic) {
  deterministichashtest::Maps map1, map2;
  (*map1.mutable_uint64_int64())[5] = -5;
  (*map1.mutable_uint64_int64())[8] = -8;
  (*map2.mutable_uint64_int64())[8] = -8;
  (*map2.mutable_uint64_int64())[5] = -5;
  EXPECT_EQ(hash(map1), hash(map2));
}

TEST(HashTest, MapWithSameKeysAndValuesPairedDifferentlyDoesNotMatch) {
  deterministichashtest::Maps map1, map2;
  (*map1.mutable_string_bool())["false"] = false;
  (*map1.mutable_string_bool())["true"] = true;
  (*map2.mutable_string_bool())["true"] = false;
  (*map2.mutable_string_bool())["false"] = true;
  EXPECT_NE(hash(map1), hash(map2));
}

TEST(HashTest, RecursiveMessageMatchesWhenSame) {
  deterministichashtest::Recursion r1, r2;
  r1.set_index(0);
  r1.mutable_child()->set_index(1);
  r2.set_index(0);
  r2.mutable_child()->set_index(1);
  EXPECT_EQ(hash(r1), hash(r2));
}

TEST(HashTest, RecursiveMessageDoesNotMatchWhenSameValuesInDifferentOrder) {
  deterministichashtest::Recursion r1, r2;
  r1.set_index(0);
  r1.mutable_child()->set_index(1);
  r2.set_index(1);
  r2.mutable_child()->set_index(0);
  EXPECT_NE(hash(r1), hash(r2));
}

TEST(HashTest, RecursiveMessageDoesNotMatchWithDifferentDepth) {
  deterministichashtest::Recursion r1, r2;
  r1.set_index(0);
  r1.mutable_child()->set_index(1);
  r2.set_index(0);
  r2.mutable_child()->set_index(1);
  r2.mutable_child()->mutable_child();
  EXPECT_NE(hash(r1), hash(r2));
}

TEST(HashTest, MatchingRepeatedBoolsMatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_bools(false);
  r1.add_bools(true);
  r2.add_bools(false);
  r2.add_bools(true);
  EXPECT_EQ(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedBoolsDifferentOrderMismatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_bools(false);
  r1.add_bools(true);
  r2.add_bools(true);
  r2.add_bools(false);
  EXPECT_NE(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedBoolsDifferentLengthMismatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_bools(false);
  r2.add_bools(false);
  r2.add_bools(false);
  EXPECT_NE(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedStringsMatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_strings("foo");
  r1.add_strings("bar");
  r2.add_strings("foo");
  r2.add_strings("bar");
  EXPECT_EQ(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedStringsDifferentOrderMismatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_strings("foo");
  r1.add_strings("bar");
  r2.add_strings("bar");
  r2.add_strings("foo");
  EXPECT_NE(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedBytesMatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_byteses("\x01\x02\x03\x04");
  r1.add_byteses("\x01\x02\x03\x04\x05");
  r2.add_byteses("\x01\x02\x03\x04");
  r2.add_byteses("\x01\x02\x03\x04\x05");
  EXPECT_EQ(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedBytesDifferentOrderMismatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_byteses("\x01\x02\x03\x04");
  r1.add_byteses("\x01\x02\x03\x04\x05");
  r2.add_byteses("\x01\x02\x03\x04\x05");
  r2.add_byteses("\x01\x02\x03\x04");
  EXPECT_NE(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedInt32Match) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_int32s(5);
  r1.add_int32s(8);
  r2.add_int32s(5);
  r2.add_int32s(8);
  EXPECT_EQ(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedInt32DifferentOrderMismatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_int32s(5);
  r1.add_int32s(8);
  r2.add_int32s(8);
  r2.add_int32s(5);
  EXPECT_NE(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedUInt32Match) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_uint32s(5);
  r1.add_uint32s(8);
  r2.add_uint32s(5);
  r2.add_uint32s(8);
  EXPECT_EQ(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedUInt32DifferentOrderMismatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_uint32s(5);
  r1.add_uint32s(8);
  r2.add_uint32s(8);
  r2.add_uint32s(5);
  EXPECT_NE(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedInt64Match) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_int64s(5);
  r1.add_int64s(8);
  r2.add_int64s(5);
  r2.add_int64s(8);
  EXPECT_EQ(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedInt64DifferentOrderMismatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_int64s(5);
  r1.add_int64s(8);
  r2.add_int64s(8);
  r2.add_int64s(5);
  EXPECT_NE(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedUInt64Match) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_uint64s(5);
  r1.add_uint64s(8);
  r2.add_uint64s(5);
  r2.add_uint64s(8);
  EXPECT_EQ(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedUInt64DifferentOrderMismatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_uint64s(5);
  r1.add_uint64s(8);
  r2.add_uint64s(8);
  r2.add_uint64s(5);
  EXPECT_NE(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedEnumMatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_enums(deterministichashtest::FOO);
  r1.add_enums(deterministichashtest::BAR);
  r2.add_enums(deterministichashtest::FOO);
  r2.add_enums(deterministichashtest::BAR);
  EXPECT_EQ(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedEnumDifferentOrderMismatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_enums(deterministichashtest::FOO);
  r1.add_enums(deterministichashtest::BAR);
  r2.add_enums(deterministichashtest::BAR);
  r2.add_enums(deterministichashtest::FOO);
  EXPECT_NE(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedDoubleMatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_doubles(1.84);
  r1.add_doubles(-4.88);
  r2.add_doubles(1.84);
  r2.add_doubles(-4.88);
  EXPECT_EQ(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedDoubleDifferentOrderMismatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_doubles(1.84);
  r1.add_doubles(-4.88);
  r2.add_doubles(-4.88);
  r2.add_doubles(1.84);
  EXPECT_NE(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedFloatMatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_floats(1.84f);
  r1.add_floats(-4.88f);
  r2.add_floats(1.84f);
  r2.add_floats(-4.88f);
  EXPECT_EQ(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedFloatDifferentOrderMismatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_floats(1.84f);
  r1.add_floats(-4.88f);
  r2.add_floats(-4.88f);
  r2.add_floats(1.84f);
  EXPECT_NE(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedMessageMatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_messages()->set_index(1);
  r1.add_messages()->set_index(2);
  r2.add_messages()->set_index(1);
  r2.add_messages()->set_index(2);
  EXPECT_EQ(hash(r1), hash(r2));
}

TEST(HashTest, RepeatedMessageOrderMisMatch) {
  deterministichashtest::RepeatedFields r1, r2;
  r1.add_messages()->set_index(1);
  r1.add_messages()->set_index(2);
  r2.add_messages()->set_index(2);
  r2.add_messages()->set_index(1);
  EXPECT_NE(hash(r1), hash(r2));
}

TEST(HashTest, SingleInt32Match) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_int32(5);
  s2.set_int32(5);
  EXPECT_EQ(hash(s1), hash(s2));
}

TEST(HashTest, SingleInt32Mismatch) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_int32(5);
  s2.set_int32(8);
  EXPECT_NE(hash(s1), hash(s2));
}

TEST(HashTest, SingleUInt32Match) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_uint32(5);
  s2.set_uint32(5);
  EXPECT_EQ(hash(s1), hash(s2));
}

TEST(HashTest, SingleUInt32Mismatch) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_uint32(5);
  s2.set_uint32(8);
  EXPECT_NE(hash(s1), hash(s2));
}

TEST(HashTest, SingleInt64Match) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_int64(5);
  s2.set_int64(5);
  EXPECT_EQ(hash(s1), hash(s2));
}

TEST(HashTest, SingleInt64Mismatch) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_int64(5);
  s2.set_int64(8);
  EXPECT_NE(hash(s1), hash(s2));
}

TEST(HashTest, SingleUInt64Match) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_uint64(5);
  s2.set_uint64(5);
  EXPECT_EQ(hash(s1), hash(s2));
}

TEST(HashTest, SingleUInt64Mismatch) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_uint64(5);
  s2.set_uint64(8);
  EXPECT_NE(hash(s1), hash(s2));
}

TEST(HashTest, SingleBoolMatch) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_b(true);
  s2.set_b(true);
  EXPECT_EQ(hash(s1), hash(s2));
}

TEST(HashTest, SingleBoolMismatch) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_b(true);
  s2.set_b(false);
  EXPECT_NE(hash(s1), hash(s2));
}

TEST(HashTest, SingleStringMatch) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_string("true");
  s2.set_string("true");
  EXPECT_EQ(hash(s1), hash(s2));
}

TEST(HashTest, SingleStringMismatch) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_string("true");
  s2.set_string("false");
  EXPECT_NE(hash(s1), hash(s2));
}

TEST(HashTest, SingleBytesMatch) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_bytes("\x01\x02");
  s2.set_bytes("\x01\x02");
  EXPECT_EQ(hash(s1), hash(s2));
}

TEST(HashTest, SingleBytesMismatch) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_bytes("\x01\x02");
  s2.set_bytes("\x01\x02\x03");
  EXPECT_NE(hash(s1), hash(s2));
}

TEST(HashTest, SingleDoubleMatch) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_db(3.9);
  s2.set_db(3.9);
  EXPECT_EQ(hash(s1), hash(s2));
}

TEST(HashTest, SingleDoubleMismatch) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_db(3.9);
  s2.set_db(3.91);
  EXPECT_NE(hash(s1), hash(s2));
}

TEST(HashTest, SingleFloatMatch) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_f(3.9f);
  s2.set_f(3.9f);
  EXPECT_EQ(hash(s1), hash(s2));
}

TEST(HashTest, SingleFloatMismatch) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_f(3.9f);
  s2.set_f(3.91f);
  EXPECT_NE(hash(s1), hash(s2));
}

TEST(HashTest, SingleEnumMatch) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_e(deterministichashtest::FOO);
  s2.set_e(deterministichashtest::FOO);
  EXPECT_EQ(hash(s1), hash(s2));
}

TEST(HashTest, SingleEnumMismatch) {
  deterministichashtest::SingleFields s1, s2;
  s1.set_e(deterministichashtest::FOO);
  s2.set_e(deterministichashtest::BAR);
  EXPECT_NE(hash(s1), hash(s2));
}

TEST(HashTest, AnyWithUnknownTypeMatch) {
  deterministichashtest::AnyContainer a1, a2;
  a1.mutable_any()->set_type_url("invalid_type");
  a2.mutable_any()->set_type_url("invalid_type");
  EXPECT_EQ(hash(a1), hash(a2));
}

TEST(HashTest, AnyWithUnknownTypeMismatch) {
  deterministichashtest::AnyContainer a1, a2;
  a1.mutable_any()->set_type_url("invalid_type");
  a2.mutable_any()->set_type_url("different_invalid_type");
  EXPECT_NE(hash(a1), hash(a2));
}

TEST(HashTest, AnyWithKnownTypeMatch) {
  deterministichashtest::AnyContainer a1, a2;
  deterministichashtest::Recursion value;
  value.set_index(1);
  a1.mutable_any()->PackFrom(value);
  a2.mutable_any()->PackFrom(value);
  EXPECT_EQ(hash(a1), hash(a2));
}

TEST(HashTest, AnyWithKnownTypeMismatch) {
  deterministichashtest::AnyContainer a1, a2;
  deterministichashtest::Recursion value;
  value.set_index(1);
  a1.mutable_any()->PackFrom(value);
  value.set_index(2);
  a2.mutable_any()->PackFrom(value);
  EXPECT_NE(hash(a1), hash(a2));
}

} // namespace DeterministicProtoHash
} // namespace Envoy
