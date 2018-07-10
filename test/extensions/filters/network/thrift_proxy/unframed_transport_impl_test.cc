#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/unframed_transport_impl.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

TEST(UnframedTransportTest, Name) {
  StrictMock<MockTransportCallbacks> cb;
  UnframedTransportImpl transport(cb);
  EXPECT_EQ(transport.name(), "unframed");
}

TEST(UnframedTransportTest, DecodeFrameStart) {
  StrictMock<MockTransportCallbacks> cb;
  EXPECT_CALL(cb, transportFrameStart(absl::optional<uint32_t>()));

  UnframedTransportImpl transport(cb);

  Buffer::OwnedImpl buffer;
  addInt32(buffer, 0xDEADBEEF);

  EXPECT_EQ(buffer.length(), 4);
  EXPECT_TRUE(transport.decodeFrameStart(buffer));
  EXPECT_EQ(buffer.length(), 4);
}

TEST(UnframedTransportTest, DecodeFrameEnd) {
  StrictMock<MockTransportCallbacks> cb;
  EXPECT_CALL(cb, transportFrameComplete());

  UnframedTransportImpl transport(cb);

  Buffer::OwnedImpl buffer;
  EXPECT_TRUE(transport.decodeFrameEnd(buffer));
}

TEST(UnframedTransportTest, EncodeFrame) {
  StrictMock<MockTransportCallbacks> cb;

  UnframedTransportImpl transport(cb);

  Buffer::OwnedImpl message;
  message.add("fake message");

  Buffer::OwnedImpl buffer;
  transport.encodeFrame(buffer, message);

  EXPECT_EQ(0, message.length());
  EXPECT_EQ("fake message", buffer.toString());
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
