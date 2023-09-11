#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/udp/udp_proxy/session_filters/http_capsule/config.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/http_capsule/http_capsule.h"

#include "test/integration/integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace HttpCapsule {
namespace {

class HttpCapsuleIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public BaseIntegrationTest {
public:
  HttpCapsuleIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseUdpListenerConfig()) {}

  void setup() {
    setUdpFakeUpstream(FakeUpstreamConfig::UdpConfig());

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      std::string filter_config = fmt::format(R"EOF(
name: udp_proxy
typed_config:
  '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.UdpProxyConfig
  stat_prefix: foo
  matcher:
    on_no_match:
      action:
        name: route
        typed_config:
          '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
          cluster: cluster_0
  session_filters:
  - name: http_capsule
    typed_config:
      '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.session.http_capsule.v3.FilterConfig
)EOF");

      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter = listener->add_listener_filters();
      TestUtility::loadFromYaml(filter_config, *filter);
    });

    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, HttpCapsuleIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(HttpCapsuleIntegrationTest, BasicFlow) {
  setup();
}

} // namespace
} // namespace HttpCapsule
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
