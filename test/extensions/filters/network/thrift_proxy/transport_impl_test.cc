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
  MessageMetadata metadata;

  EXPECT_FALSE(transport.decodeFrameStart(buffer, metadata));
  EXPECT_THAT(metadata, IsEmptyMetadata());

  addRepeated(buffer, 7, 0);

  EXPECT_FALSE(transport.decodeFrameStart(buffer, metadata));
  EXPECT_THAT(metadata, IsEmptyMetadata());
}

TEST(AutoTransportTest, UnknownTransport) {
  AutoTransportImpl transport;

  // Looks like unframed, but fails protocol check.
  {
    Buffer::OwnedImpl buffer;
    addInt32(buffer, 0);
    addInt32(buffer, 0);

    MessageMetadata metadata;
    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "unknown thrift auto transport frame start 00 00 00 00 00 00 00 00");
    EXPECT_THAT(metadata, IsEmptyMetadata());
  }

  // Looks like framed, but fails protocol check.
  {
    Buffer::OwnedImpl buffer;
    addInt32(buffer, 0xFF);
    addInt32(buffer, 0);

    MessageMetadata metadata;
    EXPECT_THROW_WITH_MESSAGE(transport.decodeFrameStart(buffer, metadata), EnvoyException,
                              "unknown thrift auto transport frame start 00 00 00 ff 00 00 00 00");
    EXPECT_THAT(metadata, IsEmptyMetadata());
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

    MessageMetadata metadata;
    EXPECT_TRUE(transport.decodeFrameStart(buffer, metadata));
    EXPECT_THAT(metadata, HasOnlyFrameSize(255U));
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

    MessageMetadata metadata;
    EXPECT_TRUE(transport.decodeFrameStart(buffer, metadata));
    EXPECT_THAT(metadata, HasOnlyFrameSize(4095U));
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

    MessageMetadata metadata;
    EXPECT_TRUE(transport.decodeFrameStart(buffer, metadata));
    EXPECT_THAT(metadata, IsEmptyMetadata());
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

    MessageMetadata metadata;
    EXPECT_TRUE(transport.decodeFrameStart(buffer, metadata));
    EXPECT_THAT(metadata, IsEmptyMetadata());
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

  MessageMetadata metadata;
  EXPECT_TRUE(transport.decodeFrameStart(buffer, metadata));

  EXPECT_EQ(buffer.length(), 4);

  EXPECT_TRUE(transport.decodeFrameEnd(buffer));
}

TEST(AutoTransportTest, EncodeFrame) {
  MockTransport* mock_transport = new NiceMock<MockTransport>();

  AutoTransportImpl transport;
  transport.setTransport(TransportPtr{mock_transport});

  MessageMetadata metadata;
  Buffer::OwnedImpl buffer;
  Buffer::OwnedImpl message;

  EXPECT_CALL(*mock_transport, encodeFrame(Ref(buffer), Ref(metadata), Ref(message)));
  transport.encodeFrame(buffer, metadata, message);
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
