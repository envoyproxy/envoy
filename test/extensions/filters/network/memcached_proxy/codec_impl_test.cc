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

// TEST_F(MemcachedCodecImplTest, GetMoreEqual) {
//   {
//     GetMoreMessageImpl g1(0, 0);
//     GetMoreMessageImpl g2(1, 1);
//     EXPECT_FALSE(g1 == g2);
//   }

//   {
//     GetMoreMessageImpl g1(0, 0);
//     g1.cursorId(1);
//     GetMoreMessageImpl g2(0, 0);
//     g1.cursorId(2);
//     EXPECT_FALSE(g1 == g2);
//   }
// }

TEST_F(MemcachedCodecImplTest, Get) {
  GetRequestImpl get(3, 3, 3);
  get.key("foo");

  encoder_.encodeGet(get);
  EXPECT_CALL(callbacks_, decodeGet_(Pointee(Eq(get))));
  decoder_.onData(output_);
}

TEST_F(MemcachedCodecImplTest, Set) {
  SetRequestImpl set(3, 3, 3);
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
