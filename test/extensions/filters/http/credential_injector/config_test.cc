#include "source/extensions/filters/http/credential_injector/config.h"
#include "source/extensions/filters/http/credential_injector/credential_injector_filter.h"

#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {

TEST(Factory, UnregisteredExtension) {
  const std::string yaml_string = R"EOF(
  overwrite: true
  allow_request_without_credential: true
  credential:
    name: undefined_credential
    typed_config:
      "@type": type.googleapis.com/test.mock_credential.Unregistered
  )EOF";

  envoy::extensions::filters::http::credential_injector::v3::CredentialInjector proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  CredentialInjectorFilterFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createFilterFactoryFromProto(proto_config, "stats", context), EnvoyException,
      "Didn't find a registered implementation for type: 'test.mock_credential.Unregistered'");
}

} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
