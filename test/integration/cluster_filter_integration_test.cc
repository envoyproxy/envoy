#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/network/filter.h"

#include "test/config/utility.h"
#include "test/integration/integration.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class PoliteFilter : public Network::Filter, Logger::Loggable<Logger::Id::filter> {
public:
  PoliteFilter(const ProtobufWkt::StringValue& value) : greeting_(value.value()) {}

  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override {
    ENVOY_CONN_LOG(debug, "polite: onData {} bytes {} end_stream", read_callbacks_->connection(),
                   data.length(), end_stream);
    if (!read_greeted_ && on_new_connection_called_ == 1) {
      Buffer::OwnedImpl greeter(greeting_);
      read_callbacks_->injectReadDataToFilterChain(greeter, false);
      read_greeted_ = true;
    }
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override {
    ENVOY_CONN_LOG(debug, "polite: onWrite {} bytes {} end_stream", write_callbacks_->connection(),
                   data.length(), end_stream);
    if (!write_greeted_ && on_new_connection_called_ == 1) {
      Buffer::OwnedImpl greeter("please ");
      write_callbacks_->injectWriteDataToFilterChain(greeter, false);
      write_greeted_ = true;
    }
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onNewConnection() override {
    ENVOY_CONN_LOG(debug, "polite: new connection", read_callbacks_->connection());
    on_new_connection_called_++;
    return Network::FilterStatus::Continue;
  }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

private:
  const std::string greeting_;
  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};
  bool read_greeted_{false};
  bool write_greeted_{false};
  int on_new_connection_called_{0};
};

class PoliteFilterConfigFactory
    : public Server::Configuration::NamedUpstreamNetworkFilterConfigFactory {
public:
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::CommonFactoryContext&) override {
    auto config = dynamic_cast<const ProtobufWkt::StringValue&>(proto_config);
    return [config](Network::FilterManager& filter_manager) -> void {
      filter_manager.addFilter(std::make_shared<PoliteFilter>(config));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }

  std::string name() const override { return "envoy.upstream.polite"; }
};

class ClusterFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public BaseIntegrationTest {
public:
  ClusterFilterIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()), registration_(factory_) {}

  void initialize() override {
    enableHalfClose(true);
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
      auto* filter = cluster_0->add_filters();
      filter->set_name("envoy.upstream.polite");
      ProtobufWkt::StringValue config;
      config.set_value("surely ");
      filter->mutable_typed_config()->PackFrom(config);
    });
    BaseIntegrationTest::initialize();
  }

  PoliteFilterConfigFactory factory_;
  Registry::InjectFactory<Server::Configuration::NamedUpstreamNetworkFilterConfigFactory>
      registration_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClusterFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ClusterFilterIntegrationTest, TestClusterFilter) {
  initialize();

  auto tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Upstream read filters are now expected to be initialized, so that each upstream read filter's
  // onNewConnection() is called exactly once. Otherwise "please " greeting will not be prepended to
  // the written data and fake_upstream_connection->waitForData() fails. This is dependent on the
  // fake_upstream_connection having not written yet, as then read filter's onNewConnection() will
  // be called before onData(), if not called already before.

  std::string observed_data;
  ASSERT_TRUE(tcp_client->write("test"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(11, &observed_data));
  EXPECT_EQ("please test", observed_data);

  observed_data.clear();
  ASSERT_TRUE(tcp_client->write(" everything"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(22, &observed_data));
  EXPECT_EQ("please test everything", observed_data);

  ASSERT_TRUE(fake_upstream_connection->write("yes"));
  tcp_client->waitForData("surely yes");

  ASSERT_TRUE(tcp_client->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->write("", true));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->waitForDisconnect();
}

} // namespace
} // namespace Envoy
