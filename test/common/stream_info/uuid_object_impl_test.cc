#include <set>
#include <string>
#include <vector>

#include "source/common/stream_info/uuid_object_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace StreamInfo {
namespace {

TEST(UuidObjectTest, Serialization) {
  const Random::RandomUtility::UuidBytes bytes = {0xa1, 0x21, 0xe9, 0xe1, 0xfe, 0xae, 0x41, 0x36,
                                                  0x9e, 0x0e, 0x6f, 0xac, 0x34, 0x3d, 0x56, 0xc9};
  UuidObject object(bytes);
  EXPECT_EQ(bytes, object.bytes());
  EXPECT_EQ("a121e9e1-feae-4136-9e0e-6fac343d56c9", object.serializeAsString());

  auto message = object.serializeAsProto();
  ASSERT_NE(nullptr, message);
  const auto* string_value = dynamic_cast<const Protobuf::StringValue*>(message.get());
  ASSERT_NE(nullptr, string_value);
  EXPECT_EQ("a121e9e1-feae-4136-9e0e-6fac343d56c9", string_value->value());
}

TEST(UuidObjectTest, HashKeyContainsFullUuid) {
  const Random::RandomUtility::UuidBytes bytes = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  UuidObject object(bytes);
  std::vector<uint8_t> key{0xff};
  object.hashKey(key);
  const std::vector<uint8_t> expected{0xff, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  EXPECT_EQ(expected, key);
}

TEST(UuidObjectTest, Hash) {
  const Random::RandomUtility::UuidBytes bytes1 = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  const Random::RandomUtility::UuidBytes bytes2 = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16};
  EXPECT_TRUE(UuidObject(bytes1).hash().has_value());
  EXPECT_EQ(UuidObject(bytes1).hash(), UuidObject(bytes1).hash());
  EXPECT_NE(UuidObject(bytes1).hash(), UuidObject(bytes2).hash());
}

TEST(UuidObjectTest, RandomIsUnique) {
  std::set<Random::RandomUtility::UuidBytes> results;
  const size_t num_of_results = 10000;
  for (size_t i = 0; i < num_of_results; ++i) {
    auto object = UuidObject::random();
    // Version 4, variant 1.
    EXPECT_EQ(0x40, object->bytes()[6] & 0xf0);
    EXPECT_EQ(0x80, object->bytes()[8] & 0xc0);
    results.insert(object->bytes());
  }
  EXPECT_EQ(num_of_results, results.size());
}

} // namespace
} // namespace StreamInfo
} // namespace Envoy
