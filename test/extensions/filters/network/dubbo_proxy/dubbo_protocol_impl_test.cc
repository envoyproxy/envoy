#include "extensions/filters/network/dubbo_proxy/dubbo_protocol_impl.h"
#include "extensions/filters/network/dubbo_proxy/protocol.h"

#include "test/extensions/filters/network/dubbo_proxy/mocks.h"
#include "test/extensions/filters/network/dubbo_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

using testing::StrictMock;

TEST(DubboProtocolImplTest, NotEnoughData) {
  Buffer::OwnedImpl buffer;
  StrictMock<MockProtocolCallbacks> cb;
  DubboProtocolImpl dubbo_protocol(cb);
  Protocol::Context context;
  EXPECT_FALSE(dubbo_protocol.decode(buffer, &context));

  addRepeated(buffer, 15, 0);

  EXPECT_FALSE(dubbo_protocol.decode(buffer, &context));
}

TEST(DubboProtocolImplTest, Name) {
  StrictMock<MockProtocolCallbacks> cb;
  DubboProtocolImpl dubbo_protocol(cb);
  EXPECT_EQ(dubbo_protocol.name(), "dubbo");
}

TEST(DubboProtocolImplTest, InvalidProtocol) {
  StrictMock<MockProtocolCallbacks> cb;
  DubboProtocolImpl dubbo_protocol(cb);
  Protocol::Context context;
  // Invalid dubbo magic number
  {
    Buffer::OwnedImpl buffer;
    addInt64(buffer, 0);
    addInt64(buffer, 0);

    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decode(buffer, &context), EnvoyException,
                              "invalid dubbo message magic number 0");
  }

  // Invalid message size
  {
    Buffer::OwnedImpl buffer;
    addInt8(buffer, 0xda);
    addInt8(buffer, 0xbb);
    addInt8(buffer, 0xc2);
    addInt8(buffer, 0);
    addInt64(buffer, 1);
    addInt32(buffer, DubboProtocolImpl::MaxBodySize + 1);
    std::string exception_string =
        fmt::format("invalid dubbo message size {}", DubboProtocolImpl::MaxBodySize + 1);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decode(buffer, &context), EnvoyException,
                              exception_string);
  }

  // Invalid serialization type
  {
    Buffer::OwnedImpl buffer;
    addInt8(buffer, 0xda);
    addInt8(buffer, 0xbb);
    addInt8(buffer, 0xc3);
    addInt8(buffer, 0);
    addInt64(buffer, 1);
    addInt32(buffer, 0xff);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decode(buffer, &context), EnvoyException,
                              "invalid dubbo message serialization type 3");
  }

  // Invalid response status
  {
    Buffer::OwnedImpl buffer;
    addInt8(buffer, 0xda);
    addInt8(buffer, 0xbb);
    addInt8(buffer, 0x42);
    addInt8(buffer, 0);
    addInt64(buffer, 1);
    addInt32(buffer, 0xff);
    EXPECT_THROW_WITH_MESSAGE(dubbo_protocol.decode(buffer, &context), EnvoyException,
                              "invalid dubbo message response status 0");
  }
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy