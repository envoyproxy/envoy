#include "envoy/extensions/filters/network/proxy_protocol/v3/proxy_protocol.pb.h"

#include "extensions/filters/common/proxy_protocol/proxy_protocol_header.h"

#include "test/integration/integration.h"

using envoy::extensions::filters::network::proxy_protocol::v3::ProxyProtocol_Version;

namespace Envoy {
namespace {

class ProxyProtocolIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public BaseIntegrationTest {
public:
  ProxyProtocolIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::TCP_PROXY_CONFIG) {}

  ~ProxyProtocolIntegrationTest() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void initialize() override {
    enable_half_close_ = true;
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
      auto* filter = cluster_0->add_filters();
      filter->set_name("envoy.filters.network.proxy_protocol");
      envoy::extensions::filters::network::proxy_protocol::v3::ProxyProtocol config{};
      config.set_version(version_);
      filter->mutable_typed_config()->PackFrom(config);
    });
    BaseIntegrationTest::initialize();
  }

  void setVersion(ProxyProtocol_Version version) { version_ = version; }

private:
  ProxyProtocol_Version version_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyProtocolIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ProxyProtocolIntegrationTest, TestV1ProxyProtocol) {
  setVersion(ProxyProtocol_Version::ProxyProtocol_Version_V1);
  initialize();

  auto listener_port = lookupPort("listener_0");
  auto tcp_client = makeTcpConnection(listener_port);
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  std::string observed_data;
  tcp_client->write("data");
  if (GetParam() == Envoy::Network::Address::IpVersion::v4) {
    ASSERT_TRUE(fake_upstream_connection->waitForData(48, &observed_data));
    std::ostringstream stream;
    stream << "PROXY TCP4 127\\.0\\.0\\.1 127\\.0\\.0\\.1 [0-9]{1,5} " << listener_port
           << "\r\ndata";
    EXPECT_THAT(observed_data, testing::MatchesRegex(stream.str()));
  } else if (GetParam() == Envoy::Network::Address::IpVersion::v6) {
    ASSERT_TRUE(fake_upstream_connection->waitForData(36, &observed_data));
    std::ostringstream stream;
    stream << "PROXY TCP6 ::1 ::1 [0-9]{1,5} " << listener_port << "\r\ndata";
    EXPECT_THAT(observed_data, testing::MatchesRegex(stream.str()));
  }

  auto previous_data = observed_data;
  observed_data.clear();
  tcp_client->write(" more data");
  ASSERT_TRUE(fake_upstream_connection->waitForData(previous_data.length() + 10, &observed_data));
  EXPECT_EQ(previous_data + " more data", observed_data);

  tcp_client->close();
  auto _ = fake_upstream_connection->waitForDisconnect();
}

TEST_P(ProxyProtocolIntegrationTest, TestV2ProxyProtocol) {
  setVersion(ProxyProtocol_Version::ProxyProtocol_Version_V2);
  initialize();

  auto listener_port = lookupPort("listener_0");
  auto tcp_client = makeTcpConnection(listener_port);
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  std::string observed_data;
  tcp_client->write("data");
  if (GetParam() == Envoy::Network::Address::IpVersion::v4) {
    ASSERT_TRUE(fake_upstream_connection->waitForData(32, &observed_data));
    // - signature
    // - version and command type, address family and protocol, length of addresses
    // - src address, dest address
    auto header_start = "\x0d\x0a\x0d\x0a\x00\x0d\x0a\x51\x55\x49\x54\x0a\
                         \x21\x11\x00\x0c\
                         \x7f\x00\x00\x01\x7f\x00\x00\x01";
    EXPECT_THAT(observed_data, testing::StartsWith(header_start));
    EXPECT_EQ(static_cast<uint8_t>(observed_data[26]), listener_port >> 8);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[27]), listener_port & 0xFF);
    EXPECT_THAT(observed_data, testing::EndsWith("data"));
  } else if (GetParam() == Envoy::Network::Address::IpVersion::v6) {
    ASSERT_TRUE(fake_upstream_connection->waitForData(56, &observed_data));
    // - signature
    // - version and command type, address family and protocol, length of addresses
    // - src address
    // - dest address
    auto header_start = "\x0d\x0a\x0d\x0a\x00\x0d\x0a\x51\x55\x49\x54\x0a\
                         \x21\x21\x00\x24\
                         \x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\
                         \x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01";
    EXPECT_THAT(observed_data, testing::StartsWith(header_start));
    EXPECT_EQ(static_cast<uint8_t>(observed_data[50]), listener_port >> 8);
    EXPECT_EQ(static_cast<uint8_t>(observed_data[51]), listener_port & 0xFF);
    EXPECT_THAT(observed_data, testing::EndsWith("data"));
  }

  auto previous_data = observed_data;
  observed_data.clear();
  tcp_client->write(" more data");
  ASSERT_TRUE(fake_upstream_connection->waitForData(previous_data.length() + 10, &observed_data));
  EXPECT_EQ(previous_data + " more data", observed_data);

  tcp_client->close();
  auto _ = fake_upstream_connection->waitForDisconnect();
}

} // namespace
} // namespace Envoy
