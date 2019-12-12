#include "envoy/api/v2/route/route.pb.validate.h"
#include "envoy/config/filter/http/cors/v2/cors.pb.validate.h"

#include <string>

#include "common/router/config_impl.h"

#include "test/mocks/runtime/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {
namespace {
// Helper utility to convert a YAML string blob into a protobuf configuration.
template <typename T>
T createConfigFromYaml(const std::string& config_str) {
  T policy;
  TestUtility::loadFromYamlAndValidate(config_str, policy);
  return policy;
}
}

class CorsLegacyConfigTest : public testing::Test {
public:
  envoy::api::v2::route::CorsPolicy createConfig(const std::string& yaml) {
    return createConfigFromYaml<envoy::api::v2::route::CorsPolicy>(yaml);
  }

  testing::NiceMock<Runtime::MockLoader> runtime_;
};

TEST_F(CorsLegacyConfigTest, BasicTest) {
  const std::string simple_config = R"(
  allow_origin_string_match: []
  allow_methods: "allow-methods"
  allow_headers: "allow-headers"
  expose_headers: "expose-headers"
  max_age: "10"
  allow_credentials: false
  )";

  const auto proto_config = createConfig(simple_config);
  const Router::CorsPolicyImpl config(proto_config, runtime_);
  EXPECT_TRUE(config.allowOrigins().empty());
  EXPECT_EQ("allow-methods", config.allowMethods());
  EXPECT_EQ("allow-headers", config.allowHeaders());
  EXPECT_EQ("expose-headers", config.exposeHeaders());
  EXPECT_EQ("10", config.maxAge());
}

TEST_F(CorsLegacyConfigTest, DEPRECATED_FEATURE_TEST(DeprecatedFields)) {
  const std::string simple_config = R"(
  allow_origin: "allow-origin"
  allow_origin_regex: "regex"
  enabled: false
  )";

  const auto proto_config = createConfig(simple_config);
  const Router::CorsPolicyImpl config(proto_config, runtime_);
  EXPECT_EQ(2, config.allowOrigins().size());
  EXPECT_FALSE(config.enabled());
}

class CorsConfigTest : public testing::Test {
public:
  envoy::config::filter::http::cors::v2::PerRouteCorsPolicy createConfig(const std::string& yaml) {
    return createConfigFromYaml<envoy::config::filter::http::cors::v2::PerRouteCorsPolicy>(yaml);
  }

  testing::NiceMock<Runtime::MockLoader> runtime_;
};

TEST_F(CorsConfigTest, BasicTest) {
  const std::string simple_config = R"(
  allow_origin_string_match: []
  allow_methods: "allow-methods"
  allow_headers: "allow-headers"
  expose_headers: "expose-headers"
  max_age: "10"
  allow_credentials: false
  )";

  const auto proto_config = createConfig(simple_config);
  const Router::CorsPolicyImpl config(proto_config, runtime_);
  EXPECT_TRUE(config.allowOrigins().empty());
  EXPECT_EQ("allow-methods", config.allowMethods());
  EXPECT_EQ("allow-headers", config.allowHeaders());
  EXPECT_EQ("expose-headers", config.exposeHeaders());
  EXPECT_EQ("10", config.maxAge());
}

} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
