#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/extension/v3/config_discovery.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/upstream/utility.h"
#include "test/integration/filters/test_listener_filter.h"
#include "test/integration/filters/test_listener_filter.pb.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

enum class ListenerMatcherType { NULLMATCHER, ANYMATCHER, NOTANYMATCHER };

constexpr absl::string_view EcdsClusterName = "ecds_cluster";
constexpr absl::string_view Ecds2ClusterName = "ecds2_cluster";

class ListenerExtensionDiscoveryIntegrationTestBase : public Grpc::GrpcClientIntegrationParamTest,
                                                      public HttpIntegrationTest {
public:
  ListenerExtensionDiscoveryIntegrationTestBase(Http::CodecType downstream_type,
                                                const std::string& config,
                                                const std::string& listener_filter_name)
      : HttpIntegrationTest(downstream_type, ipVersion(), config),
        filter_name_(listener_filter_name), port_name_("http") {
    // TODO(ggreenway): add tag extraction rules.
    // Missing stat tag-extraction rule for stat
    // 'extension_config_discovery.tcp_listener_filter.foo.grpc.ecds_cluster.streams_closed_7' and
    // stat_prefix 'ecds_cluster'.
    skip_tag_extraction_rule_check_ = true;
  }

  void addDynamicFilterWithType(const std::string& filter_name,
                                const std::string& filter_config_type_url,
                                bool apply_without_warming,
                                std::shared_ptr<Protobuf::Message> default_config,
                                bool rate_limit = false,
                                ListenerMatcherType matcher = ListenerMatcherType::NULLMATCHER,
                                bool second_connection = false) {
    config_helper_.addConfigModifier([filter_name, apply_without_warming, filter_config_type_url,
                                      default_config, rate_limit, matcher, second_connection,
                                      this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      listener->set_stat_prefix("listener_stat");
      auto* listener_filter = listener->add_listener_filters();

      listener_filter->set_name(filter_name);

      auto* discovery = listener_filter->mutable_config_discovery();
      discovery->add_type_urls(filter_config_type_url);
      if (default_config != nullptr) {
        discovery->mutable_default_config()->PackFrom(*default_config);
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
      addListenerFilterMatcher(listener_filter, matcher);
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

  void addStaticFilter(const std::string& name, uint32_t drain_bytes,
                       ListenerMatcherType matcher = ListenerMatcherType::NULLMATCHER) {
    config_helper_.addConfigModifier(
        [name, drain_bytes, matcher, this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* listener_filter =
              bootstrap.mutable_static_resources()->mutable_listeners(0)->add_listener_filters();
          listener_filter->set_name(name);
          auto configuration = test::integration::filters::TestTcpListenerFilterConfig();
          configuration.set_drain_bytes(drain_bytes);
          listener_filter->mutable_typed_config()->PackFrom(configuration);
          addListenerFilterMatcher(listener_filter, matcher);
        });
  }

  // Add listener filter matcher config.
  void addListenerFilterMatcher(envoy::config::listener::v3::ListenerFilter* listener_filter,
                                ListenerMatcherType matcher) {
    if (matcher == ListenerMatcherType::ANYMATCHER) {
      listener_filter->mutable_filter_disabled()->set_any_match(true);
    } else if (matcher == ListenerMatcherType::NOTANYMATCHER) {
      listener_filter->mutable_filter_disabled()->mutable_not_match()->set_any_match(true);
    }
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

    addEcdsCluster(std::string(EcdsClusterName));
    // Add 2nd cluster in case of two connections.
    if (two_connections_) {
      addEcdsCluster(std::string(Ecds2ClusterName));
    }
    HttpIntegrationTest::initialize();
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

  ~ListenerExtensionDiscoveryIntegrationTestBase() override {
    resetEcdsConnection(ecds_connection_);
    resetEcdsConnection(ecds2_connection_);
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
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

  void sendXdsResponseWithTypedConfig(const std::string& name, const std::string& version,
                                      Protobuf::Message& filter_config, bool ttl = false,
                                      bool second_connection = false) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    response.set_type_url("type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig");
    envoy::config::core::v3::TypedExtensionConfig typed_config;
    typed_config.set_name(name);
    envoy::service::discovery::v3::Resource resource;
    resource.set_name(name);

    typed_config.mutable_typed_config()->PackFrom(filter_config);
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

  // Utilities used for config dump request.
  absl::string_view request(const std::string port_key, const std::string method,
                            const std::string endpoint, BufferingStreamDecoderPtr& response) {
    response = IntegrationUtil::makeSingleRequest(lookupPort(port_key), method, endpoint, "",
                                                  Http::CodecType::HTTP1, version_);
    EXPECT_TRUE(response->complete());
    return response->headers().getStatusValue();
  }

  absl::string_view contentType(const BufferingStreamDecoderPtr& response) {
    const Http::HeaderEntry* entry = response->headers().ContentType();
    if (entry == nullptr) {
      return "(null)";
    }
    return entry->value().getStringView();
  }

  const std::string filter_name_;
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

class ListenerExtensionDiscoveryIntegrationTest
    : public ListenerExtensionDiscoveryIntegrationTestBase {
public:
  static constexpr absl::string_view expected_types[] = {
      "type.googleapis.com/envoy.admin.v3.BootstrapConfigDump",
      "type.googleapis.com/envoy.admin.v3.ClustersConfigDump",
      "type.googleapis.com/envoy.admin.v3.EcdsConfigDump",
      "type.googleapis.com/envoy.admin.v3.ListenersConfigDump",
      "type.googleapis.com/envoy.admin.v3.SecretsConfigDump"};

  ListenerExtensionDiscoveryIntegrationTest()
      : ListenerExtensionDiscoveryIntegrationTestBase(Http::CodecType::HTTP1,
                                                      config_helper_.baseConfig(), "foo"),
        data_("HelloWorld") {}

  void initialize() override {
    // Add a tcp_proxy network filter.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->add_filter_chains();
      auto* filter = filter_chain->add_filters();
      filter->set_name("envoy.filters.network.tcp_proxy");
      envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config;
      config.set_stat_prefix("tcp_stats");
      config.set_cluster("cluster_0");
      filter->mutable_typed_config()->PackFrom(config);
    });

    ListenerExtensionDiscoveryIntegrationTestBase::initialize();
  }

  // Overloaded with test TCP filter.
  void addDynamicFilter(const std::string& filter_name, bool apply_without_warming,
                        bool set_default_config = true, bool rate_limit = false,
                        ListenerMatcherType matcher = ListenerMatcherType::NULLMATCHER,
                        bool second_connection = false) {
    auto default_configuration =
        std::make_shared<test::integration::filters::TestTcpListenerFilterConfig>();
    default_configuration->set_drain_bytes(default_drain_bytes_);
    addDynamicFilterWithType(
        filter_name, "type.googleapis.com/test.integration.filters.TestTcpListenerFilterConfig",
        apply_without_warming, set_default_config ? std::move(default_configuration) : nullptr,
        rate_limit, matcher, second_connection);
  }

  // Client sends data_, which is drained by Envoy listener filter based on config, then received by
  // upstream.
  void sendDataVerifyResults(uint32_t drain_bytes) {
    test_server_->waitUntilListenersReady();
    test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
    EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort(port_name_));
    ASSERT_TRUE(tcp_client->write(data_));
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
    std::string received_data;
    ASSERT_TRUE(fake_upstream_connection->waitForData(data_.size() - drain_bytes, &received_data));
    const std::string expected_data = data_.substr(drain_bytes, std::string::npos);
    EXPECT_EQ(expected_data, received_data);
    tcp_client->close();
  }

  // Verify ECDS config dump data.
  bool
  verifyConfigDumpData(envoy::config::core::v3::TypedExtensionConfig filter_config,
                       test::integration::filters::TestTcpListenerFilterConfig listener_config) {
    // There is no ordering. i.e, either foo or bar could be the 1st in the config dump.
    if (filter_config.name() == "foo") {
      EXPECT_EQ(3, listener_config.drain_bytes());
      return true;
    } else if (filter_config.name() == "bar") {
      EXPECT_EQ(4, listener_config.drain_bytes());
      return true;
    } else {
      return false;
    }
  }

  void sendXdsResponse(const std::string& name, const std::string& version, size_t drain_bytes,
                       bool ttl = false, bool second_connection = false) {
    // The to-be-drained bytes has to be smaller than data size.
    ASSERT(drain_bytes <= data_.size());

    auto configuration = test::integration::filters::TestTcpListenerFilterConfig();
    configuration.set_drain_bytes(drain_bytes);
    sendXdsResponseWithTypedConfig(name, version, configuration, ttl, second_connection);
  }

protected:
  const uint32_t default_drain_bytes_{2};
  const std::string data_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, ListenerExtensionDiscoveryIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update to have listener filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(5);

  // Send 2nd config update to have listener filter drain 3 bytes of data.
  sendXdsResponse(filter_name_, "2", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 2);
  sendDataVerifyResults(3);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicSuccessWithAnyMatcher) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  // Add dynamic filter with any matcher, which effectively disables the filter.
  addDynamicFilter(filter_name_, false, true, false, ListenerMatcherType::ANYMATCHER);
  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send config update to have listener filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  // Send data, verify the listener filter doesn't drain any data.
  sendDataVerifyResults(0);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicSuccessWithNotAnyMatcher) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  // Not any matcher negates the any matcher, which effectively enable the filter.
  addDynamicFilter(filter_name_, false, true, false, ListenerMatcherType::NOTANYMATCHER);
  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send config update to have listener filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  // Send data, verify the listener filter drains five bytes data.
  sendDataVerifyResults(5);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicSuccessWithTtl) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false, false);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update with TTL 1s, and have listener filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5, true);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(5);

  // Wait for configuration expired. Then start a TCP connection.
  // The missing config listener filter will be installed to handle the connection.
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 2);
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort(port_name_));
  auto result = tcp_client->write(data_);
  if (result) {
    tcp_client->waitForDisconnect();
  }
  // The extension_config_missing stats counter increases by 1.
  test_server_->waitForCounterGe("listener.listener_stat.extension_config_missing", 1);

  // Send the data again. The extension_config_missing stats counter increases to 2.
  tcp_client = makeTcpConnection(lookupPort(port_name_));
  result = tcp_client->write(data_);
  if (result) {
    tcp_client->waitForDisconnect();
  }
  test_server_->waitForCounterGe("listener.listener_stat.extension_config_missing", 2);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicSuccessWithTtlWithDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false, true);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update with TTL 1s, and have listener filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5, true);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(5);

  // Wait for configuration expired. The default filter will be installed.
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 2);
  // Start a TCP connection. The default filter drain 2 bytes.
  sendDataVerifyResults(default_drain_bytes_);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicFailWithDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false, true);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send config update with invalid config (drain_bytes has to >=2).
  sendXdsResponse(filter_name_, "1", 1);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_fail", 1);
  // The default filter will be installed. Start a TCP connection. The default filter drain 2 bytes.
  sendDataVerifyResults(default_drain_bytes_);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicFailWithoutDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false, false);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send config update with invalid config (drain_bytes has to >=2).
  sendXdsResponse(filter_name_, "1", 1);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_fail", 1);
  // The missing config filter will be installed and close the connection when a correction is
  // created.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort(port_name_));
  auto result = tcp_client->write(data_);
  if (result) {
    tcp_client->waitForDisconnect();
  }
  // The extension_config_missing stats counter increases by 1.
  test_server_->waitForCounterGe("listener.listener_stat.extension_config_missing", 1);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicWithoutWarming) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  initialize();

  // Send data without send config update.
  sendDataVerifyResults(default_drain_bytes_);
  // Send update should cause a different response.
  sendXdsResponse(filter_name_, "1", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  sendDataVerifyResults(3);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicWithoutWarmingConfigFail) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  initialize();

  // Send data without send config update.
  sendDataVerifyResults(default_drain_bytes_);
  // Send config update with invalid config (drain_bytes has to >=2).
  sendXdsResponse(filter_name_, "1", 1);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_fail", 1);
  sendDataVerifyResults(default_drain_bytes_);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, TwoSubscriptionsSameName) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, true);
  addDynamicFilter(filter_name_, false);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  sendXdsResponse(filter_name_, "1", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  // Each filter drain 3 bytes.
  sendDataVerifyResults(6);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, TwoSubscriptionsDifferentName) {
  two_connections_ = true;
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", true);
  addDynamicFilter("bar", false, true, false, ListenerMatcherType::NULLMATCHER, true);
  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update.
  sendXdsResponse("foo", "1", 3);
  sendXdsResponse("bar", "1", 4, false, true);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter.foo.config_reload",
                                 1);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter.bar.config_reload",
                                 1);
  // The two filters drain 3 + 4  bytes.
  sendDataVerifyResults(7);

  // Send 2nd config update.
  sendXdsResponse("foo", "2", 4);
  sendXdsResponse("bar", "2", 5, false, true);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter.foo.config_reload",
                                 2);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter.bar.config_reload",
                                 2);
  // The two filters drain 4 + 5  bytes.
  sendDataVerifyResults(9);
}

