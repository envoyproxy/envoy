#include <string>

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

TEST(EnvironmentResourceDetectorTest, Detection) {
  // OTEL_RESOURCE_ATTRIBUTES env variable not present
  {
    NiceMock<Server::Configuration::MockTracerFactoryContext> context;
    TestEnvironment::unsetEnvVar(kOtelResourceAttributesEnv);

    auto detector = std::make_shared<EnvironmentResourceDetector>(context);
    Resource resource = detector->detect();

    EXPECT_EQ(resource.schemaUrl, "");
    EXPECT_TRUE(resource.attributes.empty());
  }
  // OTEL_RESOURCE_ATTRIBUTES env variable present but empty
  {
    NiceMock<Server::Configuration::MockTracerFactoryContext> context;
    TestEnvironment::setEnvVar(kOtelResourceAttributesEnv, "", 1);

    auto detector = std::make_shared<EnvironmentResourceDetector>(context);
    Resource resource = detector->detect();

    EXPECT_EQ(resource.schemaUrl, "");
    EXPECT_TRUE(resource.attributes.empty());
    TestEnvironment::unsetEnvVar(kOtelResourceAttributesEnv);
  }
  // // OTEL_RESOURCE_ATTRIBUTES env variable present and with attributes
  {
    NiceMock<Server::Configuration::MockTracerFactoryContext> context;
    TestEnvironment::setEnvVar(kOtelResourceAttributesEnv, "key1=val1,key2=val2", 1);
    ResourceAttributes expected_attributes = {{"key1", "val1"}, {"key2", "val2"}};

    Api::ApiPtr api = Api::createApiForTest();
    EXPECT_CALL(context.server_factory_context_, api()).WillRepeatedly(ReturnRef(*api));

    auto detector = std::make_shared<EnvironmentResourceDetector>(context);
    Resource resource = detector->detect();

    EXPECT_EQ(resource.schemaUrl, "");
    EXPECT_EQ(2, resource.attributes.size());

    for (auto& actual : resource.attributes) {
      auto expected = expected_attributes.find(actual.first);

      EXPECT_TRUE(expected != expected_attributes.end());
      EXPECT_EQ(expected->second, actual.second);
    }
    TestEnvironment::unsetEnvVar(kOtelResourceAttributesEnv);
  }
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
