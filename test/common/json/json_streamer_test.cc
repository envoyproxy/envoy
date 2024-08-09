#include <ostream>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/json/json_internal.h"
#include "source/common/json/json_streamer.h"

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace {

class TestBuffer : public BufferTemplate {
public:
  Buffer::OwnedImpl buffer_;
  TestBuffer() : BufferTemplate(buffer_) {}
  std::string toString() { return buffer_.toString(); }
};

class TestString : public StringTemplate {
public:
  std::string buffer_;
  TestString() : StringTemplate(buffer_) {}
  std::string toString() { return buffer_; }
};

using JsonStreamerTestTypes = testing::Types<TestBuffer, TestString>;

template <class T> class JsonStreamerTest : public testing::Test {
public:
  using TestStreamer = Streamer<T>;
  T buffer_;
  TestStreamer streamer_{buffer_};
};

TYPED_TEST_CASE(JsonStreamerTest, JsonStreamerTestTypes);

TYPED_TEST(JsonStreamerTest, Empty) { EXPECT_EQ("", this->buffer_.toString()); }

TYPED_TEST(JsonStreamerTest, EmptyMap) {
  this->streamer_.makeRootMap();
  EXPECT_EQ("{}", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, MapOneDouble) {
  {
    typename TestFixture::TestStreamer::MapPtr map = this->streamer_.makeRootMap();
    map->addEntries({{"a", 3.141592654}});
  }
  EXPECT_EQ(R"EOF({"a":3.141592654})EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, MapTwoDoubles) {
  {
    typename TestFixture::TestStreamer::MapPtr map = this->streamer_.makeRootMap();
    map->addEntries({{"a", -989282.1087}, {"b", 1.23456789012345e+67}});
  }
  EXPECT_EQ(R"EOF({"a":-989282.1087,"b":1.23456789012345e+67})EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, MapOneUInt) {
  {
    typename TestFixture::TestStreamer::MapPtr map = this->streamer_.makeRootMap();
    map->addEntries({{"a", static_cast<uint64_t>(0xffffffffffffffff)}});
  }
  EXPECT_EQ(R"EOF({"a":18446744073709551615})EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, MapTwoInts) {
  {
    typename TestFixture::TestStreamer::MapPtr map = this->streamer_.makeRootMap();
    map->addEntries({{"a", static_cast<int64_t>(0x7fffffffffffffff)},
                     {"b", static_cast<int64_t>(0x8000000000000000)}});
  }
  EXPECT_EQ(R"EOF({"a":9223372036854775807,"b":-9223372036854775808})EOF",
            this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, MapOneString) {
  {
    typename TestFixture::TestStreamer::MapPtr map = this->streamer_.makeRootMap();
    map->addEntries({{"a", "b"}});
  }
  EXPECT_EQ(R"EOF({"a":"b"})EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, MapOneBool) {
  {
    typename TestFixture::TestStreamer::MapPtr map = this->streamer_.makeRootMap();
    map->addEntries({{"a", true}});
  }
  EXPECT_EQ(R"EOF({"a":true})EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, MapTwoBools) {
  {
    typename TestFixture::TestStreamer::MapPtr map = this->streamer_.makeRootMap();
    map->addEntries({{"a", true}, {"b", false}});
  }
  EXPECT_EQ(R"EOF({"a":true,"b":false})EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, MapOneSanitized) {
  {
    typename TestFixture::TestStreamer::MapPtr map = this->streamer_.makeRootMap();
    map->addKey("a");
    map->addString("\b\001");
  }
  EXPECT_EQ(R"EOF({"a":"\b\u0001"})EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, MapTwoSanitized) {
  {
    typename TestFixture::TestStreamer::MapPtr map = this->streamer_.makeRootMap();
    map->addKey("a");
    map->addString("\b\001");
    map->addKey("b");
    map->addString("\r\002");
  }
  EXPECT_EQ(R"EOF({"a":"\b\u0001","b":"\r\u0002"})EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, SubArray) {
  typename TestFixture::TestStreamer::MapPtr map = this->streamer_.makeRootMap();
  map->addKey("a");
  typename TestFixture::TestStreamer::ArrayPtr array = map->addArray();
  array->addEntries({1.0, "two", 3.5, true, false, std::nan("")});
  array.reset();
  map->addEntries({{"embedded\"quote", "value"}});
  map.reset();
  EXPECT_EQ(R"EOF({"a":[1,"two",3.5,true,false,null],"embedded\"quote":"value"})EOF",
            this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, TopArray) {
  {
    typename TestFixture::TestStreamer::ArrayPtr array = this->streamer_.makeRootArray();
    array->addEntries({1.0, "two", 3.5, true, false, std::nan("")});
  }
  EXPECT_EQ(R"EOF([1,"two",3.5,true,false,null])EOF", this->buffer_.toString());
}

TYPED_TEST(JsonStreamerTest, SubMap) {
  typename TestFixture::TestStreamer::MapPtr map = this->streamer_.makeRootMap();
  map->addKey("a");
  typename TestFixture::TestStreamer::MapPtr sub_map = map->addMap();
  sub_map->addEntries({{"one", 1.0}, {"three.5", 3.5}});
  sub_map.reset();
  map.reset();
  EXPECT_EQ(R"EOF({"a":{"one":1,"three.5":3.5}})EOF", this->buffer_.toString());
}

} // namespace
} // namespace Json
} // namespace Envoy
