#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "contrib/postgres_proxy/filters/network/source/config.h"

using testing::_;
using testing::NiceMock;
using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {
namespace {

TEST(PostgresConfigTest, CreatesFilterWithRules) {
  envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy config;
  config.set_stat_prefix("postgres");
  config.mutable_rules();

  NiceMock<Server::Configuration::MockFactoryContext> context;
  PostgresConfigFactory factory;
  auto rules_result = factory.createFilterFactoryFromProto(config, context);
  ASSERT_TRUE(rules_result.ok());
  StrictMock<Network::MockFilterManager> manager;
  EXPECT_CALL(manager, addFilter(_));
  (*rules_result)(manager);
}

TEST(PostgresConfigTest, CreatesNetworkRules) {
  envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy config;
  TestUtility::loadFromYaml(R"EOF(
stat_prefix: postgres
rules:
  policies:
    network:
      permissions:
      - and_rules:
          rules:
          - or_rules:
              rules:
              - not_rule: {destination_port: 1}
      principals:
      - and_ids:
          ids:
          - or_ids:
              ids:
              - not_id: {direct_remote_ip: {address_prefix: "127.0.0.1", prefix_len: 32}}
)EOF",
                            config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  PostgresConfigFactory factory;
  EXPECT_TRUE(factory.createFilterFactoryFromProto(config, context).ok());
}

} // namespace
} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