// Testing it works with two static listener filter configuration.
TEST_P(ListenerExtensionDiscoveryIntegrationTest, TwoStaticFilters) {
  addStaticFilter("foo", 3);
  addStaticFilter("bar", 5);
  initialize();

  // filter drain 3+5 bytes.
  sendDataVerifyResults(8);
}

// Static listener filter configuration with matcher config.
TEST_P(ListenerExtensionDiscoveryIntegrationTest, TwoStaticFiltersWithMatcher) {
  addStaticFilter("foo", 3, ListenerMatcherType::ANYMATCHER);
  addStaticFilter("bar", 5, ListenerMatcherType::NOTANYMATCHER);
  initialize();

  // The filter with any matcher configured is disabled.
  sendDataVerifyResults(5);
}

// Testing it works with mixed static/dynamic listener filter configuration.
TEST_P(ListenerExtensionDiscoveryIntegrationTest, TwoDynamicTwoStaticFilterMixed) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false);
  addStaticFilter("bar", 2);
  addDynamicFilter(filter_name_, true);
  addStaticFilter("foobar", 2);
  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse(filter_name_, "1", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  // filter drain 3 + 2 + 3  + 2 bytes.
  sendDataVerifyResults(10);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, DynamicStaticFilterMixedDifferentOrder) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addStaticFilter("bar", 2);
  addStaticFilter("baz", 2);
  addDynamicFilter(filter_name_, true);
  addDynamicFilter(filter_name_, false);
  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse(filter_name_, "1", 2);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  // filter drain 2 + 2 + 2 + 2 bytes.
  sendDataVerifyResults(8);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, DynamicStaticFilterMixedWithMatcher) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addStaticFilter("bar", 2, ListenerMatcherType::NOTANYMATCHER);
  addStaticFilter("baz", 2, ListenerMatcherType::ANYMATCHER);
  addDynamicFilter(filter_name_, true, true, false, ListenerMatcherType::NOTANYMATCHER);
  addDynamicFilter(filter_name_, false, true, false, ListenerMatcherType::ANYMATCHER);
  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse(filter_name_, "1", 3);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  // The filters with any matcher configured are disabled. They totally drain 2 + 3 bytes.
  sendDataVerifyResults(5);
}

