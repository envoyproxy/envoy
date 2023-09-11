#include <ostream>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/json/json_internal.h"
#include "source/common/json/json_streamer.h"

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace {

class JsonStreamerTest : public testing::Test {
protected:
  Buffer::OwnedImpl buffer_;
  Json::Streamer streamer_{buffer_};
};

TEST_F(JsonStreamerTest, Empty) { EXPECT_EQ("", buffer_.toString()); }

TEST_F(JsonStreamerTest, EmptyMap) {
  streamer_.makeRootMap();
  EXPECT_EQ("{}", buffer_.toString());
}

TEST_F(JsonStreamerTest, MapOneDouble) {
  {
    Streamer::MapPtr map = streamer_.makeRootMap();
    map->addEntries({{"a", 3.141592654}});
  }
  EXPECT_EQ(R"EOF({"a":3.141592654})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, MapTwoDoubles) {
  {
    Streamer::MapPtr map = streamer_.makeRootMap();
    map->addEntries({{"a", -989282.1087}, {"b", 1.23456789012345e+67}});
  }
  EXPECT_EQ(R"EOF({"a":-989282.1087,"b":1.23456789012345e+67})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, MapOneUInt) {
  {
    Streamer::MapPtr map = streamer_.makeRootMap();
    map->addEntries({{"a", static_cast<uint64_t>(0xffffffffffffffff)}});
  }
  EXPECT_EQ(R"EOF({"a":18446744073709551615})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, MapTwoInts) {
  {
    Streamer::MapPtr map = streamer_.makeRootMap();
    map->addEntries({{"a", static_cast<int64_t>(0x7fffffffffffffff)},
                     {"b", static_cast<int64_t>(0x8000000000000000)}});
  }
  EXPECT_EQ(R"EOF({"a":9223372036854775807,"b":-9223372036854775808})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, MapOneString) {
  {
    Streamer::MapPtr map = streamer_.makeRootMap();
    map->addEntries({{"a", "b"}});
  }
  EXPECT_EQ(R"EOF({"a":"b"})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, MapOneSanitized) {
  {
    Streamer::MapPtr map = streamer_.makeRootMap();
    map->addKey("a");
    map->addString("\b\001");
  }
  EXPECT_EQ(R"EOF({"a":"\b\u0001"})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, MapTwoSanitized) {
  {
    Streamer::MapPtr map = streamer_.makeRootMap();
    map->addKey("a");
    map->addString("\b\001");
    map->addKey("b");
    map->addString("\r\002");
  }
  EXPECT_EQ(R"EOF({"a":"\b\u0001","b":"\r\u0002"})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, SubArray) {
  Streamer::MapPtr map = streamer_.makeRootMap();
  map->addKey("a");
  Streamer::ArrayPtr array = map->addArray();
  array->addEntries({1.0, "two", 3.5, std::nan("")});
  array.reset();
  map->addEntries({{"embedded\"quote", "value"}});
  map.reset();
  EXPECT_EQ(R"EOF({"a":[1,"two",3.5,null],"embedded\"quote":"value"})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, TopArray) {
  {
    Streamer::ArrayPtr array = streamer_.makeRootArray();
    array->addEntries({1.0, "two", 3.5, std::nan("")});
  }
  EXPECT_EQ(R"EOF([1,"two",3.5,null])EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, SubMap) {
  Streamer::MapPtr map = streamer_.makeRootMap();
  map->addKey("a");
  Streamer::MapPtr sub_map = map->addMap();
  sub_map->addEntries({{"one", 1.0}, {"three.5", 3.5}});
  sub_map.reset();
  map.reset();
  EXPECT_EQ(R"EOF({"a":{"one":1,"three.5":3.5}})EOF", buffer_.toString());
}

} // namespace
} // namespace Json
} // namespace Envoy
