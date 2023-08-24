#include <ostream>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/json/json_internal.h"
#include "source/common/json/json_streamer.h"

//#include "source/common/protobuf/utility.h"

//#include "test/common/json/json_streamer_test_util.h"

#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace {

class JsonStreamerTest : public testing::Test {
protected:
  Buffer::OwnedImpl buffer_;
  Json::Streamer streamer_{buffer_};
};

TEST_F(JsonStreamerTest, Empty) {
  streamer_.clear();
  EXPECT_EQ("", buffer_.toString());
}

TEST_F(JsonStreamerTest, EmptyMap) {
  streamer_.makeRootMap();
  streamer_.clear();
  EXPECT_EQ("{}", buffer_.toString());
}

TEST_F(JsonStreamerTest, MapOneNumber) {
  Streamer::MapPtr map = streamer_.makeRootMap();
  map->addEntries({{"a", 0.0}});
  streamer_.clear();
  EXPECT_EQ(R"EOF({"a":0})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, MapTwoNumbers) {
  Streamer::MapPtr map = streamer_.makeRootMap();
  map->addEntries({{"a", 0.0}, {"b", 1.0}});
  streamer_.clear();
  EXPECT_EQ(R"EOF({"a":0,"b":1})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, MapOneString) {
  Streamer::MapPtr map = streamer_.makeRootMap();
  map->addEntries({{"a", "b"}});
  streamer_.clear();
  EXPECT_EQ(R"EOF({"a":"b"})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, MapOneSanitized) {
  Streamer::MapPtr map = streamer_.makeRootMap();
  map->newKey("a");
  map->addString("\b\001");
  streamer_.clear();
  EXPECT_EQ(R"EOF({"a":"\b\u0001"})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, MapTwoSanitized) {
  Streamer::MapPtr map = streamer_.makeRootMap();
  map->newKey("a");
  map->addString("\b\001");
  map->newKey("b");
  map->addString("\r\002");
  streamer_.clear();
  EXPECT_EQ(R"EOF({"a":"\b\u0001","b":"\r\u0002"})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, SubArray) {
  Streamer::MapPtr map = streamer_.makeRootMap();
  map->newKey("a");
  Streamer::ArrayPtr array = map->addArray();
  array->addEntries({1.0, "two", 3.5, std::nan("")});
  array.reset();
  map->addEntries({{"embedded\"quote", "value"}});
  map.reset();
  streamer_.clear();
  EXPECT_EQ(R"EOF({"a":[1,"two",3.5,null],"embedded\"quote":"value"})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, SubMap) {
  Streamer::MapPtr map = streamer_.makeRootMap();
  map->newKey("a");
  Streamer::MapPtr sub_map = map->addMap();
  sub_map->addEntries({{"one", 1.0}, {"three.5", 3.5}});
  sub_map.reset();
  map.reset();
  streamer_.clear();
  EXPECT_EQ(R"EOF({"a":{"one":1,"three.5":3.5}})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, ExhaustBuffers) {
  Streamer::MapPtr map = streamer_.makeRootMap();
  map->addEntries({{"\001", "\002"},
                   {"\003", "\004"},
                   {"\005", "\006"},
                   {"\007", "\010"},
                   {"\011", "\012"},
                   {"\013", "\014"}});
  streamer_.clear();
  EXPECT_EQ(
      R"EOF({"\u0001":"\u0002","\u0003":"\u0004","\u0005":"\u0006","\u0007":"\b","\t":"\n","\u000b":"\f"})EOF",
      buffer_.toString());
}

// https://storage.googleapis.com/envoy-pr/1659efc/coverage/source/common/json/index.html

} // namespace
} // namespace Json
} // namespace Envoy
