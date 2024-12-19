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
  - key: key1
    client: user1
  - key: key1
    client: user2
  key_sources:
  - header: "Authorization
  )";

  ApiKeyAuthProto proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  ApiKeyAuthFilterFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  auto status_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);

  EXPECT_FALSE(status_or.ok());
  EXPECT_EQ("Duplicated credential key: 'key1'", status_or.status().message());
}

TEST(ApiKeyAuthFilterFactoryTest, EmptyKeySource) {
  const std::string yaml = R"(
  credentials:
  - key: key1
    client: user1
  - key: key2
    client: user2
  key_sources:
  - header: ""
  )";

  ApiKeyAuthProto proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  ApiKeyAuthFilterFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  auto status_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);

  EXPECT_FALSE(status_or.ok());
  EXPECT_EQ("One of 'header'/'query'/'cookie' must be set.", status_or.status().message());
}

TEST(ApiKeyAuthFilterFactoryTest, NormalFactory) {
  const std::string yaml = R"(
  credentials:
  - key: key1
    client: user1
  - key: key2
    client: user2
  key_sources:
  - header: "Authorization"
  )";

  ApiKeyAuthProto proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  const std::string scope_yaml = R"(
  credentials:
  - key: key3
    client: user3
  allowed_clients:
  - user1
  )";
  ApiKeyAuthPerRouteProto scope_proto_config;
  TestUtility::loadFromYaml(scope_yaml, scope_proto_config);

  ApiKeyAuthFilterFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  auto status_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_TRUE(status_or.ok());

  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  status_or.value()(filter_callback);

  const auto route_config =
      factory
          .createRouteSpecificFilterConfig(scope_proto_config, context.server_factory_context_,
                                           ProtobufMessage::getNullValidationVisitor())
          .value();
  EXPECT_TRUE(route_config != nullptr);
}

} // namespace
} // namespace ApiKeyAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
