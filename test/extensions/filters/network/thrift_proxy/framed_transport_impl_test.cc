#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/framed_transport_impl.h"

#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

TEST(FramedTransportTest, Name) {
  FramedTransportImpl transport;
  EXPECT_EQ(transport.name(), "framed");
}

TEST(FramedTransportTest, Type) {
  FramedTransportImpl transport;
  EXPECT_EQ(transport.type(), TransportType::Framed);
}

TEST(FramedTransportTest, NotEnoughData) {
  Buffer::OwnedImpl buffer;
  FramedTransportImpl transport;
  absl::optional<uint32_t> size = 1;

  EXPECT_FALSE(transport.decodeFrameStart(buffer, size));
  EXPECT_EQ(absl::optional<uint32_t>(1), size);

  addRepeated(buffer, 3, 0);

  EXPECT_FALSE(transport.decodeFrameStart(buffer, size));
  EXPECT_EQ(absl::optional<uint32_t>(1), size);
}

TEST(FramedTransportTest, InvalidFrameSize) {
  FramedTransportImpl transport;

  {
    Buffer::OwnedImpl buffer;
    addInt32(buffer, -1);

    absl::optional<uint32_t> size = 1;
    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, size), EnvoyException,
                              "invalid thrift framed transport frame size -1");
    EXPECT_EQ(absl::optional<uint32_t>(1), size);
  }

  {
    Buffer::OwnedImpl buffer;
    addInt32(buffer, 0x7fffffff);

    absl::optional<uint32_t> size = 1;
    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, size), EnvoyException,
                              "invalid thrift framed transport frame size 2147483647");
    EXPECT_EQ(absl::optional<uint32_t>(1), size);
  }
}

TEST(FramedTransportTest, DecodeFrameStart) {
  FramedTransportImpl transport;

  Buffer::OwnedImpl buffer;
  addInt32(buffer, 100);
  EXPECT_EQ(buffer.length(), 4);

  absl::optional<uint32_t> size;
  EXPECT_TRUE(transport.decodeFrameStart(buffer, size));
  EXPECT_EQ(absl::optional<uint32_t>(100U), size);
  EXPECT_EQ(buffer.length(), 0);
}

TEST(FramedTransportTest, DecodeFrameEnd) {
  FramedTransportImpl transport;

  Buffer::OwnedImpl buffer;

  EXPECT_TRUE(transport.decodeFrameEnd(buffer));
}

TEST(FramedTransportTest, EncodeFrame) {
  FramedTransportImpl transport;

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
