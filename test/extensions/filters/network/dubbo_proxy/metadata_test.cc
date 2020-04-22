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
  EXPECT_EQ("method", invo->method_name());

  EXPECT_FALSE(invo->service_version().has_value());
  EXPECT_THROW(invo->service_version().value(), absl::bad_optional_access);
  invo->setServiceVersion("1.0.0");
  EXPECT_TRUE(invo->service_version().has_value());
  EXPECT_EQ("1.0.0", invo->service_version().value());

  EXPECT_FALSE(invo->service_group().has_value());
  EXPECT_THROW(invo->service_group().value(), absl::bad_optional_access);
  invo->setServiceGroup("group");
  EXPECT_TRUE(invo->service_group().has_value());
  EXPECT_EQ("group", invo->service_group().value());
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
