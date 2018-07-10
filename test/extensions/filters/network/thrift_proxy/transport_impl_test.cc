#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/transport_impl.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Ref;
using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

TEST(AutoTransportTest, NotEnoughData) {
  Buffer::OwnedImpl buffer;
  StrictMock<MockTransportCallbacks> cb;
  AutoTransportImpl transport(cb);

  EXPECT_FALSE(transport.decodeFrameStart(buffer));

  addRepeated(buffer, 7, 0);

  EXPECT_FALSE(transport.decodeFrameStart(buffer));
}

TEST(AutoTransportTest, UnknownTransport) {
  StrictMock<MockTransportCallbacks> cb;
  AutoTransportImpl transport(cb);

  // Looks like unframed, but fails protocol check.
  {
    Buffer::OwnedImpl buffer;
    addInt32(buffer, 0);
    addInt32(buffer, 0);

    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer), EnvoyException,
                              "unknown thrift auto transport frame start 00 00 00 00 00 00 00 00");
  }

  // Looks like framed, but fails protocol check.
  {
    Buffer::OwnedImpl buffer;
    addInt32(buffer, 0xFF);
    addInt32(buffer, 0);

    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer), EnvoyException,
                              "unknown thrift auto transport frame start 00 00 00 ff 00 00 00 00");
  }
}

TEST(AutoTransportTest, DecodeFrameStart) {
  StrictMock<MockTransportCallbacks> cb;

  // Framed transport + binary protocol
  {
    AutoTransportImpl transport(cb);
    Buffer::OwnedImpl buffer;
    addInt32(buffer, 0xFF);
    addInt16(buffer, 0x8001);
    addInt16(buffer, 0);

    EXPECT_CALL(cb, transportFrameStart(absl::optional<uint32_t>(255U)));
    EXPECT_TRUE(transport.decodeFrameStart(buffer));
    EXPECT_EQ(transport.name(), "framed(auto)");
    EXPECT_EQ(buffer.length(), 4);
  }

  // Framed transport + compact protocol
  {
    AutoTransportImpl transport(cb);
    Buffer::OwnedImpl buffer;
    addInt32(buffer, 0xFFF);
    addInt16(buffer, 0x8201);
    addInt16(buffer, 0);

    EXPECT_CALL(cb, transportFrameStart(absl::optional<uint32_t>(4095U)));
    EXPECT_TRUE(transport.decodeFrameStart(buffer));
    EXPECT_EQ(transport.name(), "framed(auto)");
    EXPECT_EQ(buffer.length(), 4);
  }

  // Unframed transport + binary protocol
  {
    AutoTransportImpl transport(cb);
    Buffer::OwnedImpl buffer;
    addInt16(buffer, 0x8001);
    addRepeated(buffer, 6, 0);

    EXPECT_CALL(cb, transportFrameStart(absl::optional<uint32_t>()));
    EXPECT_TRUE(transport.decodeFrameStart(buffer));
    EXPECT_EQ(transport.name(), "unframed(auto)");
    EXPECT_EQ(buffer.length(), 8);
  }

  // Unframed transport + compact protocol
  {
    AutoTransportImpl transport(cb);
    Buffer::OwnedImpl buffer;
    addInt16(buffer, 0x8201);
    addRepeated(buffer, 6, 0);

    EXPECT_CALL(cb, transportFrameStart(absl::optional<uint32_t>()));
    EXPECT_TRUE(transport.decodeFrameStart(buffer));
    EXPECT_EQ(transport.name(), "unframed(auto)");
    EXPECT_EQ(buffer.length(), 8);
  }
}

TEST(AutoTransportTest, DecodeFrameEnd) {
  StrictMock<MockTransportCallbacks> cb;

  AutoTransportImpl transport(cb);
  Buffer::OwnedImpl buffer;
  addInt32(buffer, 0xFF);
  addInt16(buffer, 0x8001);
  addInt16(buffer, 0);

  EXPECT_CALL(cb, transportFrameStart(absl::optional<uint32_t>(255U)));
  EXPECT_TRUE(transport.decodeFrameStart(buffer));
  EXPECT_EQ(buffer.length(), 4);

  EXPECT_CALL(cb, transportFrameComplete());
  EXPECT_TRUE(transport.decodeFrameEnd(buffer));
}

TEST(AutoTransportTest, EncodeFrame) {
  StrictMock<MockTransportCallbacks> cb;
  MockTransport* mock_transport = new NiceMock<MockTransport>();

  AutoTransportImpl transport(cb);
  transport.setTransport(TransportPtr{mock_transport});

  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl message;

  EXPECT_CALL(*mock_transport, encodeFrame(Ref(buffer), Ref(message)));
  transport.encodeFrame(buffer, message);
}

TEST(AutoTransportTest, Name) {
  StrictMock<MockTransportCallbacks> cb;
  AutoTransportImpl transport(cb);
  EXPECT_EQ(transport.name(), "auto");
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
