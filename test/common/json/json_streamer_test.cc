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

// https://storage.googleapis.com/envoy-pr/1a4c83a/coverage/source/common/json/json_streamer.cc.gcov.html

} // namespace
} // namespace Json
} // namespace Envoy
