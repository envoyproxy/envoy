#include <string>

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/memcached_proxy/codec_impl.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Eq;
using testing::NiceMock;
using testing::Pointee;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MemcachedProxy {

class TestDecoderCallbacks : public DecoderCallbacks {
public:
  void decodeGet(GetRequestPtr&& message) override { decodeGet_(message); }
  void decodeSet(SetRequestPtr&& message) override { decodeSet_(message); }

  MOCK_METHOD1(decodeGet_, void(GetRequestPtr& message));
  MOCK_METHOD1(decodeSet_, void(SetRequestPtr& message));
};

class MemcachedCodecImplTest : public testing::Test {
public:
  Buffer::OwnedImpl output_;
  EncoderImpl encoder_{output_};
  NiceMock<TestDecoderCallbacks> callbacks_;
  DecoderImpl decoder_{callbacks_};
};

TEST_F(MemcachedCodecImplTest, GetEquality) {
  {
    GetRequestImpl g1(1, 1, 1, 1);
    GetRequestImpl g2(2, 2, 2, 2);
    EXPECT_FALSE(g1 == g2);
  }

  {
    GetRequestImpl g1(1, 1, 1, 1);
    g1.key("foo");
    GetRequestImpl g2(1, 1, 1, 1);
    g2.key("bar");
    EXPECT_FALSE(g1 == g2);
  }

  {
    GetRequestImpl g1(1, 1, 1, 1);
    g1.key("foo");
    GetRequestImpl g2(1, 1, 1, 1);
    g2.key("foo");
    EXPECT_TRUE(g1 == g2);
  }
}

TEST_F(MemcachedCodecImplTest, SetEquality) {
  {
    SetRequestImpl s1(1, 1, 1, 1);
    SetRequestImpl s2(2, 2, 2, 2);
    EXPECT_FALSE(s1 == s2);
  }

  {
    SetRequestImpl s1(1, 1, 1, 1);
    s1.key("foo");
    SetRequestImpl s2(1, 1, 1, 1);
    s2.key("bar");
    EXPECT_FALSE(s1 == s2);
  }

  {
    SetRequestImpl s1(1, 1, 1, 1);
    s1.body("foo");
    SetRequestImpl s2(1, 1, 1, 1);
    s2.body("bar");
    EXPECT_FALSE(s1 == s2);
  }

  {
    SetRequestImpl s1(1, 1, 1, 1);
    s1.expiration(1);
    SetRequestImpl s2(1, 1, 1, 1);
    s2.expiration(2);
    EXPECT_FALSE(s1 == s2);
  }

  {
    SetRequestImpl s1(1, 1, 1, 1);
    s1.flags(1);
    SetRequestImpl s2(1, 1, 1, 1);
    s2.flags(2);
    EXPECT_FALSE(s1 == s2);
  }

  {
    SetRequestImpl s1(1, 1, 1, 1);
    s1.body("foo");
    s1.expiration(1336);
    s1.flags(1337);
    SetRequestImpl s2(1, 1, 1, 1);
    s2.body("foo");
    s2.expiration(1336);
    s2.flags(1337);
    EXPECT_TRUE(s1 == s2);
  }
}

TEST_F(MemcachedCodecImplTest, GetRoundTrip) {
  GetRequestImpl get(3, 3, 3, 3);
  get.key("foo");

  encoder_.encodeGet(get);
  EXPECT_CALL(callbacks_, decodeGet_(Pointee(Eq(get))));
  decoder_.onData(output_);
}

TEST_F(MemcachedCodecImplTest, SetRoundTrip) {
  SetRequestImpl set(3, 3, 3, 3);
  set.key("foo");
  set.body("bar");

  encoder_.encodeSet(set);
  EXPECT_CALL(callbacks_, decodeSet_(Pointee(Eq(set))));
  decoder_.onData(output_);
}

} // namespace MemcachedProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
