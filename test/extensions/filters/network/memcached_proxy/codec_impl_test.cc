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
  void decodeGetk(GetkRequestPtr&& message) override { decodeGetk_(message); }
  void decodeDelete(DeleteRequestPtr&& message) override { decodeDelete_(message); }
  void decodeSet(SetRequestPtr&& message) override { decodeSet_(message); }
  void decodeAdd(AddRequestPtr&& message) override { decodeAdd_(message); }
  void decodeReplace(ReplaceRequestPtr&& message) override { decodeReplace_(message); }
  void decodeIncrement(IncrementRequestPtr&& message) override { decodeIncrement_(message); }
  void decodeDecrement(DecrementRequestPtr&& message) override { decodeDecrement_(message); }
  void decodeAppend(AppendRequestPtr&& message) override { decodeAppend_(message); }
  void decodePrepend(PrependRequestPtr&& message) override { decodePrepend_(message); }

  MOCK_METHOD1(decodeGet_, void(GetRequestPtr& message));
  MOCK_METHOD1(decodeGetk_, void(GetkRequestPtr& message));
  MOCK_METHOD1(decodeDelete_, void(DeleteRequestPtr& message));
  MOCK_METHOD1(decodeSet_, void(SetRequestPtr& message));
  MOCK_METHOD1(decodeAdd_, void(AddRequestPtr& message));
  MOCK_METHOD1(decodeReplace_, void(ReplaceRequestPtr& message));
  MOCK_METHOD1(decodeIncrement_, void(IncrementRequestPtr& message));
  MOCK_METHOD1(decodeDecrement_, void(DecrementRequestPtr& message));
  MOCK_METHOD1(decodeAppend_, void(AppendRequestPtr& message));
  MOCK_METHOD1(decodePrepend_, void(PrependRequestPtr& message));
};

class MemcachedCodecImplTest : public testing::Test {
public:
  Buffer::OwnedImpl output_;
  EncoderImpl encoder_;
  NiceMock<TestDecoderCallbacks> callbacks_;
  DecoderImpl decoder_{callbacks_};
};

