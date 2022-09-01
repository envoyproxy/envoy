#include <memory>
#include <string>
#include <utility>

#include "source/extensions/filters/network/meta_protocol_proxy/proxy.h"

#include "test/extensions/filters/network/meta_protocol_proxy/fake_codec.h"
#include "test/extensions/filters/network/meta_protocol_proxy/mocks/codec.h"
#include "test/extensions/filters/network/meta_protocol_proxy/mocks/filter.h"
#include "test/extensions/filters/network/meta_protocol_proxy/mocks/route.h"
#include "test/integration/base_integration_test.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {
namespace {

class IntegrationTest : public testing::TestWithParam<Network::Address::IpVersion> {
public:
  void initialize(const std::string& config_yaml) {
    integration_ = std::make_unique<BaseIntegrationTest>(GetParam(), config_yaml);
    integration_->initialize();
  }

  std::string defaultConfig() {
    return absl::StrCat(ConfigHelper::baseConfig(false), R"EOF(
    filter_chains:
      filters:
        name: meta
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.meta_protocol_proxy.v3.MetaProtocolProxy
          stat_prefix: config_test
          filters:
          - name: envoy.filters.meta.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.meta_protocol_proxy.filters.router.v3.Router
          codec:
            name: fake
            typed_config:
              "@type": type.googleapis.com/xds.type.v3.TypedStruct
              type_url: envoy.meta_protocol_proxy.codec.fake.type
              value: {}
          route_config:
            name: test-routes
            routes:
              matcher_tree:
                input:
                  name: request-service
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.filters.network.meta_protocol_proxy.matcher.v3.ServiceMatchInput
                exact_match_map:
                  map:
                    service_name_0:
                      matcher:
                        matcher_list:
                          matchers:
                          - predicate:
                              single_predicate:
                                input:
                                  name: request-properties
                                  typed_config:
                                    "@type": type.googleapis.com/envoy.extensions.filters.network.meta_protocol_proxy.matcher.v3.PropertyMatchInput
                                    property_name: version
                                value_match:
                                  exact: v1
                            on_match:
                              action:
                                name: route
                                typed_config:
                                  "@type": type.googleapis.com/envoy.extensions.filters.network.meta_protocol_proxy.matcher.action.v3.RouteAction
                                  cluster: cluster_0
)EOF");
  }

  std::unique_ptr<BaseIntegrationTest> integration_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, IntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(IntegrationTest, InitializeInstance) { initialize(defaultConfig()); }

} // namespace
} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
