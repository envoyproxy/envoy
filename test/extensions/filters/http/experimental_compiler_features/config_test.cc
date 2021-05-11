#include "envoy/extensions/filters/http/experimental_compiler_features/v3/experimental_compiler_features.pb.h"
#include "envoy/extensions/filters/http/experimental_compiler_features/v3/experimental_compiler_features.pb.validate.h"

#include "extensions/filters/http/experimental_compiler_features/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExperimentalCompilerFeatures {

using ExperimentalCompilerFeaturesProtoConfig = envoy::extensions::filters::http::
    experimental_compiler_features::v3::ExperimentalCompilerFeatures;

TEST(ExperimentalCompilerFeaturesConfigTest, SimpleConfig) {
  const std::string yaml = R"EOF(
key: fooKey
value: fooValue
associative_container_use_contains: true
enum_members_in_scope: true
str_starts_with: true
str_ends_with: true
  )EOF";

  ExperimentalCompilerFeaturesProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  ExperimentalCompilerFeatures::ExperimentalCompilerFeaturesFactory factory;

  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamDecoderFilter(_));
  cb(filter_callbacks);
}

} // namespace ExperimentalCompilerFeatures
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
