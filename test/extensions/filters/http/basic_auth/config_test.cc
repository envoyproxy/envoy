#include "source/extensions/filters/http/basic_auth/basic_auth_filter.h"
#include "source/extensions/filters/http/basic_auth/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BasicAuth {

TEST(Factory, ValidConfig) {
  const std::string yaml = R"(
  users:
    inline_string: |-
        user1:{SHA}tESsBmE/yNY3lb6a0L6vVQEZNqw=
        user2:{SHA}EJ9LPFDXsN9ynSmbxvjp75Bmlx8=
  )";

  BasicAuthFilterFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  auto callback = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  callback(filter_callback);
}

TEST(Factory, InvalidConfigNoColon) {
  const std::string yaml = R"(
  users:
    inline_string: |-
        user1{SHA}tESsBmE/yNY3lb6a0L6vVQEZNqw=
        user2:{SHA}EJ9LPFDXsN9ynSmbxvjp75Bmlx8=
  )";

  BasicAuthFilterFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_THROW(factory.createFilterFactoryFromProto(*proto_config, "stats", context),
               EnvoyException);
}

TEST(Factory, InvalidConfigNoUser) {
  const std::string yaml = R"(
  users:
    inline_string: |-
        :{SHA}tESsBmE/yNY3lb6a0L6vVQEZNqw=
        user2:{SHA}EJ9LPFDXsN9ynSmbxvjp75Bmlx8=
  )";

  BasicAuthFilterFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_THROW(factory.createFilterFactoryFromProto(*proto_config, "stats", context),
               EnvoyException);
}

TEST(Factory, InvalidConfigNoPassword) {
  const std::string yaml = R"(
  users:
    inline_string: |-
        user1:
        user2:{SHA}EJ9LPFDXsN9ynSmbxvjp75Bmlx8=
  )";

  BasicAuthFilterFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_THROW(factory.createFilterFactoryFromProto(*proto_config, "stats", context),
               EnvoyException);
}

TEST(Factory, InvalidConfigNoHash) {
  const std::string yaml = R"(
  users:
    inline_string: |-
        user1:{SHA}
        user2:{SHA}EJ9LPFDXsN9ynSmbxvjp75Bmlx8=
  )";

  BasicAuthFilterFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_THROW(factory.createFilterFactoryFromProto(*proto_config, "stats", context),
               EnvoyException);
}

TEST(Factory, InvalidConfigNotSHA) {
  const std::string yaml = R"(
  users:
    inline_string: |-
        user1:{SHA}tESsBmE/yNY3lb6a0L6vVQEZNqw=
        user2:$apr1$0vAnUTEB$4EJJr0GR3y48WF2AiieWs.
  )";

  BasicAuthFilterFactory factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_THROW(factory.createFilterFactoryFromProto(*proto_config, "stats", context),
               EnvoyException);
}

} // namespace BasicAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
