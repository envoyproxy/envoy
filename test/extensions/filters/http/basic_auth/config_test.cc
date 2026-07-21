#include "envoy/extensions/filters/http/basic_auth/v3/basic_auth.pb.h"

#include "source/extensions/filters/http/basic_auth/basic_auth_filter.h"
#include "source/extensions/filters/http/basic_auth/config.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BasicAuth {

using envoy::extensions::filters::http::basic_auth::v3::BasicAuth;
using ::Envoy::StatusHelpers::HasStatusMessage;

TEST(Factory, ValidConfig) {
  const std::string yaml = R"(
  users:
    inline_string: |-
        # comment line
        user1:{SHA}tESsBmE/yNY3lb6a0L6vVQEZNqw=
        user2:{SHA}EJ9LPFDXsN9ynSmbxvjp75Bmlx8=
  )";

  BasicAuthFilterFactory factory;
  BasicAuth proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  auto callback = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  callback.value()(filter_callback);
}

TEST(Factory, InvalidConfigNoColon) {
  const std::string yaml = R"(
  users:
    inline_string: |-
        user1{SHA}tESsBmE/yNY3lb6a0L6vVQEZNqw=
        user2:{SHA}EJ9LPFDXsN9ynSmbxvjp75Bmlx8=
  )";

  BasicAuthFilterFactory factory;
  BasicAuth proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  auto status_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_THAT(status_or, HasStatusMessage(
                             "basic auth: invalid htpasswd format, username:password is expected"));
}

TEST(Factory, InvalidConfigDuplicateUsers) {
  const std::string yaml = R"(
  users:
    inline_string: |-
        user1:{SHA}tESsBmE/yNY3lb6a0L6vVQEZNqw=
        user1:{SHA}EJ9LPFDXsN9ynSmbxvjp75Bmlx8=
  )";

  BasicAuthFilterFactory factory;
  BasicAuth proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  auto status_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_THAT(status_or, HasStatusMessage("basic auth: duplicate users"));
}

TEST(Factory, InvalidConfigNoUser) {
  const std::string yaml = R"(
  users:
    inline_string: |-
        :{SHA}tESsBmE/yNY3lb6a0L6vVQEZNqw=
        user2:{SHA}EJ9LPFDXsN9ynSmbxvjp75Bmlx8=
  )";

  BasicAuthFilterFactory factory;
  BasicAuth proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  auto status_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_THAT(status_or, HasStatusMessage("basic auth: empty user name or password"));
}

TEST(Factory, InvalidConfigNoPassword) {
  const std::string yaml = R"(
  users:
    inline_string: |-
        user1:
        user2:{SHA}EJ9LPFDXsN9ynSmbxvjp75Bmlx8=
  )";

  BasicAuthFilterFactory factory;
  BasicAuth proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  auto status_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_THAT(status_or, HasStatusMessage("basic auth: empty user name or password"));
}

TEST(Factory, InvalidConfigNoHash) {
  const std::string yaml = R"(
  users:
    inline_string: |-
        user1:{SHA}
        user2:{SHA}EJ9LPFDXsN9ynSmbxvjp75Bmlx8=
  )";

  BasicAuthFilterFactory factory;
  BasicAuth proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  auto status_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_THAT(status_or,
              HasStatusMessage("basic auth: invalid htpasswd format, invalid SHA hash length"));
}

TEST(Factory, InvalidConfigNotSHA) {
  const std::string yaml = R"(
  users:
    inline_string: |-
        user1:{SHA}tESsBmE/yNY3lb6a0L6vVQEZNqw=
        user2:$apr1$0vAnUTEB$4EJJr0GR3y48WF2AiieWs.
  )";

  BasicAuthFilterFactory factory;
  BasicAuth proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  auto status_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_THAT(status_or,
              HasStatusMessage("basic auth: unsupported htpasswd format: please use {SHA}"));
}

TEST(Factory, ValidConfigWithServerContext) {
  const std::string yaml = R"(
  users:
    inline_string: |-
        user1:{SHA}tESsBmE/yNY3lb6a0L6vVQEZNqw=
        user2:{SHA}EJ9LPFDXsN9ynSmbxvjp75Bmlx8=
  )";

  BasicAuthFilterFactory factory;
  BasicAuth proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto callback = factory.createHttpFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  callback(filter_callback);
}

} // namespace BasicAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
