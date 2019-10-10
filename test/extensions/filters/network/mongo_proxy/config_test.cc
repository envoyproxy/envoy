#include "envoy/config/filter/network/mongo_proxy/v2/mongo_proxy.pb.h"
#include "envoy/config/filter/network/mongo_proxy/v2/mongo_proxy.pb.validate.h"

#include "extensions/filters/network/mongo_proxy/config.h"

#include "test/mocks/server/mocks.h"

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
                   envoy::config::filter::network::mongo_proxy::v2::MongoProxy(), context),
               ProtoValidationException);
}

TEST(MongoFilterConfigTest, CorrectConfigurationNoFaults) {
  const std::string yaml_string = R"EOF(
  stat_prefix: my_stat_prefix
  access_log: path/to/access/log
  )EOF";

  envoy::config::filter::network::mongo_proxy::v2::MongoProxy proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  MongoProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

TEST(MongoFilterConfigTest, ValidProtoConfigurationNoFaults) {
  envoy::config::filter::network::mongo_proxy::v2::MongoProxy config;

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
  envoy::config::filter::network::mongo_proxy::v2::MongoProxy config =
      *dynamic_cast<envoy::config::filter::network::mongo_proxy::v2::MongoProxy*>(
          factory.createEmptyConfigProto().get());
  config.set_access_log("path/to/access/log");
  config.set_stat_prefix("my_stat_prefix");

  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

void handleInvalidConfiguration(const std::string& yaml_string) {
  envoy::config::filter::network::mongo_proxy::v2::MongoProxy config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml_string, config), EnvoyException);
}

TEST(MongoFilterConfigTest, InvalidExtraProperty) {
  const std::string yaml_string = R"EOF(
  stat_prefix: my_stat_prefix
  access_log: path/to/access/log
  test: a
  )EOF";

  handleInvalidConfiguration(yaml_string);
}

TEST(MongoFilterConfigTest, EmptyConfig) { handleInvalidConfiguration("{}"); }

TEST(MongoFilterConfigTest, InvalidFaultsEmptyConfig) {
  const std::string yaml_string = R"EOF(
  stat_prefix: my_stat_prefix
  fault: {}
  )EOF";

  handleInvalidConfiguration(yaml_string);
}

TEST(MongoFilterConfigTest, InvalidFaultsMissingPercentage) {
  const std::string yaml_string = R"EOF(
  stat_prefix: my_stat_prefix
  fault:
    fixed_delay:
      duration_ms: 1
  )EOF";

  handleInvalidConfiguration(yaml_string);
}

TEST(MongoFilterConfigTest, InvalidFaultsMissingMs) {
  const std::string yaml_string = R"EOF(
  stat_prefix: my_stat_prefix
  fault:
    fixed_delay:
      delay_percent: 1
  )EOF";

  handleInvalidConfiguration(yaml_string);
}

TEST(MongoFilterConfigTest, InvalidFaultsNegativeMs) {
  const std::string yaml_string = R"EOF(
  stat_prefix: my_stat_prefix
  fault:
    fixed_delay:
      percent: 1
      duration: -1s
  )EOF";

  handleInvalidConfiguration(yaml_string);
}

TEST(MongoFilterConfigTest, InvalidFaultsDelayPercent) {
  {
    const std::string yaml_string = R"EOF(
    stat_prefix: my_stat_prefix
    fault:
      fixed_delay:
        percent: 101
        duration: 1s
    )EOF";

    handleInvalidConfiguration(yaml_string);
  }

  {
    const std::string yaml_string = R"EOF(
    stat_prefix: my_stat_prefix
    fault:
      fixed_delay:
        percent: -1
        duration: 1s
    )EOF";

    handleInvalidConfiguration(yaml_string);
  }
}

TEST(MongoFilterConfigTest, InvalidFaultsType) {
  {
    const std::string yaml_string = R"EOF(
    stat_prefix: my_stat_prefix
    fault:
      fixed_delay:
        percent: df
        duration: 1
    )EOF";

    handleInvalidConfiguration(yaml_string);
  }

  {
    const std::string yaml_string = R"EOF(
    stat_prefix: my_stat_prefix
    fault:
      fixed_delay:
        percent: 3
        duration: ab
    )EOF";

    handleInvalidConfiguration(yaml_string);
  }

  {
    const std::string yaml_string = R"EOF(
    stat_prefix: my_stat_prefix
    fault:
      fixed_delay:
        percent: 3
        duration: 0s
    )EOF";

    handleInvalidConfiguration(yaml_string);
  }
}

TEST(MongoFilterConfigTest, CorrectFaultConfiguration) {
  const std::string yaml_string = R"EOF(
  stat_prefix: my_stat_prefix
  fault:
    fixed_delay:
      percent: 1
      duration: 1s
  )EOF";

  envoy::config::filter::network::mongo_proxy::v2::MongoProxy proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  MongoProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

TEST(MongoFilterConfigTest, CorrectFaultConfigurationInProto) {
  envoy::config::filter::network::mongo_proxy::v2::MongoProxy config{};
  config.set_stat_prefix("my_stat_prefix");
  config.mutable_delay()->mutable_percentage()->set_numerator(50);
  config.mutable_delay()->mutable_percentage()->set_denominator(
      envoy::type::FractionalPercent::HUNDRED);
  config.mutable_delay()->mutable_fixed_delay()->set_seconds(500);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  MongoProxyFilterConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

} // namespace MongoProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
