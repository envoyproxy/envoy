#include "source/extensions/filters/network/dubbo_proxy/message_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/metadata.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

TEST(MessageMetadataTest, MessageMetadataTest) {
  auto invocation_info = std::make_shared<RpcInvocationImpl>();
  MessageMetadata meta;

  EXPECT_EQ(false, meta.hasInvocationInfo());

  meta.setInvocationInfo(invocation_info);

  EXPECT_EQ(true, meta.hasInvocationInfo());
  EXPECT_EQ(invocation_info.get(), &meta.invocationInfo());

  meta.setProtocolType(ProtocolType::Dubbo);
  EXPECT_EQ(ProtocolType::Dubbo, meta.protocolType());

  meta.setProtocolVersion(27);
  EXPECT_EQ(27, meta.protocolVersion());

  meta.setMessageType(MessageType::Request);
  EXPECT_EQ(MessageType::Request, meta.messageType());

  meta.setRequestId(1234567);
  EXPECT_EQ(1234567, meta.requestId());

  EXPECT_EQ(false, meta.timeout().has_value());
  meta.setTimeout(6000);
  EXPECT_EQ(6000, meta.timeout().value());

  meta.setTwoWayFlag(true);
  EXPECT_EQ(true, meta.isTwoWay());

  meta.setSerializationType(SerializationType::Hessian2);
  EXPECT_EQ(SerializationType::Hessian2, meta.serializationType());

  EXPECT_EQ(false, meta.hasResponseStatus());

  meta.setResponseStatus(ResponseStatus::ServerTimeout);
  EXPECT_EQ(ResponseStatus::ServerTimeout, meta.responseStatus());

  EXPECT_EQ(true, meta.hasResponseStatus());
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
