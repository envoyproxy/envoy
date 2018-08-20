#include "extensions/filters/network/dubbo_proxy/hessian_serializer_impl.h"

#include "test/extensions/filters/network/dubbo_proxy/mocks.h"
#include "test/extensions/filters/network/dubbo_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NotNull;
using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

TEST(HessianProtocolTest, Name) {
  StrictMock<MockSerializationCallbacks> cb;
  HessianSerializerImpl serializer(cb);
  EXPECT_EQ(serializer.name(), "hessian");
}

TEST(HessianProtocolTest, deserializeRpcInvocation) {
  MockSerializationCallbacks cb;
  HessianSerializerImpl serializer(cb);
  // incorrect body size
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {
                     0x05, '2', '.', '0', '.', '2', // Dubbo version
                     0x04, 't', 'e', 's', 't',      // Service naem
                     0x05, '0', '.', '0', '.', '0', // Service version
                     0x04, 't', 'e', 's', 't',      // method name
                 });

  std::string exception_string = fmt::format("RpcInvocation size({}) large than body size({})",
                                             buffer.length(), buffer.length() - 1);
  EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcInvocation(buffer, buffer.length() - 1),
                            EnvoyException, exception_string);
}

TEST(HessianProtocolTest, deserializeRpcResult) {
  StrictMock<MockSerializationCallbacks> cb;
  HessianSerializerImpl serializer(cb);
  // incorrect body size
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {
                       0x94,                     // return type
                       0x05, 't', 'e', 's', 't', // return body
                   });
    EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcResult(buffer, 0), EnvoyException,
                              "RpcResult size(1) large than body size(0)");
  }

  // incorrect return type
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {
                       0x96,                     // incorrect return type
                       0x05, 't', 'e', 's', 't', // return body
                   });
    EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcResult(buffer, buffer.length()),
                              EnvoyException, "not supported return type 6");
  }

  // incorrect value size
  {
    Buffer::OwnedImpl buffer;
    addSeq(buffer, {
                       0x92,                     // without the value of the return type
                       0x05, 't', 'e', 's', 't', // return body
                   });
    std::string exception_string =
        fmt::format("RpcResult is no value, but the rest of the body size({}) not equal 0",
                    buffer.length() - 1);
    EXPECT_THROW_WITH_MESSAGE(serializer.deserializeRpcResult(buffer, buffer.length()),
                              EnvoyException, exception_string);
  }
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy