#include "envoy/extensions/filters/http/connection_pool_cardinality/v3/connection_pool_cardinality.pb.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/connection_pool_cardinality/config.h"

#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectionPoolCardinality {

class ConnectionPoolCardinalityFilterFactoryTest : public testing::Test {
public:
  ConnectionPoolCardinalityFilterFactory factory_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(ConnectionPoolCardinalityFilterFactoryTest, CreateFilterFactory) {
  const std::string config_yaml = R"EOF(
connection_pool_count: 5
)EOF";
  envoy::extensions::filters::http::connection_pool_cardinality::v3::ConnectionPoolCardinalityConfig
      proto_config;
  MessageUtil::loadFromYamlAndValidate(config_yaml, proto_config,
                                       ProtobufMessage::getNullValidationVisitor());

  Http::FilterFactoryCb cb =
      factory_.createFilterFactoryFromProtoTyped(proto_config, "stats", context_);
  EXPECT_TRUE(cb != nullptr);
}

TEST_F(ConnectionPoolCardinalityFilterFactoryTest, CreateFilterFactoryDefaultConfig) {
  const std::string config_yaml = R"EOF(
connection_pool_count: 0
)EOF";

  envoy::extensions::filters::http::connection_pool_cardinality::v3::ConnectionPoolCardinalityConfig
      proto_config;
  MessageUtil::loadFromYamlAndValidate(config_yaml, proto_config,
                                       ProtobufMessage::getNullValidationVisitor());

  Http::FilterFactoryCb cb =
      factory_.createFilterFactoryFromProtoTyped(proto_config, "stats", context_);
  EXPECT_TRUE(cb != nullptr);
}

TEST_F(ConnectionPoolCardinalityFilterFactoryTest, CreateFilterFactoryEmptyConfig) {
  envoy::extensions::filters::http::connection_pool_cardinality::v3::ConnectionPoolCardinalityConfig
      proto_config;

  Http::FilterFactoryCb cb =
      factory_.createFilterFactoryFromProtoTyped(proto_config, "stats", context_);
  EXPECT_TRUE(cb != nullptr);
}

TEST_F(ConnectionPoolCardinalityFilterFactoryTest, Name) {
  EXPECT_EQ("envoy.filters.http.connection_pool_cardinality", factory_.name());
}

} // namespace ConnectionPoolCardinality
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
