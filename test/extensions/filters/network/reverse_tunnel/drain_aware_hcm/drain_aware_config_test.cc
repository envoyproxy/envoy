#include "source/extensions/filters/network/reverse_tunnel/drain_aware_hcm/drain_aware_config.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {
namespace {

// Minimal valid DrainAwareHttpConnectionManager proto YAML.
// The router filter must be registered (via the BUILD dep on
// //source/extensions/filters/http/router:config).
constexpr absl::string_view kMinimalConfig = R"EOF(
hcm_config:
  stat_prefix: test
  route_config:
    virtual_hosts:
    - name: local
      domains: ["*"]
      routes:
      - match:
          prefix: "/"
        direct_response:
          status: 200
  http_filters:
  - name: envoy.filters.http.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
)EOF";

class DrainAwareConfigTest : public testing::Test {
protected:
  DrainAwareConfigTest() {
    ON_CALL(context_, listenerInfo()).WillByDefault(testing::ReturnRef(listener_info_));
  }

  NiceMock<Network::MockListenerInfo> listener_info_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;

  envoy::extensions::filters::network::reverse_tunnel::v3::DrainAwareHttpConnectionManager
  parseConfig(absl::string_view yaml) {
    envoy::extensions::filters::network::reverse_tunnel::v3::DrainAwareHttpConnectionManager proto;
    TestUtility::loadFromYaml(std::string(yaml), proto);
    return proto;
  }
};

TEST_F(DrainAwareConfigTest, FactoryName) {
  DrainAwareHttpConnectionManagerFilterConfigFactory factory;
  EXPECT_EQ("envoy.filters.network.reverse_tunnel_drain_aware_http_connection_manager",
            factory.name());
}

TEST_F(DrainAwareConfigTest, CreateEmptyConfigProto) {
  DrainAwareHttpConnectionManagerFilterConfigFactory factory;
  auto proto = factory.createEmptyConfigProto();
  ASSERT_NE(nullptr, proto);
  EXPECT_EQ("envoy.extensions.filters.network.reverse_tunnel.v3.DrainAwareHttpConnectionManager",
            proto->GetTypeName());
}

TEST_F(DrainAwareConfigTest, CreateFilterFactoryFromValidConfig) {
  DrainAwareHttpConnectionManagerFilterConfigFactory factory;
  auto proto_config = parseConfig(kMinimalConfig);
  auto result = factory.createFilterFactoryFromProto(proto_config, context_);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_NE(nullptr, result.value());
}

TEST_F(DrainAwareConfigTest, FilterFactoryCallbackIsNonNull) {
  DrainAwareHttpConnectionManagerFilterConfigFactory factory;
  auto proto_config = parseConfig(kMinimalConfig);
  auto result = factory.createFilterFactoryFromProto(proto_config, context_);
  ASSERT_TRUE(result.ok());
  // Verify a callable callback was produced. The actual ConnectionManagerImpl
  // installation path is exercised end-to-end in integration_test.cc.
  EXPECT_NE(nullptr, result.value());
}

} // namespace
} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