TEST_P(ListenerExtensionDiscoveryIntegrationTest, DestroyDuringInit) {
  // If rate limiting is enabled on the config source, gRPC mux drainage updates the requests
  // queue size on destruction. The update calls out to stats scope nested under the extension
  // config subscription stats scope. This test verifies that the stats scope outlasts the gRPC
  // subscription.
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false, true);
  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  test_server_.reset();
  auto result = ecds_connection_->waitForDisconnect();
  ASSERT_TRUE(result);
  ecds_connection_.reset();
}

// Basic ECDS config dump test with one filter.
TEST_P(ListenerExtensionDiscoveryIntegrationTest, BasicSuccessWithConfigDump) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin interface.
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update to have listener filter drain 5 bytes of data.
  sendXdsResponse(filter_name_, "1", 5);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);

  // Verify ECDS config dump are working correctly.
  BufferingStreamDecoderPtr response;
  EXPECT_EQ("200", request("admin", "GET", "/config_dump", response));
  EXPECT_EQ("application/json", contentType(response));
  Json::ObjectSharedPtr json = Json::Factory::loadFromString(response->body());
  size_t index = 0;
  for (const Json::ObjectSharedPtr& obj_ptr : json->getObjectArray("configs")) {
    EXPECT_TRUE(expected_types[index].compare(obj_ptr->getString("@type")) == 0);
    index++;
  }

  // Validate we can parse as proto.
  envoy::admin::v3::ConfigDump config_dump;
  TestUtility::loadFromJson(response->body(), config_dump);
  EXPECT_EQ(5, config_dump.configs_size());

  // With /config_dump, the response has the format: EcdsConfigDump.
  envoy::admin::v3::EcdsConfigDump ecds_config_dump;
  config_dump.configs(2).UnpackTo(&ecds_config_dump);
  EXPECT_EQ("1", ecds_config_dump.ecds_filters(0).version_info());
  envoy::config::core::v3::TypedExtensionConfig filter_config;
  EXPECT_TRUE(ecds_config_dump.ecds_filters(0).ecds_filter().UnpackTo(&filter_config));
  EXPECT_EQ("foo", filter_config.name());
  test::integration::filters::TestTcpListenerFilterConfig listener_config;
  filter_config.typed_config().UnpackTo(&listener_config);
  EXPECT_EQ(5, listener_config.drain_bytes());
}

