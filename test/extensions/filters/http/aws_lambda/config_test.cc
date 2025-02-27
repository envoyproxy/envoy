#include "envoy/extensions/filters/http/aws_lambda/v3/aws_lambda.pb.h"
#include "envoy/extensions/filters/http/aws_lambda/v3/aws_lambda.pb.validate.h"

#include "source/extensions/filters/http/aws_lambda/aws_lambda_filter.h"
#include "source/extensions/filters/http/aws_lambda/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using ::testing::Truly;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsLambdaFilter {
namespace {

using LambdaConfig = envoy::extensions::filters::http::aws_lambda::v3::Config;
using LambdaPerRouteConfig = envoy::extensions::filters::http::aws_lambda::v3::PerRouteConfig;

class AwsLambdaFilterFactoryWrapper : public AwsLambdaFilterFactory {
public:
  using AwsLambdaFilterFactory::getCredentialsProvider;
};

TEST(AwsLambdaFilterConfigTest, ValidConfigCreatesFilter) {
  const std::string yaml = R"EOF(
arn: "arn:aws:lambda:region:424242:function:fun"
payload_passthrough: true
invocation_mode: asynchronous
  )EOF";

  LambdaConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  AwsLambdaFilterFactory factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  auto has_expected_settings = [](std::shared_ptr<Envoy::Http::StreamFilter> stream_filter) {
    auto filter = std::static_pointer_cast<Filter>(stream_filter);
    const auto& settings = filter->settingsForTest();
    return settings.payloadPassthrough() &&
           settings.invocationMode() == InvocationMode::Asynchronous;
  };

  EXPECT_CALL(filter_callbacks, addStreamFilter(Truly(has_expected_settings)));
  cb(filter_callbacks);
}

/**
 * The default for passthrough is false.
 * The default for invocation_mode is Synchronous.
 */
TEST(AwsLambdaFilterConfigTest, ValidConfigVerifyDefaults) {
  const std::string yaml = R"EOF(
arn: "arn:aws:lambda:region:424242:function:fun"
  )EOF";

  LambdaConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  AwsLambdaFilterFactory factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  auto has_expected_settings = [](std::shared_ptr<Envoy::Http::StreamFilter> stream_filter) {
    auto filter = std::static_pointer_cast<Filter>(stream_filter);
    const auto& settings = filter->settingsForTest();
    return settings.payloadPassthrough() == false &&
           settings.invocationMode() == InvocationMode::Synchronous;
  };

  EXPECT_CALL(filter_callbacks, addStreamFilter(Truly(has_expected_settings)));
  cb(filter_callbacks);
}

TEST(AwsLambdaFilterConfigTest, ValidPerRouteConfigCreatesFilter) {
  const std::string yaml = R"EOF(
  invoke_config:
    arn: "arn:aws:lambda:region:424242:function:fun"
    payload_passthrough: true
  )EOF";

  LambdaPerRouteConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  AwsLambdaFilterFactory factory;

  auto route_specific_config_ptr =
      factory
          .createRouteSpecificFilterConfig(proto_config, context,
                                           ProtobufMessage::getStrictValidationVisitor())
          .value();
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  ASSERT_NE(route_specific_config_ptr, nullptr);
  auto filter_settings_ptr =
      std::static_pointer_cast<const FilterSettings>(route_specific_config_ptr);
  EXPECT_TRUE(filter_settings_ptr->payloadPassthrough());
  EXPECT_EQ(InvocationMode::Synchronous, filter_settings_ptr->invocationMode());
}

TEST(AwsLambdaFilterConfigTest, InvalidARN) {
  const std::string yaml = R"EOF(
arn: "arn:aws:lambda:region:424242:fun"
  )EOF";

  LambdaConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  AwsLambdaFilterFactory factory;

  auto status_or = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  EXPECT_FALSE(status_or.ok());
  EXPECT_EQ(status_or.status().message(),
            "aws_lambda_filter: Invalid ARN: arn:aws:lambda:region:424242:fun");
}

