#include <ostream>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/json/json_internal.h"
#include "source/common/json/json_streamer.h"

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace {

class BufferOutputWrapper {
public:
  using Type = BufferOutput;
  std::string toString() { return underlying_buffer_.toString(); }
  void clear() { underlying_buffer_.drain(underlying_buffer_.length()); }
  Buffer::OwnedImpl underlying_buffer_;
};

class StringOutputWrapper {
public:
  using Type = StringOutput;
  std::string toString() { return underlying_buffer_; }
  void clear() { underlying_buffer_.clear(); }
  std::string underlying_buffer_;
};

template <typename T> class JsonStreamerTest : public testing::Test {
public:
  T buffer_;
  Json::StreamerBase<typename T::Type> streamer_{this->buffer_.underlying_buffer_};
};

using OutputBufferTypes = ::testing::Types<BufferOutputWrapper, StringOutputWrapper>;
TYPED_TEST_SUITE(JsonStreamerTest, OutputBufferTypes);

TYPED_TEST(JsonStreamerTest, Empty) { EXPECT_EQ("", this->buffer_.toString()); }

TYPED_TEST(JsonStreamerTest, EmptyMap) {
  this->streamer_.makeRootMap();
  EXPECT_EQ("{}", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, MapOneDouble) {
  {
    auto map = this->streamer_.makeRootMap();
    map->addEntries({{"a", 3.141592654}});
  }
  EXPECT_EQ(R"EOF({"a":3.141592654})EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, MapTwoDoubles) {
  {
    auto map = this->streamer_.makeRootMap();
    map->addEntries({{"a", -989282.1087}, {"b", 1.23456789012345e+67}});
  }
  EXPECT_EQ(R"EOF({"a":-989282.1087,"b":1.23456789012345e+67})EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, MapOneUInt) {
  {
    auto map = this->streamer_.makeRootMap();
    map->addEntries({{"a", static_cast<uint64_t>(0xffffffffffffffff)}});
  }
  EXPECT_EQ(R"EOF({"a":18446744073709551615})EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, MapTwoInts) {
  {
    auto map = this->streamer_.makeRootMap();
    map->addEntries({{"a", static_cast<int64_t>(0x7fffffffffffffff)},
                     {"b", static_cast<int64_t>(0x8000000000000000)}});
  }
  EXPECT_EQ(R"EOF({"a":9223372036854775807,"b":-9223372036854775808})EOF",
            this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, MapOneString) {
  {
    auto map = this->streamer_.makeRootMap();
    map->addEntries({{"a", "b"}});
  }
  EXPECT_EQ(R"EOF({"a":"b"})EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, MapOneBool) {
  {
    auto map = this->streamer_.makeRootMap();
    map->addEntries({{"a", true}});
  }
  EXPECT_EQ(R"EOF({"a":true})EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, MapTwoBools) {
  {
    auto map = this->streamer_.makeRootMap();
    map->addEntries({{"a", true}, {"b", false}});
  }
  EXPECT_EQ(R"EOF({"a":true,"b":false})EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, MapOneSanitized) {
  {
    auto map = this->streamer_.makeRootMap();
    map->addKey("a");
    map->addString("\b\001");
  }
  EXPECT_EQ(R"EOF({"a":"\b\u0001"})EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, MapTwoSanitized) {
  {
    auto map = this->streamer_.makeRootMap();
    map->addKey("a");
    map->addString("\b\001");
    map->addKey("b");
    map->addString("\r\002");
  }
  EXPECT_EQ(R"EOF({"a":"\b\u0001","b":"\r\u0002"})EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, SubArray) {
  auto map = this->streamer_.makeRootMap();
  map->addKey("a");
  auto array = map->addArray();
  array->addEntries({1.0, "two", 3.5, true, false, std::nan("")});
  array.reset();
  map->addEntries({{"embedded\"quote", "value"}});
  map.reset();
  EXPECT_EQ(R"EOF({"a":[1,"two",3.5,true,false,null],"embedded\"quote":"value"})EOF",
            this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, TopArray) {
  {
    auto array = this->streamer_.makeRootArray();
    array->addEntries({1.0, "two", 3.5, true, false, std::nan(""), absl::monostate{}});
  }
  EXPECT_EQ(R"EOF([1,"two",3.5,true,false,null,null])EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, SubMap) {
  auto map = this->streamer_.makeRootMap();
  map->addKey("a");
  auto sub_map = map->addMap();
  sub_map->addEntries({{"one", 1.0}, {"three.5", 3.5}});
  sub_map.reset();
  map.reset();
  EXPECT_EQ(R"EOF({"a":{"one":1,"three.5":3.5}})EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, SimpleDirectCall) {
  {
    this->streamer_.addBool(true);
    EXPECT_EQ("true", this->buffer_.toString());
    this->buffer_.clear();
  }

  {
    this->streamer_.addBool(false);
    EXPECT_EQ("false", this->buffer_.toString());
    this->buffer_.clear();
  }

  {
    this->streamer_.addString("hello");
    EXPECT_EQ(R"EOF("hello")EOF", this->buffer_.toString());
    this->buffer_.clear();
  }

  {
    uint64_t value = 1;
    this->streamer_.addNumber(value);
    EXPECT_EQ("1", this->buffer_.toString());
    this->buffer_.clear();
  }

  {
    this->streamer_.addNumber(1.5);
    EXPECT_EQ("1.5", this->buffer_.toString());
    this->buffer_.clear();
  }

  {
    this->streamer_.addNull();
    EXPECT_EQ("null", this->buffer_.toString());
    this->buffer_.clear();
  }
}

} // namespace
} // namespace Json
} // namespace Envoy