// ECDS config dump test with the filter configuration being removed by TTL expired.
TEST_P(ListenerExtensionDiscoveryIntegrationTest, ConfigDumpWithFilterConfigRemovedByTtl) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin interface.
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(filter_name_, false, false);
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);
  // Send config update with TTL 1s.
  sendXdsResponse(filter_name_, "1", 5, true);
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 1);
  // Wait for configuration expired.
  test_server_->waitForCounterGe(
      "extension_config_discovery.tcp_listener_filter." + filter_name_ + ".config_reload", 2);

  BufferingStreamDecoderPtr response;
  EXPECT_EQ("200", request("admin", "GET", "/config_dump?resource=ecds_filters", response));
  envoy::admin::v3::ConfigDump config_dump;
  TestUtility::loadFromJson(response->body(), config_dump);
  // With /config_dump?resource=ecds_filters, the response has the format: EcdsFilterConfig.
  envoy::admin::v3::EcdsConfigDump::EcdsFilterConfig ecds_msg;
  config_dump.configs(0).UnpackTo(&ecds_msg);
  EXPECT_EQ("", ecds_msg.version_info());
  envoy::config::core::v3::TypedExtensionConfig filter_config;
  EXPECT_TRUE(ecds_msg.ecds_filter().UnpackTo(&filter_config));
  EXPECT_EQ("foo", filter_config.name());
  // Verify ECDS config dump doesn't have the filter configuration.
  EXPECT_EQ(false, filter_config.has_typed_config());
}