TEST(AwsLambdaFilterConfigTest, PerRouteConfigWithInvalidARN) {
  const std::string yaml = R"EOF(
  invoke_config:
    arn: "arn:aws:lambda:region:424242:fun"
    payload_passthrough: true
  )EOF";

  LambdaPerRouteConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  AwsLambdaFilterFactory factory;

  auto status_or = factory.createRouteSpecificFilterConfig(
      proto_config, context, ProtobufMessage::getStrictValidationVisitor());
  EXPECT_FALSE(status_or.ok());
  EXPECT_EQ(status_or.status().message(),
            "aws_lambda_filter: Invalid ARN: arn:aws:lambda:region:424242:fun");
}

TEST(AwsLambdaFilterConfigTest, AsynchrnousPerRouteConfig) {
  const std::string yaml = R"EOF(
  invoke_config:
    arn: "arn:aws:lambda:region:424242:function:fun"
    payload_passthrough: false
    invocation_mode: asynchronous
  )EOF";

  LambdaPerRouteConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  AwsLambdaFilterFactory factory;

  auto route_specific_config_ptr =
      factory
          .createRouteSpecificFilterConfig(proto_config, context,
                                           ProtobufMessage::getStrictValidationVisitor())
          .value();
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  ASSERT_NE(route_specific_config_ptr, nullptr);
  auto filter_settings_ptr =
      std::static_pointer_cast<const FilterSettings>(route_specific_config_ptr);
  EXPECT_FALSE(filter_settings_ptr->payloadPassthrough());
  EXPECT_EQ(InvocationMode::Asynchronous, filter_settings_ptr->invocationMode());
}

TEST(AwsLambdaFilterConfigTest, UpstreamFactoryTest) {

  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::UpstreamHttpFilterConfigFactory>::getFactory(
          "envoy.filters.http.aws_lambda");
  ASSERT_NE(factory, nullptr);
}

TEST(AwsLambdaFilterConfigTest, ValidConfigWithProfileCreatesFilter) {
  const std::string yaml = R"EOF(
arn: "arn:aws:lambda:region:424242:function:fun"
payload_passthrough: true
invocation_mode: asynchronous
credentials_profile: test_profile
  )EOF";

  LambdaConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  AwsLambdaFilterFactory factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  auto has_expected_settings = [](std::shared_ptr<Envoy::Http::StreamFilter> stream_filter) {
    auto filter = std::static_pointer_cast<Filter>(stream_filter);
    const auto& settings = filter->settingsForTest();

    return settings.payloadPassthrough() &&
           settings.invocationMode() == InvocationMode::Asynchronous;
  };
  EXPECT_CALL(filter_callbacks, addStreamFilter(Truly(has_expected_settings)));
  cb(filter_callbacks);
}

TEST(AwsLambdaFilterConfigTest, ValidConfigWithCredentialsCreatesFilter) {
  const std::string yaml = R"EOF(
arn: "arn:aws:lambda:region:424242:function:fun"
payload_passthrough: true
invocation_mode: asynchronous
credentials:
  access_key_id: config_kid
  secret_access_key: config_Key
  session_token: config_token
  )EOF";

  LambdaConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  AwsLambdaFilterFactory factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  auto has_expected_settings = [](std::shared_ptr<Envoy::Http::StreamFilter> stream_filter) {
    auto filter = std::static_pointer_cast<Filter>(stream_filter);
    const auto& settings = filter->settingsForTest();

    return settings.payloadPassthrough() &&
           settings.invocationMode() == InvocationMode::Asynchronous;
  };
  EXPECT_CALL(filter_callbacks, addStreamFilter(Truly(has_expected_settings)));
  cb(filter_callbacks);
}

TEST(AwsLambdaFilterConfigTest, ValidConfigWithCredentialsOptionalSessionTokenCreatesFilter) {
  const std::string yaml = R"EOF(
arn: "arn:aws:lambda:region:424242:function:fun"
payload_passthrough: true
invocation_mode: asynchronous
credentials:
  access_key_id: config_kid
  secret_access_key: config_Key
)EOF";

  LambdaConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  AwsLambdaFilterFactory factory;

  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  auto has_expected_settings = [](std::shared_ptr<Envoy::Http::StreamFilter> stream_filter) {
    auto filter = std::static_pointer_cast<Filter>(stream_filter);
    const auto& settings = filter->settingsForTest();

    return settings.payloadPassthrough() &&
           settings.invocationMode() == InvocationMode::Asynchronous;
  };
  EXPECT_CALL(filter_callbacks, addStreamFilter(Truly(has_expected_settings)));
  cb(filter_callbacks);
}

