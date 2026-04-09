#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/listener/set_filter_state/config.h"

#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/utility.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace SetFilterState {
namespace {

class ObjectFooFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return "foo"; }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return std::make_unique<Router::StringAccessorImpl>(data);
  }
};

REGISTER_FACTORY(ObjectFooFactory, StreamInfo::FilterState::ObjectFactory);

class SetFilterStateIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public BaseIntegrationTest {
public:
  SetFilterStateIntegrationTest() : BaseIntegrationTest(GetParam(), config()) {}

  static std::string config() {
    return absl::StrCat(ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
      - filters:
        - name: echo
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.echo.v3.Echo
)EOF");
  }

  void addListenerFilter(const std::string& config) {
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter = listener->add_listener_filters();
      filter->set_name("set_filter_state");
      TestUtility::loadFromYaml(config, *filter->mutable_typed_config());
    });
  }

  void SetUp() override { useListenerAccessLog("%FILTER_STATE(early)%"); }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SetFilterStateIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(SetFilterStateIntegrationTest, OnAccept) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    const std::string metadata_yaml = R"EOF(
      filter_metadata:
        com.test.my_filter:
          my_key: "my_value"
    )EOF";
    TestUtility::loadFromYaml(metadata_yaml, *listener->mutable_metadata());
  });
  const std::string filter_config = R"EOF(
    "@type": type.googleapis.com/envoy.extensions.filters.listener.set_filter_state.v3.Config
    on_accept:
      - object_key: "early"
        factory_key: "foo"
        format_string:
          text_format_source:
            inline_string: "%METADATA(LISTENER:com.test.my_filter:my_key)%"
  )EOF";
  addListenerFilter(filter_config);
  BaseIntegrationTest::initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(tcp_client->connected());
  tcp_client->close();
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_), testing::HasSubstr("my_value"));
}

} // namespace
} // namespace SetFilterState
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
