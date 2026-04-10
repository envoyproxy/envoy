#include "source/common/version/version.h"
#include "source/extensions/common/opentelemetry/exporters/otlp/environment.h"
#include "source/extensions/common/opentelemetry/exporters/otlp/populate_attribute_utils.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace OpenTelemetry {
namespace Exporters {
namespace Otlp {
namespace {

// Tests for PopulateAttributeUtils::makeKeyValue
TEST(PopulateAttributeUtilsTest, MakeKeyValue) {
  auto kv = PopulateAttributeUtils::makeKeyValue("my_key", "my_value");
  EXPECT_EQ("my_key", kv.key());
  EXPECT_EQ("my_value", kv.value().string_value());
}

TEST(PopulateAttributeUtilsTest, MakeKeyValueEmptyStrings) {
  auto kv = PopulateAttributeUtils::makeKeyValue("", "");
  EXPECT_EQ("", kv.key());
  EXPECT_EQ("", kv.value().string_value());
}

// Tests for PopulateAttributeUtils::populateAnyValue — scalar types
TEST(PopulateAttributeUtilsTest, PopulateAnyValueBool) {
  AnyValue proto;
  PopulateAttributeUtils::populateAnyValue(proto, OTelAttribute{true});
  EXPECT_TRUE(proto.bool_value());

  PopulateAttributeUtils::populateAnyValue(proto, OTelAttribute{false});
  EXPECT_FALSE(proto.bool_value());
}

TEST(PopulateAttributeUtilsTest, PopulateAnyValueInt32) {
  AnyValue proto;
  PopulateAttributeUtils::populateAnyValue(proto, OTelAttribute{static_cast<int32_t>(42)});
  EXPECT_EQ(42, proto.int_value());

  PopulateAttributeUtils::populateAnyValue(proto, OTelAttribute{static_cast<int32_t>(-7)});
  EXPECT_EQ(-7, proto.int_value());
}

TEST(PopulateAttributeUtilsTest, PopulateAnyValueUInt32) {
  AnyValue proto;
  PopulateAttributeUtils::populateAnyValue(proto, OTelAttribute{static_cast<uint32_t>(100)});
  EXPECT_EQ(100, proto.int_value());
}

TEST(PopulateAttributeUtilsTest, PopulateAnyValueInt64) {
  AnyValue proto;
  PopulateAttributeUtils::populateAnyValue(proto,
                                           OTelAttribute{static_cast<int64_t>(9876543210LL)});
  EXPECT_EQ(9876543210LL, proto.int_value());
}

TEST(PopulateAttributeUtilsTest, PopulateAnyValueUInt64) {
  AnyValue proto;
  PopulateAttributeUtils::populateAnyValue(proto,
                                           OTelAttribute{static_cast<uint64_t>(12345678900ULL)});
  EXPECT_EQ(12345678900LL, proto.int_value());
}

TEST(PopulateAttributeUtilsTest, PopulateAnyValueDouble) {
  AnyValue proto;
  PopulateAttributeUtils::populateAnyValue(proto, OTelAttribute{3.14});
  EXPECT_DOUBLE_EQ(3.14, proto.double_value());
}

TEST(PopulateAttributeUtilsTest, PopulateAnyValueString) {
  AnyValue proto;
  PopulateAttributeUtils::populateAnyValue(proto, OTelAttribute{std::string("hello")});
  EXPECT_EQ("hello", proto.string_value());
}

TEST(PopulateAttributeUtilsTest, PopulateAnyValueStringView) {
  AnyValue proto;
  const absl::string_view sv = "world";
  PopulateAttributeUtils::populateAnyValue(proto, OTelAttribute{sv});
  EXPECT_EQ("world", proto.string_value());
}

// Tests for PopulateAttributeUtils::populateAnyValue — array/span types
TEST(PopulateAttributeUtilsTest, PopulateAnyValueVectorOfStrings) {
  AnyValue proto;
  std::vector<std::string> values = {"alpha", "beta", "gamma"};
  PopulateAttributeUtils::populateAnyValue(proto, OTelAttribute{values});
  ASSERT_EQ(3, proto.array_value().values_size());
  EXPECT_EQ("alpha", proto.array_value().values(0).string_value());
  EXPECT_EQ("beta", proto.array_value().values(1).string_value());
  EXPECT_EQ("gamma", proto.array_value().values(2).string_value());
}

TEST(PopulateAttributeUtilsTest, PopulateAnyValueVectorOfStringViews) {
  AnyValue proto;
  std::vector<absl::string_view> values = {"x", "y"};
  PopulateAttributeUtils::populateAnyValue(proto, OTelAttribute{values});
  ASSERT_EQ(2, proto.array_value().values_size());
  EXPECT_EQ("x", proto.array_value().values(0).string_value());
  EXPECT_EQ("y", proto.array_value().values(1).string_value());
}

TEST(PopulateAttributeUtilsTest, PopulateAnyValueEmptyVectorOfStrings) {
  AnyValue proto;
  std::vector<std::string> empty;
  PopulateAttributeUtils::populateAnyValue(proto, OTelAttribute{empty});
  EXPECT_EQ(0, proto.array_value().values_size());
}

// Tests that passing an unsupported OTelAttribute variant (e.g. std::vector<bool>, which
// corresponds to KTypeSpanBool and is not handled by the switch) triggers IS_ENVOY_BUG.
TEST(PopulateAttributeUtilsTest, PopulateAnyValueUnsupportedTypeTriggersEnvoyBug) {
  AnyValue proto;
  std::vector<bool> unsupported = {true, false};
  EXPECT_ENVOY_BUG(PopulateAttributeUtils::populateAnyValue(proto, OTelAttribute{unsupported}),
                   "unexpected otel attribute type");
}

// Tests for GetUserAgent
TEST(EnvironmentTest, GetUserAgent) {
  const std::string& agent = GetUserAgent();
  EXPECT_EQ(agent, "OTel-OTLP-Exporter-Envoy/" + Envoy::VersionInfo::version());
}

TEST(EnvironmentTest, GetUserAgentReturnsSameInstance) {
  const std::string& agent1 = GetUserAgent();
  const std::string& agent2 = GetUserAgent();
  // CONSTRUCT_ON_FIRST_USE guarantees a singleton: same address.
  EXPECT_EQ(&agent1, &agent2);
}

} // namespace
} // namespace Otlp
} // namespace Exporters
} // namespace OpenTelemetry
} // namespace Extensions
} // namespace Envoy
