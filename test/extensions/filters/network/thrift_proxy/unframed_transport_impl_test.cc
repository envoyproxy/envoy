#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/unframed_transport_impl.h"

#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_base.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

TEST_F(TestBase, UnframedTransportTest_Name) {
  UnframedTransportImpl transport;
  EXPECT_EQ(transport.name(), "unframed");
}

TEST_F(TestBase, UnframedTransportTest_Type) {
  UnframedTransportImpl transport;
  EXPECT_EQ(transport.type(), TransportType::Unframed);
}

TEST_F(TestBase, UnframedTransportTest_DecodeFrameStart) {
  UnframedTransportImpl transport;

  Buffer::OwnedImpl buffer;
  buffer.writeBEInt<uint32_t>(0xDEADBEEF);
  EXPECT_EQ(buffer.length(), 4);

  MessageMetadata metadata;
  EXPECT_TRUE(transport.decodeFrameStart(buffer, metadata));
  EXPECT_THAT(metadata, IsEmptyMetadata());
  EXPECT_EQ(buffer.length(), 4);
}

TEST_F(TestBase, UnframedTransportTest_DecodeFrameStartWithNoData) {
  UnframedTransportImpl transport;

  Buffer::OwnedImpl buffer;
  MessageMetadata metadata;
  EXPECT_FALSE(transport.decodeFrameStart(buffer, metadata));
  EXPECT_THAT(metadata, IsEmptyMetadata());
}

TEST_F(TestBase, UnframedTransportTest_DecodeFrameEnd) {
  UnframedTransportImpl transport;

  Buffer::OwnedImpl buffer;
  EXPECT_TRUE(transport.decodeFrameEnd(buffer));
}

TEST_F(TestBase, UnframedTransportTest_EncodeFrame) {
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