TEST(AwsLambdaFilterConfigTest, GetProviderShoudPrioritizeCredentialsOverOtherOptions) {
  const std::string yaml = R"EOF(
arn: "arn:aws:lambda:region:424242:function:fun"
payload_passthrough: true
invocation_mode: asynchronous
credentials_profile: test_profile
credentials:
  access_key_id: config_kid
  secret_access_key: config_Key
  session_token: config_token
  )EOF";

  LambdaConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  AwsLambdaFilterFactoryWrapper factory;

  auto chain =
      factory.getCredentialsProvider(proto_config, context.serverFactoryContext(), "region");
  // hardwired credentials are set in provider configuration
  auto credentials = chain->chainGetCredentials();
  EXPECT_EQ(credentials.accessKeyId().value(), "config_kid");
  EXPECT_EQ(credentials.secretAccessKey().value(), "config_Key");
  EXPECT_EQ(credentials.sessionToken().value(), "config_token");
}

TEST(AwsLambdaFilterConfigTest, GetProviderShouldPrioritizeProfileIfNoCredentials) {
  const std::string yaml = R"EOF(
arn: "arn:aws:lambda:region:424242:function:fun"
payload_passthrough: true
invocation_mode: asynchronous
credentials_profile: test_profile
  )EOF";

  LambdaConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  AwsLambdaFilterFactoryWrapper factory;

  auto chain = factory.getCredentialsProvider(proto_config, context_, "region");

  const char CREDENTIALS_FILE_CONTENTS[] =
      R"(
[default]
aws_access_key_id=default_access_key
aws_secret_access_key=default_secret
aws_session_token=default_token

[test_profile]
aws_access_key_id = profile4_access_key
aws_secret_access_key = profile4_secret
aws_session_token = profile4_token
)";
  const char CREDENTIALS_FILE[] = "test-profile";

  auto file_path =
      TestEnvironment::writeStringToFileForTest(CREDENTIALS_FILE, CREDENTIALS_FILE_CONTENTS);
  TestEnvironment::setEnvVar("AWS_SHARED_CREDENTIALS_FILE", file_path, 1);
  EXPECT_CALL(context_.api_.file_system_, fileReadToEnd(file_path))
      .WillRepeatedly(Return(CREDENTIALS_FILE_CONTENTS));

  // test_profile is set in provider configuration
  const auto credentials = chain->chainGetCredentials();
  EXPECT_EQ("profile4_access_key", credentials.accessKeyId().value());
  EXPECT_EQ("profile4_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("profile4_token", credentials.sessionToken().value());
}

TEST(AwsLambdaFilterConfigTest, GetProviderShoudReturnLegacyChainIfNoProfileNorCredentials) {
  const std::string yaml = R"EOF(
arn: "arn:aws:lambda:region:424242:function:fun"
payload_passthrough: true
invocation_mode: asynchronous
  )EOF";

  LambdaConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  AwsLambdaFilterFactoryWrapper factory;

  auto chain = factory.getCredentialsProvider(proto_config, context_, "region");

  const char CREDENTIALS_FILE_CONTENTS[] =
      R"(
[default]
aws_access_key_id=default_access_key
aws_secret_access_key=default_secret
aws_session_token=default_token

[test_profile]
aws_access_key_id = profile4_access_key
aws_secret_access_key = profile4_secret
aws_session_token = profile4_token
)";
  const char CREDENTIALS_FILE[] = "test-profile";

  auto file_path =
      TestEnvironment::writeStringToFileForTest(CREDENTIALS_FILE, CREDENTIALS_FILE_CONTENTS);
  TestEnvironment::setEnvVar("AWS_SHARED_CREDENTIALS_FILE", file_path, 1);
  EXPECT_CALL(context_.api_.file_system_, fileReadToEnd(file_path))
      .WillRepeatedly(Return(CREDENTIALS_FILE_CONTENTS));

  // Default Credentials Provider chain will always return credentials from default profile
  const auto credentials = chain->chainGetCredentials();
  EXPECT_EQ("default_access_key", credentials.accessKeyId().value());
  EXPECT_EQ("default_secret", credentials.secretAccessKey().value());
  EXPECT_EQ("default_token", credentials.sessionToken().value());
}

} // namespace
} // namespace AwsLambdaFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
