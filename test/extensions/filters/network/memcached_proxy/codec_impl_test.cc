#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/json/json_loader.h"

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
  void decodeGet(GetMessagePtr&& message) override { decodeGet_(message); }
  void decodeSet(SetMessagePtr&& message) override { decodeSet_(message); }

  MOCK_METHOD1(decodeGet_, void(GetMessagePtr& message));
  MOCK_METHOD1(decodeSet_, void(SetMessagePtr& message));
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

TEST_F(MemcachedCodecImplTest, GetMore) {
  GetMoreMessageImpl get_more(3, 3);
  get_more.fullCollectionName("test");
  get_more.numberToReturn(20);
  get_more.cursorId(20000);

  encoder_.encodeGetMore(get_more);
  EXPECT_CALL(callbacks_, decodeGetMore_(Pointee(Eq(get_more))));
  decoder_.onData(output_);
}

} // namespace MemcachedProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
