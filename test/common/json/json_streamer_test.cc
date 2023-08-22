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
  map->newEntries({{"a", "0"}});
  streamer_.clear();
  EXPECT_EQ(R"EOF({"a":0})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, MapTwoNumbers) {
  Streamer::MapPtr map = streamer_.makeRootMap();
  map->newEntries({{"a", "0"}, {"b", "1"}});
  streamer_.clear();
  EXPECT_EQ(R"EOF({"a":0,"b":1})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, MapOneString) {
  Streamer::MapPtr map = streamer_.makeRootMap();
  map->newEntries({{"a", Streamer::quote("b")}});
  streamer_.clear();
  EXPECT_EQ(R"EOF({"a":"b"})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, MapOneSanitized) {
  Streamer::MapPtr map = streamer_.makeRootMap();
  map->newKey("a", [&map]() { map->addSanitized("\b\001"); });
  streamer_.clear();
  EXPECT_EQ(R"EOF({"a":"\b\u0001"})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, MapTwoSanitized) {
  Streamer::MapPtr map = streamer_.makeRootMap();
  map->newKey("a", [&map]() { map->addSanitized("\b\001"); });
  map->newKey("b", [&map]() { map->addSanitized("\r\002"); });
  streamer_.clear();
  EXPECT_EQ(R"EOF({"a":"\b\u0001","b":"\r\u0002"})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, MapOneDeferred) {
  Streamer::MapPtr map = streamer_.makeRootMap();
  Json::Streamer::Map::DeferredValuePtr deferred_value;
  map->newKey("a", [&map, &deferred_value]() { deferred_value = map->deferValue(); });
  map->newArray();
  streamer_.clear();
  EXPECT_EQ(R"EOF({"a":[]})EOF", buffer_.toString());
}

TEST_F(JsonStreamerTest, MapTwoDeferred) {
  Streamer::MapPtr map = streamer_.makeRootMap();
  Json::Streamer::Map::DeferredValuePtr deferred_value;
  map->newKey("a", [&map, &deferred_value]() { deferred_value = map->deferValue(); });
  map->newArray();
  deferred_value.reset();
  map->newKey("b", [&map, &deferred_value]() { deferred_value = map->deferValue(); });
  map->newMap();
  streamer_.clear();
  EXPECT_EQ(R"EOF({"a":[],"b":{}})EOF", buffer_.toString());
}

} // namespace
} // namespace Json
} // namespace Envoy
