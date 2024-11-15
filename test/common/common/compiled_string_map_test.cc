#include "source/common/common/compiled_string_map.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {

using testing::IsNull;

TEST(CompiledStringMapTest, FindsEntriesCorrectly) {
  CompiledStringMap<const char*> map;
  map.compile({
      {"key-1", "value-1"},
      {"key-2", "value-2"},
      {"longer-key", "value-3"},
      {"bonger-key", "value-4"},
      {"bonger-bey", "value-5"},
      {"only-key-of-this-length", "value-6"},
  });
  EXPECT_EQ(map.find("key-1"), "value-1");
  EXPECT_EQ(map.find("key-2"), "value-2");
  EXPECT_THAT(map.find("key-0"), IsNull());
  EXPECT_THAT(map.find("key-3"), IsNull());
  EXPECT_EQ(map.find("longer-key"), "value-3");
  EXPECT_EQ(map.find("bonger-key"), "value-4");
  EXPECT_EQ(map.find("bonger-bey"), "value-5");
  EXPECT_EQ(map.find("only-key-of-this-length"), "value-6");
  EXPECT_THAT(map.find("songer-key"), IsNull());
  EXPECT_THAT(map.find("absent-length-key"), IsNull());
}

TEST(CompiledStringMapTest, EmptyMapReturnsNull) {
  CompiledStringMap<const char*> map;
  map.compile({});
  EXPECT_THAT(map.find("key-1"), IsNull());
}

} // namespace Envoy
