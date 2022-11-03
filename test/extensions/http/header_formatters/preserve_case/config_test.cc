#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/http/header_formatters/preserve_case/config.h"
#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderFormatters {
namespace PreserveCase {

TEST(PreserveCaseFormatterFactoryConfigTest, Basic) {
  auto* factory =
      Registry::FactoryRegistry<Envoy::Http::StatefulHeaderKeyFormatterFactoryConfig>::getFactory(
          "preserve_case");
  ASSERT_NE(factory, nullptr);

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: preserve_case
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.http.header_formatters.preserve_case.v3.PreserveCaseFormatterConfig
        forward_reason_phrase: false
        formatter_type_on_envoy_headers: DEFAULT
)EOF";
  TestUtility::loadFromYaml(yaml, typed_config);
  auto header_formatter_config = Envoy::Config::Utility::translateAnyToFactoryConfig(
      typed_config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);
  EXPECT_NE(factory->createFromProto(*header_formatter_config), nullptr);
}

TEST(PreserveCaseFormatterFactoryConfigTest, InvalidfFormatterTypeOnEnvoyHeaders) {
  auto* factory =
      Registry::FactoryRegistry<Envoy::Http::StatefulHeaderKeyFormatterFactoryConfig>::getFactory(
          "preserve_case");
  ASSERT_NE(factory, nullptr);

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: preserve_case
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.http.header_formatters.preserve_case.v3.PreserveCaseFormatterConfig
        forward_reason_phrase: true
        formatter_type_on_envoy_headers: INVALID
)EOF";
  EXPECT_THROW(TestUtility::loadFromYaml(yaml, typed_config);, EnvoyException);
}

TEST(PreserveCaseFormatterFactoryConfigTest, PreserveCaseFormatterFactoryConfig_DEFAULT) {
  auto* factory =
      Registry::FactoryRegistry<Envoy::Http::StatefulHeaderKeyFormatterFactoryConfig>::getFactory(
          "preserve_case");
  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: preserve_case
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.http.header_formatters.preserve_case.v3.PreserveCaseFormatterConfig
        forward_reason_phrase: false
        formatter_type_on_envoy_headers: DEFAULT
)EOF";
  TestUtility::loadFromYaml(yaml, typed_config);
  auto header_formatter_config = Envoy::Config::Utility::translateAnyToFactoryConfig(
      typed_config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);
  auto formatter_factory = factory->createFromProto(*header_formatter_config);
  auto formatter = formatter_factory->create();

  formatter->processKey("Foo");
  EXPECT_EQ("Foo", formatter->format("foo"));
  EXPECT_EQ("Foo", formatter->format("Foo"));
  EXPECT_EQ("hello-world", formatter->format("hello-world"));
}

TEST(PreserveCaseFormatterFactoryConfigTest, PreserveCaseFormatterFactoryConfig_PROPER_CASE) {
  auto* factory =
      Registry::FactoryRegistry<Envoy::Http::StatefulHeaderKeyFormatterFactoryConfig>::getFactory(
          "preserve_case");
  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: preserve_case
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.http.header_formatters.preserve_case.v3.PreserveCaseFormatterConfig
        forward_reason_phrase: false
        formatter_type_on_envoy_headers: PROPER_CASE
)EOF";
  TestUtility::loadFromYaml(yaml, typed_config);
  auto header_formatter_config = Envoy::Config::Utility::translateAnyToFactoryConfig(
      typed_config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);
  auto formatter_factory = factory->createFromProto(*header_formatter_config);
  auto formatter = formatter_factory->create();

  formatter->processKey("Foo");
  EXPECT_EQ("Foo", formatter->format("foo"));
  EXPECT_EQ("Foo", formatter->format("Foo"));
  EXPECT_EQ("Hello-World", formatter->format("hello-world"));
}

} // namespace PreserveCase
} // namespace HeaderFormatters
} // namespace Http
} // namespace Extensions
} // namespace Envoy
