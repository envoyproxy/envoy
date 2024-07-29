#include <string>

#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/static_config_resource_detector.pb.h"
#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/static_config_resource_detector.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/resource_detectors/static/static_config_resource_detector.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

// Test detector when when attributes is empty
TEST(StaticConfigResourceDetectorTest, EmptyAttributesMap) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;

  envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
      StaticConfigResourceDetectorConfig config;

  auto detector = std::make_unique<StaticConfigResourceDetector>(config, context);
  Resource resource = detector->detect();

  EXPECT_EQ(resource.schema_url_, "");
  EXPECT_TRUE(resource.attributes_.empty());
}

// Test detector with invalid values in attributes config
TEST(StaticConfigResourceDetectorTest, EmptyAttributesAreIgnored) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;

  ResourceAttributes expected_attributes = {{"key1", "val1"}};

  Api::ApiPtr api = Api::createApiForTest();
  EXPECT_CALL(context.server_factory_context_, api()).WillRepeatedly(ReturnRef(*api));

  const std::string yaml = R"EOF(
      attributes:
        key1: val1
        key2: ""
  )EOF";
  envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
      StaticConfigResourceDetectorConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  auto detector = std::make_unique<StaticConfigResourceDetector>(proto_config, context);
  Resource resource = detector->detect();

  EXPECT_EQ(resource.schema_url_, "");
  EXPECT_EQ(1, resource.attributes_.size());

  for (auto& actual : resource.attributes_) {
    auto expected = expected_attributes.find(actual.first);

    EXPECT_TRUE(expected != expected_attributes.end());
    EXPECT_EQ(expected->second, actual.second);
  }
}

// Test detector with valid values in attributes config
TEST(StaticConfigResourceDetectorTest, ValidAttributes) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;

  ResourceAttributes expected_attributes = {{"key1", "val1"}, {"key2", "val2"}};

  Api::ApiPtr api = Api::createApiForTest();
  EXPECT_CALL(context.server_factory_context_, api()).WillRepeatedly(ReturnRef(*api));

  const std::string yaml = R"EOF(
      attributes:
        key1: val1
        key2: val2
  )EOF";
  envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
      StaticConfigResourceDetectorConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  auto detector = std::make_unique<StaticConfigResourceDetector>(proto_config, context);
  Resource resource = detector->detect();

  EXPECT_EQ(resource.schema_url_, "");
  EXPECT_EQ(2, resource.attributes_.size());

  for (auto& actual : resource.attributes_) {
    auto expected = expected_attributes.find(actual.first);

    EXPECT_TRUE(expected != expected_attributes.end());
    EXPECT_EQ(expected->second, actual.second);
  }
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
