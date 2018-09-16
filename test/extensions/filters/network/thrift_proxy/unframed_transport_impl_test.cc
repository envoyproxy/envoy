#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/unframed_transport_impl.h"

#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

TEST(UnframedTransportTest, Name) {
  UnframedTransportImpl transport;
  EXPECT_EQ(transport.name(), "unframed");
}

TEST(UnframedTransportTest, Type) {
  UnframedTransportImpl transport;
  EXPECT_EQ(transport.type(), TransportType::Unframed);
}

TEST(UnframedTransportTest, DecodeFrameStart) {
  UnframedTransportImpl transport;

  Buffer::OwnedImpl buffer;
  addInt32(buffer, 0xDEADBEEF);
  EXPECT_EQ(buffer.length(), 4);

  MessageMetadata metadata;
  EXPECT_TRUE(transport.decodeFrameStart(buffer, metadata));
  EXPECT_THAT(metadata, IsEmptyMetadata());
  EXPECT_EQ(buffer.length(), 4);
}

TEST(UnframedTransportTest, DecodeFrameStartWithNoData) {
  UnframedTransportImpl transport;

  Buffer::OwnedImpl buffer;
  MessageMetadata metadata;
  EXPECT_FALSE(transport.decodeFrameStart(buffer, metadata));
  EXPECT_THAT(metadata, IsEmptyMetadata());
}

TEST(UnframedTransportTest, DecodeFrameEnd) {
  UnframedTransportImpl transport;

  Buffer::OwnedImpl buffer;
  EXPECT_TRUE(transport.decodeFrameEnd(buffer));
}

TEST(UnframedTransportTest, EncodeFrame) {
  UnframedTransportImpl transport;

  MessageMetadata metadata;

  Buffer::OwnedImpl message;
  message.add("fake message");

  Buffer::OwnedImpl buffer;
  transport.encodeFrame(buffer, metadata, message);

  EXPECT_EQ(0, message.length());
  EXPECT_EQ("fake message", buffer.toString());
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
