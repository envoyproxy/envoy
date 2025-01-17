#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/static_config_resource_detector.pb.h"
#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/static_config_resource_detector.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/static/config.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

TEST(StaticConfigResourceDetectorFactoryTest, Basic) {
  auto* factory = Registry::FactoryRegistry<ResourceDetectorFactory>::getFactory(
      "envoy.tracers.opentelemetry.resource_detectors.static_config");
  ASSERT_NE(factory, nullptr);

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: envoy.tracers.opentelemetry.resource_detectors.static_config
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.tracers.opentelemetry.resource_detectors.v3.StaticConfigResourceDetectorConfig
        attributes:
          key: value
          the_key: the_value
  )EOF";
  TestUtility::loadFromYaml(yaml, typed_config);

  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  EXPECT_NE(factory->createResourceDetector(typed_config.typed_config(), context), nullptr);
  EXPECT_STREQ(factory->name().c_str(),
               "envoy.tracers.opentelemetry.resource_detectors.static_config");
}

TEST(StaticConfigResourceDetectorFactoryTest, AttributesCorrectlyParsed) {
  const std::string yaml = R"EOF(
      attributes:
        key: value
        the_key: the_value
  )EOF";
  envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
      StaticConfigResourceDetectorConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  auto attributes = proto_config.attributes();
  ASSERT_EQ(attributes.size(), 2);
  ASSERT_EQ(attributes.count("key"), 1);
  ASSERT_EQ(attributes.count("the_key"), 1);
  EXPECT_EQ(attributes["key"], "value");
  EXPECT_EQ(attributes["the_key"], "the_value");
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
