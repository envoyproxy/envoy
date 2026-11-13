#include <string>
#include <vector>

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "contrib/postgres_proxy/filters/network/source/config.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::ReturnRef;
using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {
namespace {

using namespace std::literals::string_literals;

TEST(PostgresConfigTest, ConfiguredRulesDenyStartup) {
  envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy config;
  TestUtility::loadFromYaml(R"EOF(
stat_prefix: postgres
rules:
  action: ALLOW
  policies: {}
)EOF",
                            config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  PostgresConfigFactory factory;
  auto rules_result = factory.createFilterFactoryFromProto(config, context);
  ASSERT_TRUE(rules_result.ok());
  StrictMock<Network::MockFilterManager> manager;
  Network::FilterSharedPtr configured_filter;
  EXPECT_CALL(manager, addFilter(_)).WillOnce([&](Network::FilterSharedPtr filter) {
    configured_filter = std::move(filter);
  });
  (*rules_result)(manager);

  auto filter = std::dynamic_pointer_cast<PostgresFilter>(configured_filter);
  ASSERT_NE(filter, nullptr);
  NiceMock<Network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<Network::MockWriteFilterCallbacks> write_callbacks;
  NiceMock<Network::MockConnection> connection;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(read_callbacks, connection()).WillRepeatedly(ReturnRef(connection));
  EXPECT_CALL(connection, streamInfo()).WillRepeatedly(ReturnRef(stream_info));
  EXPECT_CALL(stream_info,
              setConnectionTerminationDetails("rbac_access_denied_matched_policy[none]"));
  EXPECT_CALL(write_callbacks, injectWriteDataToFilterChain(_, false))
      .WillOnce(Invoke([](Buffer::Instance& response, bool) {
        EXPECT_EQ(response.peekBEInt<char>(0), 'E');
        EXPECT_THAT(response.toString(), testing::HasSubstr("28000"));
      }));
  EXPECT_CALL(connection, close(Network::ConnectionCloseType::FlushWrite));
  filter->initializeReadFilterCallbacks(read_callbacks);
  filter->initializeWriteFilterCallbacks(write_callbacks);

  const std::string attributes = "user\0postgres\0\0"s;
  Buffer::OwnedImpl startup;
  startup.writeBEInt<uint32_t>(8 + attributes.size());
  startup.writeBEInt<uint32_t>(0x00030000);
  startup.add(attributes);
  EXPECT_EQ(filter->onData(startup, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(filter->getConfig()->stats_.authorization_denied_.value(), 1);
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

TEST(PostgresConfigTest, RejectsHeaderRules) {
  const std::vector<std::string> configs = {
      R"EOF(
stat_prefix: postgres
rules:
  action: DENY
  policies:
    header_permission:
      permissions:
      - header: {name: x-test, string_match: {exact: blocked}}
      principals:
      - any: true
)EOF",
      R"EOF(
stat_prefix: postgres
rules:
  action: DENY
  policies:
    header_principal:
      permissions:
      - any: true
      principals:
      - header: {name: x-test, string_match: {exact: blocked}}
)EOF"};

  NiceMock<Server::Configuration::MockFactoryContext> context;
  PostgresConfigFactory factory;
  for (const auto& yaml : configs) {
    envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy config;
    TestUtility::loadFromYaml(yaml, config);
    const auto create_filter = [&]() {
      const auto result = factory.createFilterFactoryFromProto(config, context);
      (void)result;
    };
    EXPECT_THROW_WITH_REGEX(create_filter(), EnvoyException,
                            "not supported by RBAC network filter");
  }
}

} // namespace
} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