// ECDS config dump test with two filters.
TEST_P(ListenerExtensionDiscoveryIntegrationTest, TwoSubscriptionsSameFilterTypeWithConfigDump) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin interface.
  two_connections_ = true;
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", true);
  addDynamicFilter("bar", false, true, false, ListenerMatcherType::NULLMATCHER, true);
  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse("foo", "1", 3);
  sendXdsResponse("bar", "1", 4, false, true);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter.foo.config_reload",
                                 1);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter.bar.config_reload",
                                 1);
  // Verify ECDS config dump are working correctly.
  BufferingStreamDecoderPtr response;
  EXPECT_EQ("200", request("admin", "GET", "/config_dump", response));
  EXPECT_EQ("application/json", contentType(response));
  Json::ObjectSharedPtr json = Json::Factory::loadFromString(response->body());
  size_t index = 0;
  for (const Json::ObjectSharedPtr& obj_ptr : json->getObjectArray("configs")) {
    EXPECT_TRUE(expected_types[index].compare(obj_ptr->getString("@type")) == 0);
    index++;
  }

  envoy::admin::v3::ConfigDump config_dump;
  TestUtility::loadFromJson(response->body(), config_dump);
  EXPECT_EQ(5, config_dump.configs_size());
  envoy::admin::v3::EcdsConfigDump ecds_config_dump;
  config_dump.configs(2).UnpackTo(&ecds_config_dump);
  envoy::config::core::v3::TypedExtensionConfig filter_config;
  test::integration::filters::TestTcpListenerFilterConfig listener_config;
  // Verify the first filter.
  EXPECT_EQ("1", ecds_config_dump.ecds_filters(0).version_info());
  EXPECT_TRUE(ecds_config_dump.ecds_filters(0).ecds_filter().UnpackTo(&filter_config));
  filter_config.typed_config().UnpackTo(&listener_config);
  EXPECT_TRUE(verifyConfigDumpData(filter_config, listener_config));
  // Verify the second filter.
  EXPECT_EQ("1", ecds_config_dump.ecds_filters(1).version_info());
  EXPECT_TRUE(ecds_config_dump.ecds_filters(1).ecds_filter().UnpackTo(&filter_config));
  filter_config.typed_config().UnpackTo(&listener_config);
  EXPECT_TRUE(verifyConfigDumpData(filter_config, listener_config));
}

