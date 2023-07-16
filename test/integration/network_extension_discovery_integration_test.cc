#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/extension/v3/config_discovery.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/filters/test_network_filter.pb.h"
#include "test/integration/integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

constexpr absl::string_view EcdsClusterName = "ecds_cluster";
constexpr absl::string_view Ecds2ClusterName = "ecds2_cluster";

class NetworkExtensionDiscoveryIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                                  public BaseIntegrationTest {
public:
  NetworkExtensionDiscoveryIntegrationTest()
      : BaseIntegrationTest(ipVersion(), ConfigHelper::baseConfig()), filter_name_("foo"),
        data_("HelloWorld"), port_name_("tcp") {
    skip_tag_extraction_rule_check_ = true;
  }

  void addFilterChain() {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      listener->set_stat_prefix("listener_stat");
      listener->add_filter_chains();
    });
  }

  void addDynamicFilter(const std::string& name, bool apply_without_warming,
                        bool set_default_config = true, bool rate_limit = false,
                        bool second_connection = false) {
    config_helper_.addConfigModifier([name, apply_without_warming, set_default_config, rate_limit,
                                      second_connection,
                                      this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      listener->set_stat_prefix("listener_stat");

      auto* filter_chain = listener->mutable_filter_chains(0);
      auto* filter = filter_chain->add_filters();
      filter->set_name(name);

      auto* discovery = filter->mutable_config_discovery();
      discovery->add_type_urls(
          "type.googleapis.com/test.integration.filters.TestTrimmerNetworkFilterConfig");
      if (set_default_config) {
        auto default_configuration = test::integration::filters::TestTrimmerNetworkFilterConfig();
        discovery->mutable_default_config()->PackFrom(default_configuration);
      }

      discovery->set_apply_default_config_without_warming(apply_without_warming);
      discovery->mutable_config_source()->set_resource_api_version(
          envoy::config::core::v3::ApiVersion::V3);
      auto* api_config_source = discovery->mutable_config_source()->mutable_api_config_source();
      api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
      api_config_source->set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
      if (rate_limit) {
        api_config_source->mutable_rate_limit_settings()->mutable_max_tokens()->set_value(10);
      }
      auto* grpc_service = api_config_source->add_grpc_services();
      if (!second_connection) {
        setGrpcService(*grpc_service, std::string(EcdsClusterName),
                       getEcdsFakeUpstream().localAddress());
      } else {
        setGrpcService(*grpc_service, std::string(Ecds2ClusterName),
                       getEcds2FakeUpstream().localAddress());
      }
    });
  }

  void addEcdsCluster(const std::string& cluster_name) {
    // Add an xDS cluster for extension config discovery.
    config_helper_.addConfigModifier(
        [cluster_name](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* ecds_cluster = bootstrap.mutable_static_resources()->add_clusters();
          ecds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
          ecds_cluster->set_name(cluster_name);
          ConfigHelper::setHttp2(*ecds_cluster);
        });
  }

  void initialize() override {
    defer_listener_finalization_ = true;
    setUpstreamCount(1);

    // Add a tcp_proxy network filter.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->mutable_filter_chains(0);
      auto* filter = filter_chain->add_filters();
      filter->set_name("envoy.filters.network.tcp_proxy");
      envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config;
      config.set_stat_prefix("tcp_stats");
      config.set_cluster("cluster_0");
      filter->mutable_typed_config()->PackFrom(config);
    });

    addEcdsCluster(std::string(EcdsClusterName));
    // Add 2nd cluster in case of two connections.
    if (two_connections_) {
      addEcdsCluster(std::string(Ecds2ClusterName));
    }
    BaseIntegrationTest::initialize();
    registerTestServerPorts({port_name_});
  }

  void resetEcdsConnection(FakeHttpConnectionPtr& connection) {
    if (connection != nullptr) {
      AssertionResult result = connection->close();
      RELEASE_ASSERT(result, result.message());
      result = connection->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      connection.reset();
    }
  }

  ~NetworkExtensionDiscoveryIntegrationTest() override {
    resetEcdsConnection(ecds_connection_);
    resetEcdsConnection(ecds2_connection_);
  }

  void createUpstreams() override {
    BaseIntegrationTest::createUpstreams();
    // Create two extension config discovery upstreams (fake_upstreams_[1] and fake_upstreams_[2]).
    addFakeUpstream(Http::CodecType::HTTP2);
    if (two_connections_) {
      addFakeUpstream(Http::CodecType::HTTP2);
    }
  }

  void waitForEcdsStream(FakeUpstream& upstream, FakeHttpConnectionPtr& connection,
                         FakeStreamPtr& stream) {
    AssertionResult result = upstream.waitForHttpConnection(*dispatcher_, connection);
    ASSERT_TRUE(result);
    result = connection->waitForNewStream(*dispatcher_, stream);
    ASSERT_TRUE(result);
    stream->startGrpcStream();
  }

  void waitXdsStream() {
    waitForEcdsStream(getEcdsFakeUpstream(), ecds_connection_, ecds_stream_);
    if (two_connections_) {
      // Wait for 2nd ECDS stream.
      waitForEcdsStream(getEcds2FakeUpstream(), ecds2_connection_, ecds2_stream_);
    }
  }

  void sendXdsResponse(const std::string& name, const std::string& version, 
                       uint32_t bytes_to_trim, bool ttl = false,
                       bool second_connection = false) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url("type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig");
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name(name);
    envoy::service::discovery::v3::Resource resource;
    resource.set_name(name);

    auto configuration = test::integration::filters::TestTrimmerNetworkFilterConfig();
    configuration.set_bytes_to_trim(bytes_to_trim);
    typed_config.mutable_typed_config()->PackFrom(configuration);
    resource.mutable_resource()->PackFrom(typed_config);
    if (ttl) {
      resource.mutable_ttl()->set_seconds(1);
    }
    response.add_resources()->PackFrom(resource);
    if (!second_connection) {
      ecds_stream_->sendGrpcMessage(response);
    } else {
      ecds2_stream_->sendGrpcMessage(response);
    }
  }

  void sendDataVerifyResults(uint32_t bytes_trimmed) {
    test_server_->waitUntilListenersReady();
    test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
    EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort(port_name_));
    ASSERT_TRUE(tcp_client->write(data_));
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
    std::string received_data;
    ASSERT_TRUE(fake_upstream_connection->waitForData(data_.size() - bytes_trimmed, &received_data));
    const std::string expected_data = data_.substr(bytes_trimmed, std::string::npos);
    EXPECT_EQ(expected_data, received_data);
    tcp_client->close();
  }

  const std::string filter_name_;
  const std::string data_;
  const std::string port_name_;
  bool two_connections_{false};

  FakeUpstream& getEcdsFakeUpstream() const { return *fake_upstreams_[1]; }
  FakeUpstream& getEcds2FakeUpstream() const { return *fake_upstreams_[2]; }

  // gRPC two ECDS connections set-up.
  FakeHttpConnectionPtr ecds_connection_{nullptr};
  FakeStreamPtr ecds_stream_{nullptr};
  FakeHttpConnectionPtr ecds2_connection_{nullptr};
  FakeStreamPtr ecds2_stream_{nullptr};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, NetworkExtensionDiscoveryIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(NetworkExtensionDiscoveryIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addFilterChain();
  addDynamicFilter(filter_name_, false);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update to have filter trim 1 byte of data.
  sendXdsResponse(filter_name_, "1", 1);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(1);

  // Send 2nd config update to have filter trim 2 bytes of data.
  sendXdsResponse(filter_name_, "2", 2);
  test_server_->waitForCounterGe(
      "extension_config_discovery.network_filter." + filter_name_ + ".config_reload", 2);
  sendDataVerifyResults(2);
}

} // namespace
} // namespace Envoy
