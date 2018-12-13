#include "extensions/filters/network/dubbo_proxy/metadata.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

TEST(MessageMetadataTest, Fields) {
  MessageMetadata metadata;

  EXPECT_FALSE(metadata.method_name().has_value());
  EXPECT_THROW(metadata.method_name().value(), absl::bad_optional_access);
  metadata.setMethodName("method");
  EXPECT_TRUE(metadata.method_name().has_value());
  EXPECT_EQ("method", metadata.method_name());

  EXPECT_FALSE(metadata.service_version().has_value());
  EXPECT_THROW(metadata.service_version().value(), absl::bad_optional_access);
  metadata.setServiceVersion("1.0.0");
  EXPECT_TRUE(metadata.service_version().has_value());
  EXPECT_EQ("1.0.0", metadata.service_version().value());

  EXPECT_FALSE(metadata.service_group().has_value());
  EXPECT_THROW(metadata.service_group().value(), absl::bad_optional_access);
  metadata.setServiceGroup("group");
  EXPECT_TRUE(metadata.service_group().has_value());
  EXPECT_EQ("group", metadata.service_group().value());
}

TEST(MessageMetadataTest, Headers) {
  MessageMetadata metadata;

  EXPECT_FALSE(metadata.hasHeaders());
  metadata.addHeader("k", "v");
  EXPECT_EQ(metadata.headers().size(), 1);
}

TEST(MessageMetadataTest, Parameters) {
  MessageMetadata metadata;

  EXPECT_FALSE(metadata.hasParameters());
  metadata.addParameterValue(0, "test");
  EXPECT_TRUE(metadata.hasParameters());
  EXPECT_EQ(metadata.parameters().size(), 1);
  EXPECT_EQ(metadata.getParameterValue(0), "test");
  EXPECT_EQ(metadata.getParameterValue(1), "");
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