// ECDS config dump test with specified resource and regex name search.
TEST_P(ListenerExtensionDiscoveryIntegrationTest, TwoSubscriptionsConfigDumpWithResourceAndRegex) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin interface.
  two_connections_ = true;
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter("foo", true);
  addDynamicFilter("bar", false, true, false, ListenerMatcherType::NULLMATCHER, true);
  initialize();
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  sendXdsResponse("foo", "1", 3);
  sendXdsResponse("bar", "1", 4, false, true);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter.foo.config_reload",
                                 1);
  test_server_->waitForCounterGe("extension_config_discovery.tcp_listener_filter.bar.config_reload",
                                 1);
  BufferingStreamDecoderPtr response;
  EXPECT_EQ("200",
            request("admin", "GET", "/config_dump?resource=ecds_filters&name_regex=.a.", response));

  envoy::admin::v3::ConfigDump config_dump;
  TestUtility::loadFromJson(response->body(), config_dump);
  EXPECT_EQ(1, config_dump.configs_size());
  envoy::admin::v3::EcdsConfigDump::EcdsFilterConfig ecds_msg;
  config_dump.configs(0).UnpackTo(&ecds_msg);
  EXPECT_EQ("1", ecds_msg.version_info());
  envoy::config::core::v3::TypedExtensionConfig filter_config;
  EXPECT_TRUE(ecds_msg.ecds_filter().UnpackTo(&filter_config));
  EXPECT_EQ("bar", filter_config.name());
  test::integration::filters::TestTcpListenerFilterConfig listener_config;
  filter_config.typed_config().UnpackTo(&listener_config);
  EXPECT_EQ(4, listener_config.drain_bytes());
}

} // namespace
} // namespace Envoy

#ifdef ENVOY_ENABLE_QUIC

#include "quiche/quic/core/deterministic_connection_id_generator.h"
#include "source/common/quic/client_connection_factory_impl.h"
#include "source/common/quic/quic_transport_socket_factory.h"
#include "test/integration/utility.h"

