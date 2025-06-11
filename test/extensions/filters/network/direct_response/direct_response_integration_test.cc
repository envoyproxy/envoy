#include "envoy/extensions/filters/network/direct_response/v3/config.pb.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/test_common/utility.h"

namespace Envoy {

class DirectResponseIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public BaseIntegrationTest {
public:
  DirectResponseIntegrationTest() : BaseIntegrationTest(GetParam(), ConfigHelper::baseConfig()) {}

  void init(bool keep_open_after_response = false) {
    useListenerAccessLog("%RESPONSE_CODE_DETAILS%");

    const std::string yaml_config = fmt::format(R"EOF(
        response:
          inline_string: "hello, world!\n"
        keep_open_after_response: {0}
    )EOF",
                                                keep_open_after_response ? "true" : "false");

    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      envoy::extensions::filters::network::direct_response::v3::Config config;
      TestUtility::loadFromYaml(yaml_config, config);

      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->add_filter_chains();
      auto* filter = filter_chain->add_filters();
      filter->set_name("direct_response");
      filter->mutable_typed_config()->PackFrom(config);
    });

    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DirectResponseIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DirectResponseIntegrationTest, DirectResponseOnConnection) {
  init();

  std::string response;
  // This test becomes flaky (especially on Windows) if the connection is closed by the server
  // before the client finishes transmitting the data it writes (resulting in a connection aborted
  // error when the client reads). Instead, we just initiate the connection and do not send from
  // the client to avoid this.
  auto connection = createConnectionDriver(
      lookupPort("listener_0"), "",
      [&response](Network::ClientConnection& conn, const Buffer::Instance& data) -> void {
        response.append(data.toString());
        conn.close(Network::ConnectionCloseType::FlushWrite);
      });
  ASSERT_TRUE(connection->run());
  EXPECT_EQ("hello, world!\n", response);
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_),
              testing::HasSubstr(StreamInfo::ResponseCodeDetails::get().DirectResponse));
}

TEST_P(DirectResponseIntegrationTest, DirectResponseKeepOpenOnConnection) {
  init(true);
  std::string expected_response = "hello, world!\n";

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  EXPECT_TRUE(tcp_client->write("ping", false));
  EXPECT_TRUE(tcp_client->waitForData(expected_response.length()));
  EXPECT_EQ(expected_response, tcp_client->data());

  EXPECT_TRUE(tcp_client->connected());
  EXPECT_TRUE(tcp_client->write("ping2", false));
  tcp_client->close();
}

} // namespace Envoy
