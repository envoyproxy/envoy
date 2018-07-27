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

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

TEST(TransportNames, FromType) {
  for (int i = 0; i <= static_cast<int>(TransportType::LastTransportType); i++) {
    TransportType type = static_cast<TransportType>(i);
    EXPECT_NE("", TransportNames::get().fromType(type));
  }
}

TEST(AutoTransportTest, NotEnoughData) {
  Buffer::OwnedImpl buffer;
  AutoTransportImpl transport;
  absl::optional<uint32_t> size = 100;

  EXPECT_FALSE(transport.decodeFrameStart(buffer, size));
  EXPECT_EQ(absl::optional<uint32_t>(100), size);

  addRepeated(buffer, 7, 0);

  EXPECT_FALSE(transport.decodeFrameStart(buffer, size));
  EXPECT_EQ(absl::optional<uint32_t>(100), size);
}

TEST(AutoTransportTest, UnknownTransport) {
  AutoTransportImpl transport;

  // Looks like unframed, but fails protocol check.
  {
    Buffer::OwnedImpl buffer;
    addInt32(buffer, 0);
    addInt32(buffer, 0);

    absl::optional<uint32_t> size = 100;
    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, size), EnvoyException,
                              "unknown thrift auto transport frame start 00 00 00 00 00 00 00 00");
    EXPECT_EQ(absl::optional<uint32_t>(100), size);
  }

  // Looks like framed, but fails protocol check.
  {
    Buffer::OwnedImpl buffer;
    addInt32(buffer, 0xFF);
    addInt32(buffer, 0);

    absl::optional<uint32_t> size = 100;
    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, size), EnvoyException,
                              "unknown thrift auto transport frame start 00 00 00 ff 00 00 00 00");
    EXPECT_EQ(absl::optional<uint32_t>(100), size);
  }
}

TEST(AutoTransportTest, DecodeFrameStart) {
  // Framed transport + binary protocol
  {
    AutoTransportImpl transport;
    Buffer::OwnedImpl buffer;
    addInt32(buffer, 0xFF);
    addInt16(buffer, 0x8001);
    addInt16(buffer, 0);

    absl::optional<uint32_t> size;
    EXPECT_TRUE(transport.decodeFrameStart(buffer, size));
    EXPECT_EQ(absl::optional<uint32_t>(255), size);
    EXPECT_EQ(transport.name(), "framed(auto)");
    EXPECT_EQ(transport.type(), TransportType::Framed);
    EXPECT_EQ(buffer.length(), 4);
  }

  // Framed transport + compact protocol
  {
    AutoTransportImpl transport;
    Buffer::OwnedImpl buffer;
    addInt32(buffer, 0xFFF);
    addInt16(buffer, 0x8201);
    addInt16(buffer, 0);

    absl::optional<uint32_t> size;
    EXPECT_TRUE(transport.decodeFrameStart(buffer, size));
    EXPECT_EQ(absl::optional<uint32_t>(4095), size);
    EXPECT_EQ(transport.name(), "framed(auto)");
    EXPECT_EQ(transport.type(), TransportType::Framed);
    EXPECT_EQ(buffer.length(), 4);
  }

  // Unframed transport + binary protocol
  {
    AutoTransportImpl transport;
    Buffer::OwnedImpl buffer;
    addInt16(buffer, 0x8001);
    addRepeated(buffer, 6, 0);

    absl::optional<uint32_t> size = 1;
    EXPECT_TRUE(transport.decodeFrameStart(buffer, size));
    EXPECT_FALSE(size.has_value());
    EXPECT_EQ(transport.name(), "unframed(auto)");
    EXPECT_EQ(transport.type(), TransportType::Unframed);
    EXPECT_EQ(buffer.length(), 8);
  }

  // Unframed transport + compact protocol
  {
    AutoTransportImpl transport;
    Buffer::OwnedImpl buffer;
    addInt16(buffer, 0x8201);
    addRepeated(buffer, 6, 0);

    absl::optional<uint32_t> size = 1;
    EXPECT_TRUE(transport.decodeFrameStart(buffer, size));
    EXPECT_FALSE(size.has_value());
    EXPECT_EQ(transport.name(), "unframed(auto)");
    EXPECT_EQ(transport.type(), TransportType::Unframed);
    EXPECT_EQ(buffer.length(), 8);
  }
}

TEST(AutoTransportTest, DecodeFrameEnd) {
  AutoTransportImpl transport;
  Buffer::OwnedImpl buffer;
  addInt32(buffer, 0xFF);
  addInt16(buffer, 0x8001);
  addInt16(buffer, 0);

  absl::optional<uint32_t> size;
  EXPECT_TRUE(transport.decodeFrameStart(buffer, size));
  EXPECT_EQ(buffer.length(), 4);

  EXPECT_TRUE(transport.decodeFrameEnd(buffer));
}

TEST(AutoTransportTest, EncodeFrame) {
  MockTransport* mock_transport = new NiceMock<MockTransport>();

  AutoTransportImpl transport;
  transport.setTransport(TransportPtr{mock_transport});

  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl message;

  EXPECT_CALL(*mock_transport, encodeFrame(Ref(buffer), Ref(message)));
  transport.encodeFrame(buffer, message);
}

TEST(AutoTransportTest, Name) {
  AutoTransportImpl transport;
  EXPECT_EQ(transport.name(), "auto");
}

TEST(AutoTransportTest, Type) {
  AutoTransportImpl transport;
  EXPECT_EQ(transport.type(), TransportType::Auto);
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
