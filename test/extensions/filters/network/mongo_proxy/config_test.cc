#include <string>

#include "envoy/extensions/filters/network/mongo_proxy/v3/mongo_proxy.pb.h"
#include "envoy/extensions/filters/network/mongo_proxy/v3/mongo_proxy.pb.validate.h"
#include "envoy/type/v3/percent.pb.h"

#include "extensions/filters/network/mongo_proxy/config.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MongoProxy {

TEST(MongoFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(MongoProxyFilterConfigFactory().createFilterFactoryFromProto(
                   envoy::extensions::filters::network::mongo_proxy::v3::MongoProxy(), context),
               ProtoValidationException);
}

TEST(MongoFilterConfigTest, CorrectConfigurationNoFaults) {
  const std::string yaml_string = R"EOF(
  stat_prefix: my_stat_prefix
  access_log: path/to/access/log
  )EOF";

  envoy::extensions::filters::network::mongo_proxy::v3::MongoProxy proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  MongoProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

TEST(MongoFilterConfigTest, ValidProtoConfigurationNoFaults) {
  envoy::extensions::filters::network::mongo_proxy::v3::MongoProxy config;

  config.set_access_log("path/to/access/log");
  config.set_stat_prefix("my_stat_prefix");

  NiceMock<Server::Configuration::MockFactoryContext> context;
  MongoProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

TEST(MongoFilterConfigTest, MongoFilterWithEmptyProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  MongoProxyFilterConfigFactory factory;
  envoy::extensions::filters::network::mongo_proxy::v3::MongoProxy config =
      *dynamic_cast<envoy::extensions::filters::network::mongo_proxy::v3::MongoProxy*>(
          factory.createEmptyConfigProto().get());
  config.set_access_log("path/to/access/log");
  config.set_stat_prefix("my_stat_prefix");

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

void handleInvalidConfiguration(const std::string& yaml_string, const std::string& error_regex) {
  envoy::extensions::filters::network::mongo_proxy::v3::MongoProxy config;
  EXPECT_THROW_WITH_REGEX(TestUtility::loadFromYamlAndValidate(yaml_string, config), EnvoyException,
                          error_regex);
}

TEST(MongoFilterConfigTest, InvalidExtraProperty) {
  const std::string yaml_string = R"EOF(
  stat_prefix: my_stat_prefix
  access_log: path/to/access/log
  test: a
  )EOF";

  handleInvalidConfiguration(yaml_string, "test: Cannot find field");
}

TEST(MongoFilterConfigTest, EmptyConfig) {
  handleInvalidConfiguration(
      "{}", R"(StatPrefix: \["value length must be at least " '\\x01' " bytes"\])");
}

TEST(MongoFilterConfigTest, InvalidFaultsEmptyConfig) {
  const std::string yaml_string = R"EOF(
  stat_prefix: my_stat_prefix
  delay: {}
  )EOF";

  handleInvalidConfiguration(yaml_string,
                             R"(caused by field: "fault_delay_secifier", reason: is required)");
}

TEST(MongoFilterConfigTest, InvalidFaultsMissingFixedDelayTime) {
  const std::string yaml_string = R"EOF(
  stat_prefix: my_stat_prefix
  delay:
    percentage:
      numerator: 1
      denominator: HUNDRED
  )EOF";

  handleInvalidConfiguration(yaml_string,
                             R"(caused by field: "fault_delay_secifier", reason: is required)");
}

TEST(MongoFilterConfigTest, InvalidFaultsNegativeMs) {
  const std::string yaml_string = R"EOF(
  stat_prefix: my_stat_prefix
  delay:
    percentage:
      numerator: 1
      denominator: HUNDRED
    fixed_delay: -1s
  )EOF";

  handleInvalidConfiguration(yaml_string, R"(FixedDelay: \["value must be greater than " "0s"\])");
}

TEST(MongoFilterConfigTest, InvalidFaultsDelayPercent) {
  {
    const std::string yaml_string = R"EOF(
    stat_prefix: my_stat_prefix
    delay:
      percentage:
        numerator: -1
        denominator: HUNDRED
      fixed_delay: 1s
    )EOF";

    handleInvalidConfiguration(yaml_string, R"(invalid value -1 for type TYPE_UINT32)");
  }
}

TEST(MongoFilterConfigTest, InvalidFaultsType) {
  {
    const std::string yaml_string = R"EOF(
    stat_prefix: my_stat_prefix
    delay:
      percentage:
        numerator: df
        denominator: HUNDRED
      fixed_delay: 1s
    )EOF";

    handleInvalidConfiguration(yaml_string, R"(invalid value "df" for type TYPE_UINT32)");
  }

  {
    const std::string yaml_string = R"EOF(
    stat_prefix: my_stat_prefix
    delay:
      percentage:
        numerator: 1
        denominator: HUNDRED
      fixed_delay: ab
    )EOF";

    handleInvalidConfiguration(yaml_string, "Illegal duration format; duration must end with 's'");
  }

  {
    const std::string yaml_string = R"EOF(
    stat_prefix: my_stat_prefix
    delay:
      percentage:
        numerator: 3
        denominator: HUNDRED
      fixed_delay: 0s
    )EOF";

    handleInvalidConfiguration(yaml_string,
                               R"(FixedDelay: \["value must be greater than " "0s"\])");
  }
}

TEST(MongoFilterConfigTest, CorrectFaultConfiguration) {
  const std::string yaml_string = R"EOF(
  stat_prefix: my_stat_prefix
  delay:
    percentage:
      numerator: 1
      denominator: HUNDRED
    fixed_delay: 0.001s
  )EOF";

  envoy::extensions::filters::network::mongo_proxy::v3::MongoProxy proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  MongoProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

TEST(MongoFilterConfigTest, CorrectFaultConfigurationInProto) {
  envoy::extensions::filters::network::mongo_proxy::v3::MongoProxy config{};
  config.set_stat_prefix("my_stat_prefix");
  config.mutable_delay()->mutable_percentage()->set_numerator(50);
  config.mutable_delay()->mutable_percentage()->set_denominator(
      envoy::type::v3::FractionalPercent::HUNDRED);
  config.mutable_delay()->mutable_fixed_delay()->set_seconds(500);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  MongoProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

// Test that the deprecated extension name still functions.
TEST(MongoFilterConfigTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.mongo_proxy";

  ASSERT_NE(
      nullptr,
      Registry::FactoryRegistry<Server::Configuration::NamedNetworkFilterConfigFactory>::getFactory(
          deprecated_name));
}

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