namespace Envoy {
namespace {

class QuicListenerExtensionDiscoveryIntegrationTest
    : public ListenerExtensionDiscoveryIntegrationTestBase {
public:
  static constexpr absl::string_view expected_types[] = {
      "type.googleapis.com/envoy.admin.v3.BootstrapConfigDump",
      "type.googleapis.com/envoy.admin.v3.ClustersConfigDump",
      "type.googleapis.com/envoy.admin.v3.EcdsConfigDump",
      "type.googleapis.com/envoy.admin.v3.ListenersConfigDump",
      "type.googleapis.com/envoy.admin.v3.ScopedRoutesConfigDump",
      "type.googleapis.com/envoy.admin.v3.RoutesConfigDump",
      "type.googleapis.com/envoy.admin.v3.SecretsConfigDump"};

  QuicListenerExtensionDiscoveryIntegrationTest()
      : ListenerExtensionDiscoveryIntegrationTestBase(Http::CodecType::HTTP3,
                                                      ConfigHelper::quicHttpProxyConfig(), "foo") {}

  // Overloaded with test QUIC filter.
  void addDynamicFilter(bool apply_without_warming, bool set_default_config = true,
                        bool rate_limit = false,
                        ListenerMatcherType matcher = ListenerMatcherType::NULLMATCHER,
                        bool second_connection = false) {
    auto default_configuration =
        std::make_shared<test::integration::filters::TestQuicListenerFilterConfig>();
    default_configuration->set_added_value(default_added_value_);
    addDynamicFilterWithType(
        filter_name_, "type.googleapis.com/test.integration.filters.TestQuicListenerFilterConfig",
        apply_without_warming, set_default_config ? std::move(default_configuration) : nullptr,
        rate_limit, matcher, second_connection);
  }

  void sendXdsResponse(const std::string& name, const std::string& version,
                       const std::string& filter_state_value, bool allow_server_migration = false,
                       bool allow_client_migration = false, bool ttl = false,
                       bool second_connection = false) {
    auto configuration = test::integration::filters::TestQuicListenerFilterConfig();
    configuration.set_added_value(filter_state_value);
    configuration.set_allow_server_migration(allow_server_migration);
    configuration.set_allow_client_migration(allow_client_migration);
    sendXdsResponseWithTypedConfig(name, version, configuration, ttl, second_connection);
  }

protected:
  std::string default_added_value_{"xyz"};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, QuicListenerExtensionDiscoveryIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(QuicListenerExtensionDiscoveryIntegrationTest, EcdsBasicSuccess) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(/*apply_without_warming=*/false);
  useAccessLog(fmt::format("%RESPONSE_CODE% %FILTER_STATE({})%",
                           TestQuicListenerFilter::TestStringFilterState::key()));
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update to have listener filter add "abc" in filter state.
  sendXdsResponse(filter_name_, "v1", "abc");
  test_server_->waitForCounterGe(
      "extension_config_discovery.quic_listener_filter." + filter_name_ + ".config_reload", 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

  testRouterHeaderOnlyRequestAndResponse();
  std::string log = waitForAccessLog(access_log_name_, 0);
  EXPECT_THAT(log, testing::HasSubstr("200 \"abc\""));

  // Change to a new port, and connection should fail.
  Network::Address::InstanceConstSharedPtr local_addr =
      Network::Test::getCanonicalLoopbackAddress(version_);
  dynamic_cast<Quic::EnvoyQuicClientConnection*>(
      dynamic_cast<Quic::EnvoyQuicClientSession&>(codec_client_->rawConnection()).connection())
      ->switchConnectionSocket(Quic::createConnectionSocket(
          codec_client_->rawConnection().connectionInfoProvider().remoteAddress(), local_addr,
          nullptr));
  Envoy::IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_EQ("QUIC_NO_ERROR with details: Closed by application with reason: Migration to a new "
            "address which is not compatible with this filter.",
            codec_client_->connection()->transportFailureReason());

  // Send 2nd config update to config the filter to add "def" in filter state
  // and allow connection migration.
  sendXdsResponse(filter_name_, "v2", "def", /*allow_server_migration=*/false,
                  /*allow_client_migration*/ true);
  test_server_->waitForCounterGe(
      "extension_config_discovery.quic_listener_filter." + filter_name_ + ".config_reload", 2);
  testRouterHeaderOnlyRequestAndResponse();
  log = waitForAccessLog(access_log_name_, 1);
  EXPECT_THAT(log, testing::HasSubstr("200 \"def\""));
  // Change to a new port, and connection shouldn't fail.
  local_addr = Network::Test::getCanonicalLoopbackAddress(version_);
  dynamic_cast<Quic::EnvoyQuicClientConnection*>(
      dynamic_cast<Quic::EnvoyQuicClientSession&>(codec_client_->rawConnection()).connection())
      ->switchConnectionSocket(Quic::createConnectionSocket(
          codec_client_->rawConnection().connectionInfoProvider().remoteAddress(), local_addr,
          nullptr));
  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  log = waitForAccessLog(access_log_name_, 1);
  EXPECT_THAT(log, testing::HasSubstr("200 \"def\""));
  codec_client_->close();
}

TEST_P(QuicListenerExtensionDiscoveryIntegrationTest, BadEcdsUpdateWithoutDefault) {
  on_server_init_function_ = [&]() { waitXdsStream(); };
  // Do not have default listener filter config nor wait for ECDS update.
  addDynamicFilter(/*apply_without_warming=*/false, /*set_default_config*/ false);
  useAccessLog(fmt::format("%RESPONSE_CODE% %FILTER_STATE({})%",
                           TestQuicListenerFilter::TestStringFilterState::key()));
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // This is a bad config with empty value string.
  sendXdsResponse(filter_name_, "v1", "");
  test_server_->waitForCounterGe(
      "extension_config_discovery.quic_listener_filter." + filter_name_ + ".config_fail", 1);
  test_server_->waitForCounterGe(
      "extension_config_discovery.quic_listener_filter." + filter_name_ + ".config_reload", 1);
  test_server_->waitUntilListenersReady();
  test_server_->waitForGaugeGe("listener_manager.workers_started", 1);
  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initialized);

  Network::ClientConnectionPtr conn = makeClientConnection(lookupPort(port_name_));
  std::shared_ptr<Upstream::MockClusterInfo> cluster{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostDescriptionConstSharedPtr host_description{Upstream::makeTestHostDescription(
      cluster, fmt::format("tcp://{}:80", Network::Test::getLoopbackAddressUrlString(version_)),
      timeSystem())};
  auto codec = std::make_unique<IntegrationCodecClient>(
      *dispatcher_, random_, std::move(conn), host_description, downstream_protocol_, true);
  EXPECT_TRUE(codec->disconnected());
  EXPECT_EQ("QUIC_NO_ERROR with details: Closed by application with reason: no filter chain found",
            codec->connection()->transportFailureReason());
  // The extension_config_missing stats counter increases by 1.
  test_server_->waitForCounterGe("listener.listener_stat.extension_config_missing", 1);

  sendXdsResponse(filter_name_, "v2", "abc");
  test_server_->waitForCounterGe(
      "extension_config_discovery.quic_listener_filter." + filter_name_ + ".config_reload", 2);
  testRouterHeaderOnlyRequestAndResponse();
  codec_client_->close();
  std::string log = waitForAccessLog(access_log_name_, 0);
  EXPECT_THAT(log, testing::HasSubstr("200 \"abc\""));
}

TEST_P(QuicListenerExtensionDiscoveryIntegrationTest, ConfigDump) {
  DISABLE_IF_ADMIN_DISABLED; // Uses admin interface.
  on_server_init_function_ = [&]() { waitXdsStream(); };
  addDynamicFilter(/*apply_without_warming=*/false);
  useAccessLog(fmt::format("%RESPONSE_CODE% %FILTER_STATE({})%",
                           TestQuicListenerFilter::TestStringFilterState::key()));
  initialize();

  EXPECT_EQ(test_server_->server().initManager().state(), Init::Manager::State::Initializing);

  // Send 1st config update to have listener filter add "abc" in filter state.
  sendXdsResponse(filter_name_, "v1", "abc", /*allow_server_migration=*/false,
                  /*allow_client_migration*/ true);
  test_server_->waitForCounterGe(
      "extension_config_discovery.quic_listener_filter." + filter_name_ + ".config_reload", 1);

  // Verify ECDS config dump are working correctly.
  BufferingStreamDecoderPtr response;
  EXPECT_EQ("200", request("admin", "GET", "/config_dump", response));
  EXPECT_EQ("application/json", contentType(response));
  Json::ObjectSharedPtr json = Json::Factory::loadFromString(response->body());
  size_t index = 0;
  for (const Json::ObjectSharedPtr& obj_ptr : json->getObjectArray("configs")) {
    EXPECT_TRUE(expected_types[index].compare(obj_ptr->getString("@type")) == 0);
    index++;
  }

  // Validate we can parse as proto.
  envoy::admin::v3::ConfigDump config_dump;
  TestUtility::loadFromJson(response->body(), config_dump);
  EXPECT_EQ(7u, config_dump.configs_size());

  // With /config_dump, the response has the format: EcdsConfigDump.
  envoy::admin::v3::EcdsConfigDump ecds_config_dump;
  config_dump.configs(2).UnpackTo(&ecds_config_dump);
  EXPECT_EQ("v1", ecds_config_dump.ecds_filters(0).version_info());
  envoy::config::core::v3::TypedExtensionConfig filter_config;
  EXPECT_TRUE(ecds_config_dump.ecds_filters(0).ecds_filter().UnpackTo(&filter_config));
  EXPECT_EQ(filter_name_, filter_config.name());
  test::integration::filters::TestQuicListenerFilterConfig listener_config;
  filter_config.typed_config().UnpackTo(&listener_config);
  EXPECT_EQ("abc", listener_config.added_value());
  EXPECT_FALSE(listener_config.allow_server_migration());
  EXPECT_TRUE(listener_config.allow_client_migration());
}

} // namespace
} // namespace Envoy
#endif
