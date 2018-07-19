#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/framed_transport_impl.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

TEST(FramedTransportTest, Name) {
  StrictMock<MockTransportCallbacks> cb;
  FramedTransportImpl transport(cb);
  EXPECT_EQ(transport.name(), "framed");
}

TEST(FramedTransportTest, NotEnoughData) {
  Buffer::OwnedImpl buffer;
  StrictMock<MockTransportCallbacks> cb;
  FramedTransportImpl transport(cb);

  EXPECT_FALSE(transport.decodeFrameStart(buffer));

  addRepeated(buffer, 3, 0);

  EXPECT_FALSE(transport.decodeFrameStart(buffer));
}

TEST(FramedTransportTest, InvalidFrameSize) {
  StrictMock<MockTransportCallbacks> cb;
  FramedTransportImpl transport(cb);

  {
    Buffer::OwnedImpl buffer;
    addInt32(buffer, -1);

    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer), EnvoyException,
                              "invalid thrift framed transport frame size -1");
  }

  {
    Buffer::OwnedImpl buffer;
    addInt32(buffer, 0x7fffffff);

    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer), EnvoyException,
                              "invalid thrift framed transport frame size 2147483647");
  }
}

TEST(FramedTransportTest, DecodeFrameStart) {
  StrictMock<MockTransportCallbacks> cb;
  EXPECT_CALL(cb, transportFrameStart(absl::optional<uint32_t>(100U)));

  FramedTransportImpl transport(cb);

  Buffer::OwnedImpl buffer;
  addInt32(buffer, 100);

  EXPECT_EQ(buffer.length(), 4);
  EXPECT_TRUE(transport.decodeFrameStart(buffer));
  EXPECT_EQ(buffer.length(), 0);
}

TEST(FramedTransportTest, DecodeFrameEnd) {
  StrictMock<MockTransportCallbacks> cb;
  EXPECT_CALL(cb, transportFrameComplete());

  FramedTransportImpl transport(cb);

  Buffer::OwnedImpl buffer;

  EXPECT_TRUE(transport.decodeFrameEnd(buffer));
}

TEST(FramedTransportTest, EncodeFrame) {
  StrictMock<MockTransportCallbacks> cb;

  FramedTransportImpl transport(cb);

  {
    Buffer::OwnedImpl message;
    message.add("fake message");

    Buffer::OwnedImpl buffer;
    transport.encodeFrame(buffer, message);

    EXPECT_EQ(0, message.length());
    EXPECT_EQ(std::string("\0\0\0\xC"
                          "fake message",
                          16),
              buffer.toString());
  }

  {
    Buffer::OwnedImpl message;
    Buffer::OwnedImpl buffer;
    EXPECT_THROW_WITH_MESSAGE(transport.encodeFrame(buffer, message), EnvoyException,
                              "invalid thrift framed transport frame size 0");
  }
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
