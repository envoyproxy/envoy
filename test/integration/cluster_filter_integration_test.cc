#include "envoy/network/filter.h"
#include "envoy/registry/registry.h"

#include "test/config/utility.h"
#include "test/integration/integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class PoliteFilter : public Network::Filter, Logger::Loggable<Logger::Id::filter> {
public:
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override {
    ENVOY_CONN_LOG(debug, "polite: onData {} bytes {} end_stream", read_callbacks_->connection(),
                   data.length(), end_stream);
    if (!read_greeted_) {
      Buffer::OwnedImpl greeter("surely ");
      read_callbacks_->injectReadDataToFilterChain(greeter, false);
      read_greeted_ = true;
    }
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override {
    ENVOY_CONN_LOG(debug, "polite: onWrite {} bytes {} end_stream", write_callbacks_->connection(),
                   data.length(), end_stream);
    if (!write_greeted_) {
      Buffer::OwnedImpl greeter("please ");
      write_callbacks_->injectWriteDataToFilterChain(greeter, false);
      write_greeted_ = true;
    }
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onNewConnection() override {
    ENVOY_CONN_LOG(debug, "polite: new connection", read_callbacks_->connection());
    return Network::FilterStatus::Continue;
  }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};
  bool read_greeted_{false};
  bool write_greeted_{false};
};

class PoliteFilterConfigFactory
    : public Server::Configuration::NamedUpstreamNetworkFilterConfigFactory {
public:
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::CommonFactoryContext&) override {
    return [](Network::FilterManager& filter_manager) -> void {
      filter_manager.addFilter(std::make_shared<PoliteFilter>());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Empty()};
  }

  std::string name() override { return "envoy.upstream.polite"; }
};

// perform static registration
REGISTER_FACTORY(PoliteFilterConfigFactory,
                 Server::Configuration::NamedUpstreamNetworkFilterConfigFactory);

class ClusterFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public BaseIntegrationTest {
public:
  ClusterFilterIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::TCP_PROXY_CONFIG) {}

  void initialize() override {
    enable_half_close_ = true;
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
      auto* filter = cluster_0->add_filters();
      filter->set_name("envoy.upstream.polite");
    });
    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClusterFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ClusterFilterIntegrationTest, TestClusterFilter) {
  initialize();

  auto tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  std::string observed_data;
  tcp_client->write("test");
  ASSERT_TRUE(fake_upstream_connection->waitForData(11, &observed_data));
  EXPECT_EQ("please test", observed_data);

  observed_data.clear();
  tcp_client->write(" everything");
  ASSERT_TRUE(fake_upstream_connection->waitForData(22, &observed_data));
  EXPECT_EQ("please test everything", observed_data);

  ASSERT_TRUE(fake_upstream_connection->write("yes"));
  tcp_client->waitForData("surely yes");

  tcp_client->write("", true);
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect(true));
  tcp_client->waitForDisconnect();
}

} // namespace
} // namespace Envoy
