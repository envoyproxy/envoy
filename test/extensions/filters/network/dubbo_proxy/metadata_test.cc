#include "extensions/filters/network/dubbo_proxy/message_impl.h"
#include "extensions/filters/network/dubbo_proxy/metadata.h"
#include "extensions/filters/network/dubbo_proxy/serializer_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

TEST(MessageMetadataTest, Fields) {
  MessageMetadata metadata;
  auto invo = std::make_shared<RpcInvocationImpl>();

  EXPECT_FALSE(metadata.hasInvocationInfo());
  metadata.setInvocationInfo(invo);
  EXPECT_TRUE(metadata.hasInvocationInfo());

  EXPECT_THROW(metadata.timeout().value(), absl::bad_optional_access);
  metadata.setTimeout(3);
  EXPECT_TRUE(metadata.timeout().has_value());

  invo->setMethodName("method");
  EXPECT_EQ("method", invo->methodName());

  EXPECT_FALSE(invo->serviceVersion().has_value());
  EXPECT_THROW(invo->serviceVersion().value(), absl::bad_optional_access);
  invo->setServiceVersion("1.0.0");
  EXPECT_TRUE(invo->serviceVersion().has_value());
  EXPECT_EQ("1.0.0", invo->serviceVersion().value());

  EXPECT_FALSE(invo->serviceGroup().has_value());
  EXPECT_THROW(invo->serviceGroup().value(), absl::bad_optional_access);
  invo->setServiceGroup("group");
  EXPECT_TRUE(invo->serviceGroup().has_value());
  EXPECT_EQ("group", invo->serviceGroup().value());
}

TEST(MessageMetadataTest, Headers) {
  MessageMetadata metadata;
  auto invo = std::make_shared<RpcInvocationImpl>();

  EXPECT_FALSE(invo->hasHeaders());
  invo->addHeader("k", "v");
  EXPECT_EQ(invo->headers().size(), 1);
}

TEST(MessageMetadataTest, Parameters) {
  MessageMetadata metadata;
  auto invo = std::make_shared<RpcInvocationImpl>();

  EXPECT_FALSE(invo->hasParameters());
  invo->addParameterValue(0, "test");
  EXPECT_TRUE(invo->hasParameters());
  EXPECT_EQ(invo->parameters().size(), 1);
  EXPECT_EQ(invo->getParameterValue(0), "test");
  EXPECT_EQ(invo->getParameterValue(1), "");
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
