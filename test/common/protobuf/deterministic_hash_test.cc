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

} // namespace DeterministicProtoHash
} // namespace Envoy
