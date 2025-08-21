#include "source/common/router/string_accessor_impl.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SetFilterState {

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
      filters:
      - name: direct_response
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.set_filter_state.v3.Config
          on_new_connection:
          - object_key: foo
            format_string:
              text_format_source:
                inline_string: "bar"
      - name: envoy.filters.network.echo
        typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.echo.v3.Echo
      )EOF");
  }

  void SetUp() override {
    useListenerAccessLog("%FILTER_STATE(foo)%");
    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SetFilterStateIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(SetFilterStateIntegrationTest, OnConnection) {
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(tcp_client->connected());
  tcp_client->close();
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_), testing::HasSubstr("bar"));
}

} // namespace SetFilterState
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
