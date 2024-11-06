#include "envoy/extensions/filters/http/api_key_auth/v3/api_key_auth.pb.h"

#include "source/extensions/filters/http/api_key_auth/api_key_auth.h"
#include "source/extensions/filters/http/api_key_auth/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ApiKeyAuth {
namespace {

TEST(ApiKeyAuthFilterFactoryTest, DuplicateApiKey) {
  const std::string yaml = R"(
  credentials:
    entries:
      - api_key: key1
        client_id: user1
      - api_key: key1
        client_id: user2
  authentication_header: "Authorization"
  )";

  ApiKeyAuthProto proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  ApiKeyAuthFilterFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_THROW_WITH_MESSAGE(
      { auto status_or = factory.createFilterFactoryFromProto(proto_config, "stats", context); },
      EnvoyException, "Duplicate API key.");
}

TEST(ApiKeyAuthFilterFactoryTest, NormalFactory) {
  const std::string yaml = R"(
  credentials:
    entries:
      - api_key: key1
        client_id: user1
      - api_key: key2
        client_id: user2
  authentication_header: "Authorization"
  )";

  ApiKeyAuthProto proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  const std::string scope_yaml = R"(
  override_config:
    credentials:
      entries:
        - api_key: key3
          client_id: user3
  allowed_clients:
    - user1
  )";
  ApiKeyAuthPerScopeProto scope_proto_config;
  TestUtility::loadFromYaml(scope_yaml, scope_proto_config);

  ApiKeyAuthFilterFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  auto status_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_TRUE(status_or.ok());

  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  status_or.value()(filter_callback);

  auto scope_config =
      factory.createRouteSpecificFilterConfig(scope_proto_config, context.server_factory_context_,
                                              ProtobufMessage::getNullValidationVisitor());
  EXPECT_TRUE(scope_config != nullptr);
}

} // namespace
} // namespace ApiKeyAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
