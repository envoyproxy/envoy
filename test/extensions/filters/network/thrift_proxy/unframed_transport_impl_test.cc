#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/unframed_transport_impl.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

TEST(UnframedTransportTest, Name) {
  NiceMock<MockTransportCallbacks> cb;
  UnframedTransportImpl transport(cb);
  EXPECT_EQ(transport.name(), "unframed");
}

TEST(UnframedTransportTest, DecodeFrameStart) {
  MockTransportCallbacks cb;
  EXPECT_CALL(cb, transportFrameStart(absl::optional<uint32_t>()));

  UnframedTransportImpl transport(cb);

  Buffer::OwnedImpl buffer;
  addInt32(buffer, 0xDEADBEEF);

  EXPECT_EQ(buffer.length(), 4);
  EXPECT_TRUE(transport.decodeFrameStart(buffer));
  EXPECT_EQ(buffer.length(), 4);
}

TEST(UnframedTransportTest, DecodeFrameEnd) {
  MockTransportCallbacks cb;
  EXPECT_CALL(cb, transportFrameComplete());

  UnframedTransportImpl transport(cb);

  Buffer::OwnedImpl buffer;
  EXPECT_TRUE(transport.decodeFrameEnd(buffer));
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
