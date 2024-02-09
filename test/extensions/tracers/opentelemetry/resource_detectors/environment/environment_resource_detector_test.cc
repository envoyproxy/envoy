#include <string>

#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/environment_resource_detector.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/resource_detectors/environment/environment_resource_detector.h"

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

const std::string kOtelResourceAttributesEnv = "OTEL_RESOURCE_ATTRIBUTES";

// Test detector when env variable is not present
TEST(EnvironmentResourceDetectorTest, EnvVariableNotPresent) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;

  envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
      EnvironmentResourceDetectorConfig config;

  auto detector = std::make_unique<EnvironmentResourceDetector>(config, context);
  Resource resource = detector->detect();

  EXPECT_EQ(resource.schema_url_, "");
  EXPECT_TRUE(resource.attributes_.empty());
}

// Test detector when env variable is present but contains an empty value
TEST(EnvironmentResourceDetectorTest, EnvVariablePresentButEmpty) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  TestEnvironment::setEnvVar(kOtelResourceAttributesEnv, "", 1);
  Envoy::Cleanup cleanup([]() { TestEnvironment::unsetEnvVar(kOtelResourceAttributesEnv); });

  envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
      EnvironmentResourceDetectorConfig config;

  auto detector = std::make_unique<EnvironmentResourceDetector>(config, context);
  Resource resource = detector->detect();

  EXPECT_EQ(resource.schema_url_, "");
  EXPECT_TRUE(resource.attributes_.empty());
}

// Test detector with valid values in the env variable
TEST(EnvironmentResourceDetectorTest, EnvVariablePresentAndWithAttributes) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  TestEnvironment::setEnvVar(kOtelResourceAttributesEnv, "key1=val1,key2=val2", 1);
  Envoy::Cleanup cleanup([]() { TestEnvironment::unsetEnvVar(kOtelResourceAttributesEnv); });
  ResourceAttributes expected_attributes = {{"key1", "val1"}, {"key2", "val2"}};

  Api::ApiPtr api = Api::createApiForTest();
  EXPECT_CALL(context.server_factory_context_, api()).WillRepeatedly(ReturnRef(*api));

  envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
      EnvironmentResourceDetectorConfig config;

  auto detector = std::make_unique<EnvironmentResourceDetector>(config, context);
  Resource resource = detector->detect();

  EXPECT_EQ(resource.schema_url_, "");
  EXPECT_EQ(2, resource.attributes_.size());

  for (auto& actual : resource.attributes_) {
    auto expected = expected_attributes.find(actual.first);

    EXPECT_TRUE(expected != expected_attributes.end());
    EXPECT_EQ(expected->second, actual.second);
  }
}

// Test detector with invalid values mixed with valid ones in the env variable
TEST(EnvironmentResourceDetectorTest, EnvVariablePresentAndWithAttributesWrongFormat) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  TestEnvironment::setEnvVar(kOtelResourceAttributesEnv, "key1=val1,key2val2,key3/val3, , key", 1);
  Envoy::Cleanup cleanup([]() { TestEnvironment::unsetEnvVar(kOtelResourceAttributesEnv); });
  ResourceAttributes expected_attributes = {{"key1", "val"}};

  Api::ApiPtr api = Api::createApiForTest();
  EXPECT_CALL(context.server_factory_context_, api()).WillRepeatedly(ReturnRef(*api));

  envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
      EnvironmentResourceDetectorConfig config;

  auto detector = std::make_unique<EnvironmentResourceDetector>(config, context);
  Resource resource = detector->detect();

  EXPECT_EQ(resource.schema_url_, "");
  EXPECT_EQ(1, resource.attributes_.size());
  EXPECT_EQ(resource.attributes_.begin()->first, resource.attributes_.begin()->first);
  EXPECT_EQ(resource.attributes_.begin()->second, resource.attributes_.begin()->second);
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
