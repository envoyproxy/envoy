#include <memory>

#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"

#include "contrib/envoy/extensions/filters/network/sip_proxy/v3alpha/sip_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/sip_proxy/v3alpha/sip_proxy.pb.validate.h"
#include "contrib/sip_proxy/filters/network/source/config.h"
#include "contrib/sip_proxy/filters/network/source/filters/factory_base.h"
#include "contrib/sip_proxy/filters/network/test/mocks.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

namespace {

envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy
parseSipProxyFromYaml(const std::string& yaml) {
  envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy sip_proxy;
  TestUtility::loadFromYaml(yaml, sip_proxy);
  return sip_proxy;
}
} // namespace

class SipFilterConfigTestBase {
public:
  void testConfig(envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy& config) {
    Network::FilterFactoryCb cb;
    EXPECT_NO_THROW({ cb = factory_.createFilterFactoryFromProto(config, context_).value(); });
    EXPECT_TRUE(factory_.isTerminalFilterByProto(config, context_.serverFactoryContext()));

    Network::MockConnection connection;
    EXPECT_CALL(connection, addReadFilter(_));
    cb(connection);
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  SipProxyFilterConfigFactory factory_;
};

class SipFilterConfigTest : public testing::Test, public SipFilterConfigTestBase {};

TEST_F(SipFilterConfigTest, ValidateFail) {
  EXPECT_THROW(
      factory_
          .createFilterFactoryFromProto(
              envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy(), context_)
          .IgnoreError(),
      ProtoValidationException);
}

TEST_F(SipFilterConfigTest, ValidProtoConfiguration) {
  envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy config{};
  config.set_stat_prefix("my_stat_prefix");

  testConfig(config);
}

TEST_F(SipFilterConfigTest, SipProxyWithEmptyProto) {
  envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy config =
      *dynamic_cast<envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy*>(
          factory_.createEmptyConfigProto().get());
  config.set_stat_prefix("my_stat_prefix");

  testConfig(config);
}

// Test config with an invalid cluster_header.
TEST_F(SipFilterConfigTest, RouterConfigWithValidCluster) {
  const std::string yaml = R"EOF(
stat_prefix: sip
route_config:
  name: local_route
  routes:
    match:
      domain: A
    route:
      cluster: A
sip_filters:
  - name: envoy.filters.sip.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.sip_proxy.router.v3alpha.Router
)EOF";

  envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy config =
      parseSipProxyFromYaml(yaml);
  std::string cluster = "A";
  config.mutable_route_config()->mutable_routes()->at(0).mutable_route()->set_cluster(cluster);
  EXPECT_NO_THROW({ factory_.createFilterFactoryFromProto(config, context_).IgnoreError(); });
}

// Test config with an explicitly defined router filter.
TEST_F(SipFilterConfigTest, SipProxyWithExplicitRouterConfig) {
  const std::string yaml = R"EOF(
stat_prefix: sip
route_config:
  name: local_route
sip_filters:
  - name: envoy.filters.sip.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.sip_proxy.router.v3alpha.Router
)EOF";

  envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy config =
      parseSipProxyFromYaml(yaml);
  testConfig(config);
}

// Test config with an unknown filter.
TEST_F(SipFilterConfigTest, SipProxyWithUnknownFilter) {
  const std::string yaml = R"EOF(
stat_prefix: sip
route_config:
  name: local_route
sip_filters:
  - name: no_such_filter
  - name: envoy.filters.sip.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.sip_proxy.router.v3alpha.Router
)EOF";

  envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy config =
      parseSipProxyFromYaml(yaml);

  EXPECT_THROW_WITH_REGEX(factory_.createFilterFactoryFromProto(config, context_).IgnoreError(),
                          EnvoyException, "no_such_filter");
}

// Test config with multiple filters.
TEST_F(SipFilterConfigTest, SipProxyWithMultipleFilters) {
  const std::string yaml = R"EOF(
stat_prefix: ingress
route_config:
  name: local_route
sip_filters:
  - name: envoy.filters.sip.mock_filter
    typed_config:
      "@type": type.googleapis.com/google.protobuf.Struct
      value:
        key: value
  - name: envoy.filters.sip.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.sip_proxy.router.v3alpha.Router
settings:
  transaction_timeout: 32s
  local_services:
  - domain: pcsf-cfed.cncs.svc.cluster.local
)EOF";

  SipFilters::MockFilterConfigFactory factory;
  Registry::InjectFactory<SipFilters::NamedSipFilterConfigFactory> registry(factory);

  envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy config =
      parseSipProxyFromYaml(yaml);
  testConfig(config);

  EXPECT_EQ(1, factory.config_struct_.fields_size());
  EXPECT_EQ("value", factory.config_struct_.fields().at("key").string_value());
  EXPECT_EQ("sip.ingress.", factory.config_stat_prefix_);
}

// Test SipProtocolOptions
TEST_F(SipFilterConfigTest, SipProtocolOptions) {
  const std::string yaml = R"EOF(
session_affinity: true
registration_affinity: true
customized_affinity:
  entries:
  - key_name: test
    subscribe: true
    query: true
  - key_name: test1
  stop_load_balance: false
)EOF";

  envoy::extensions::filters::network::sip_proxy::v3alpha::SipProtocolOptions config;
  TestUtility::loadFromYaml(yaml, config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  const auto options = std::make_shared<ProtocolOptionsConfigImpl>(config);
  EXPECT_CALL(
      *context.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
      extensionProtocolOptions(_))
      .WillRepeatedly(Return(options));

  EXPECT_EQ(true, options->sessionAffinity());
  EXPECT_EQ(true, options->registrationAffinity());
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