TEST_F(MemcachedCodecImplTest, GetLikeEquality) {
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

TEST_F(MemcachedCodecImplTest, SetLikeEquality) {
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

TEST_F(MemcachedCodecImplTest, CounterLikeEquality) {
  {
    IncrementRequestImpl s1(1, 1, 1, 1);
    IncrementRequestImpl s2(2, 2, 2, 2);
    EXPECT_FALSE(s1 == s2);
  }

  {
    IncrementRequestImpl s1(1, 1, 1, 1);
    s1.key("foo");
    IncrementRequestImpl s2(1, 1, 1, 1);
    s2.key("bar");
    EXPECT_FALSE(s1 == s2);
  }

  {
    IncrementRequestImpl s1(1, 1, 1, 1);
    s1.amount(1);
    IncrementRequestImpl s2(1, 1, 1, 1);
    s2.amount(2);
    EXPECT_FALSE(s1 == s2);
  }

  {
    IncrementRequestImpl s1(1, 1, 1, 1);
    s1.expiration(1);
    IncrementRequestImpl s2(1, 1, 1, 1);
    s2.expiration(2);
    EXPECT_FALSE(s1 == s2);
  }

  {
    IncrementRequestImpl s1(1, 1, 1, 1);
    s1.initialValue(1);
    IncrementRequestImpl s2(1, 1, 1, 1);
    s2.initialValue(2);
    EXPECT_FALSE(s1 == s2);
  }

  {
    IncrementRequestImpl s1(1, 1, 1, 1);
    s1.key("foo");
    s1.amount(1337);
    s1.expiration(1336);
    s1.initialValue(1337);
    IncrementRequestImpl s2(1, 1, 1, 1);
    s2.key("foo");
    s2.amount(1337);
    s2.expiration(1336);
    s2.initialValue(1337);
    EXPECT_TRUE(s1 == s2);
  }
}

TEST_F(MemcachedCodecImplTest, AppendLikeEquality) {
  {
    AppendRequestImpl s1(1, 1, 1, 1);
    AppendRequestImpl s2(2, 2, 2, 2);
    EXPECT_FALSE(s1 == s2);
  }

  {
    AppendRequestImpl s1(1, 1, 1, 1);
    s1.key("foo");
    AppendRequestImpl s2(1, 1, 1, 1);
    s2.key("bar");
    EXPECT_FALSE(s1 == s2);
  }

  {
    AppendRequestImpl s1(1, 1, 1, 1);
    s1.body("foo");
    AppendRequestImpl s2(1, 1, 1, 1);
    s2.body("bar");
    EXPECT_FALSE(s1 == s2);
  }

  {
    AppendRequestImpl s1(1, 1, 1, 1);
    s1.key("foo");
    s1.body("bar");
    AppendRequestImpl s2(1, 1, 1, 1);
    s2.key("foo");
    s2.body("bar");
    EXPECT_TRUE(s1 == s2);
  }
}

TEST_F(MemcachedCodecImplTest, GetRoundTrip) {
  GetRequestImpl get(3, 3, 3, 3);
  get.key("foo");

  encoder_.encodeGet(get, output_);
  EXPECT_CALL(callbacks_, decodeGet_(Pointee(Eq(get))));
  decoder_.onData(output_);
}

TEST_F(MemcachedCodecImplTest, GetkRoundTrip) {
  GetkRequestImpl getk(3, 3, 3, 3);
  getk.key("foo");

  encoder_.encodeGetk(getk, output_);
  EXPECT_CALL(callbacks_, decodeGetk_(Pointee(Eq(getk))));
  decoder_.onData(output_);
}

TEST_F(MemcachedCodecImplTest, DeleteRoundTrip) {
  DeleteRequestImpl del(3, 3, 3, 3);
  del.key("foo");

  encoder_.encodeDelete(del, output_);
  EXPECT_CALL(callbacks_, decodeDelete_(Pointee(Eq(del))));
  decoder_.onData(output_);
}

TEST_F(MemcachedCodecImplTest, SetRoundTrip) {
  SetRequestImpl set(3, 3, 3, 3);
  set.key("foo");
  set.body("bar");

  encoder_.encodeSet(set, output_);
  EXPECT_CALL(callbacks_, decodeSet_(Pointee(Eq(set))));
  decoder_.onData(output_);
}

TEST_F(MemcachedCodecImplTest, AddRoundTrip) {
  AddRequestImpl add(3, 3, 3, 3);
  add.key("foo");
  add.body("bar");

  encoder_.encodeAdd(add, output_);
  EXPECT_CALL(callbacks_, decodeAdd_(Pointee(Eq(add))));
  decoder_.onData(output_);
}

TEST_F(MemcachedCodecImplTest, ReplaceRoundTrip) {
  ReplaceRequestImpl replace(3, 3, 3, 3);
  replace.key("foo");
  replace.body("bar");

  encoder_.encodeReplace(replace, output_);
  EXPECT_CALL(callbacks_, decodeReplace_(Pointee(Eq(replace))));
  decoder_.onData(output_);
}

TEST_F(MemcachedCodecImplTest, IncrementRoundTrip) {
  IncrementRequestImpl incr(3, 3, 3, 3);
  incr.key("foo");
  incr.amount(1);
  incr.initialValue(0);
  incr.initialValue(123);

  encoder_.encodeIncrement(incr, output_);
  EXPECT_CALL(callbacks_, decodeIncrement_(Pointee(Eq(incr))));
  decoder_.onData(output_);
}

TEST_F(MemcachedCodecImplTest, DecrementRoundTrip) {
  DecrementRequestImpl decr(3, 3, 3, 3);
  decr.key("foo");
  decr.amount(1);
  decr.initialValue(0);
  decr.initialValue(123);

  encoder_.encodeDecrement(decr, output_);
  EXPECT_CALL(callbacks_, decodeDecrement_(Pointee(Eq(decr))));
  decoder_.onData(output_);
}

TEST_F(MemcachedCodecImplTest, AppendRoundTrip) {
  AppendRequestImpl append(3, 3, 3, 3);
  append.key("foo");
  append.body("bar");

  encoder_.encodeAppend(append, output_);
  EXPECT_CALL(callbacks_, decodeAppend_(Pointee(Eq(append))));
  decoder_.onData(output_);
}

TEST_F(MemcachedCodecImplTest, PrependRoundTrip) {
  PrependRequestImpl prepend(3, 3, 3, 3);
  prepend.key("foo");
  prepend.body("bar");

  encoder_.encodePrepend(prepend, output_);
  EXPECT_CALL(callbacks_, decodePrepend_(Pointee(Eq(prepend))));
  decoder_.onData(output_);
}

} // namespace MemcachedProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
