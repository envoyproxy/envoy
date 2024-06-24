#include "envoy/common/hashable.h"

#include "test/common/tcp_proxy/tcp_proxy_test_base.h"

namespace Envoy {
namespace TcpProxy {

namespace {

using ::testing::Return;

TEST(ConfigTest, DefaultTimeout) {
  const std::string yaml = R"EOF(
stat_prefix: name
cluster: foo
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromYaml(yaml, factory_context));
  EXPECT_EQ(std::chrono::hours(1), config_obj.sharedConfig()->idleTimeout().value());
}

TEST(ConfigTest, DisabledTimeout) {
  const std::string yaml = R"EOF(
stat_prefix: name
cluster: foo
idle_timeout: 0s
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromYaml(yaml, factory_context));
  EXPECT_FALSE(config_obj.sharedConfig()->idleTimeout().has_value());
}

TEST(ConfigTest, FlushAccessLogOnConnected) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  {
    const std::string yaml = R"EOF(
      stat_prefix: name
      cluster: foo
    )EOF";

    Config config_obj(constructConfigFromYaml(yaml, factory_context));
    EXPECT_FALSE(config_obj.sharedConfig()->flushAccessLogOnConnected());
  }

  {
    const std::string yaml = R"EOF(
      stat_prefix: name
      cluster: foo
      access_log_options:
        flush_access_log_on_connected: false
    )EOF";

    Config config_obj(constructConfigFromYaml(yaml, factory_context));
    EXPECT_FALSE(config_obj.sharedConfig()->flushAccessLogOnConnected());
  }

  {
    const std::string yaml = R"EOF(
      stat_prefix: name
      cluster: foo
      access_log_options:
        flush_access_log_on_connected: true
    )EOF";

    Config config_obj(constructConfigFromYaml(yaml, factory_context));
    EXPECT_TRUE(config_obj.sharedConfig()->flushAccessLogOnConnected());
  }
}

TEST(ConfigTest, DEPRECATED_FEATURE_TEST(DeprecatedFlushAccessLogOnConnected)) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  {
    const std::string deprecated_yaml = R"EOF(
      stat_prefix: name
      cluster: foo
      flush_access_log_on_connected: false # deprecated field
    )EOF";

    Config config_obj(constructConfigFromYaml(deprecated_yaml, factory_context));
    EXPECT_FALSE(config_obj.sharedConfig()->flushAccessLogOnConnected());
  }

  {
    const std::string deprecated_yaml = R"EOF(
      stat_prefix: name
      cluster: foo
      flush_access_log_on_connected: true # deprecated field
    )EOF";

    Config config_obj(constructConfigFromYaml(deprecated_yaml, factory_context));
    EXPECT_TRUE(config_obj.sharedConfig()->flushAccessLogOnConnected());
  }

  {
    const std::string deprecated_yaml = R"EOF(
      stat_prefix: name
      cluster: foo
      access_log_options:
        flush_access_log_on_connected: true
      flush_access_log_on_connected: true # deprecated field
    )EOF";

    EXPECT_THROW_WITH_MESSAGE(
        Config config_obj(constructConfigFromYaml(deprecated_yaml, factory_context)),
        EnvoyException,
        "Only one of flush_access_log_on_connected or access_log_options can be specified.");
  }

  {
    const std::string deprecated_yaml = R"EOF(
      stat_prefix: name
      cluster: foo
      access_log_options:
        flush_access_log_on_connected: true
      access_log_flush_interval: 1s # deprecated field
    )EOF";

    EXPECT_THROW_WITH_MESSAGE(
        Config config_obj(constructConfigFromYaml(deprecated_yaml, factory_context)),
        EnvoyException,
        "Only one of access_log_flush_interval or access_log_options can be specified.");
  }
}

TEST(ConfigTest, AccessLogFlushInterval) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  {
    const std::string yaml = R"EOF(
      stat_prefix: name
      cluster: foo
    )EOF";

    Config config_obj(constructConfigFromYaml(yaml, factory_context));
    EXPECT_FALSE(config_obj.sharedConfig()->accessLogFlushInterval().has_value());
  }

  {
    const std::string yaml = R"EOF(
      stat_prefix: name
      cluster: foo
      access_log_options:
        access_log_flush_interval: 1s
    )EOF";

    Config config_obj(constructConfigFromYaml(yaml, factory_context));
    EXPECT_TRUE(config_obj.sharedConfig()->accessLogFlushInterval().has_value());
    EXPECT_EQ(std::chrono::seconds(1), config_obj.sharedConfig()->accessLogFlushInterval().value());
  }
}

TEST(ConfigTest, DEPRECATED_FEATURE_TEST(DeprecatedAccessLogFlushInterval)) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  {
    const std::string deprecated_yaml = R"EOF(
      stat_prefix: name
      cluster: foo
      access_log_flush_interval: 1s # deprecated field
    )EOF";

    Config config_obj(constructConfigFromYaml(deprecated_yaml, factory_context));
    EXPECT_TRUE(config_obj.sharedConfig()->accessLogFlushInterval().has_value());
    EXPECT_EQ(std::chrono::seconds(1), config_obj.sharedConfig()->accessLogFlushInterval().value());
  }

  {
    const std::string deprecated_yaml = R"EOF(
      stat_prefix: name
      cluster: foo
      access_log_options:
        access_log_flush_interval: 1s
      access_log_flush_interval: 1s # deprecated field
    )EOF";

    EXPECT_THROW_WITH_MESSAGE(
        Config config_obj(constructConfigFromYaml(deprecated_yaml, factory_context)),
        EnvoyException,
        "Only one of access_log_flush_interval or access_log_options can be specified.");
  }

  {
    const std::string deprecated_yaml = R"EOF(
      stat_prefix: name
      cluster: foo
      access_log_options:
        access_log_flush_interval: 1s
      flush_access_log_on_connected: true # deprecated field
    )EOF";

    EXPECT_THROW_WITH_MESSAGE(
        Config config_obj(constructConfigFromYaml(deprecated_yaml, factory_context)),
        EnvoyException,
        "Only one of flush_access_log_on_connected or access_log_options can be specified.");
  }
}

TEST(ConfigTest, CustomTimeout) {
  const std::string yaml = R"EOF(
stat_prefix: name
cluster: foo
idle_timeout: 1s
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromYaml(yaml, factory_context));
  EXPECT_EQ(std::chrono::seconds(1), config_obj.sharedConfig()->idleTimeout().value());
}

TEST(ConfigTest, MaxDownstreamConnectionDuration) {
  const std::string yaml = R"EOF(
stat_prefix: name
cluster: foo
max_downstream_connection_duration: 10s
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromYaml(yaml, factory_context));
  EXPECT_EQ(std::chrono::seconds(10), config_obj.maxDownstreamConnectionDuration().value());
}

TEST(ConfigTest, NoRouteConfig) {
  const std::string yaml = R"EOF(
  stat_prefix: name
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  EXPECT_THROW(constructConfigFromYaml(yaml, factory_context), EnvoyException);
}

// Tests that it's not possible to define a weighted cluster with 0 weight.
TEST(ConfigTest, WeightedClusterWithZeroWeightConfig) {
  const std::string yaml = R"EOF(
  stat_prefix: name
  weighted_clusters:
    clusters:
    - name: cluster1
      weight: 1
    - name: cluster2
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  EXPECT_THROW(constructConfigFromYaml(yaml, factory_context), EnvoyException);
}

// Tests that it is possible to define a list of weighted clusters.
TEST(ConfigTest, WeightedClustersConfig) {
  const std::string yaml = R"EOF(
  stat_prefix: name
  weighted_clusters:
    clusters:
    - name: cluster1
      weight: 1
    - name: cluster2
      weight: 2
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromYaml(yaml, factory_context));

  NiceMock<Network::MockConnection> connection;
  EXPECT_CALL(factory_context.server_factory_context_.api_.random_, random()).WillOnce(Return(0));
  EXPECT_EQ(std::string("cluster1"), config_obj.getRouteFromEntries(connection)->clusterName());

  EXPECT_CALL(factory_context.server_factory_context_.api_.random_, random()).WillOnce(Return(2));
  EXPECT_EQ(std::string("cluster2"), config_obj.getRouteFromEntries(connection)->clusterName());
}

// Tests that it is possible to define a list of weighted clusters with independent endpoint
// selectors.
TEST(ConfigTest, WeightedClustersWithMetadataMatchConfig) {
  const std::string yaml = R"EOF(
  stat_prefix: name
  weighted_clusters:
    clusters:
    - name: cluster1
      weight: 1
      metadata_match:
        filter_metadata:
          envoy.lb:
            k1: v1
            k2: v2
    - name: cluster2
      weight: 2
      metadata_match:
        filter_metadata:
          envoy.lb:
            k3: v3
            k4: v4
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromYaml(yaml, factory_context));

  {
    ProtobufWkt::Value v1, v2;
    v1.set_string_value("v1");
    v2.set_string_value("v2");
    HashedValue hv1(v1), hv2(v2);

    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(factory_context.server_factory_context_.api_.random_, random()).WillOnce(Return(0));

    const auto route = config_obj.getRouteFromEntries(connection);
    EXPECT_NE(nullptr, route);

    EXPECT_EQ("cluster1", route->clusterName());

    const auto* criteria = route->metadataMatchCriteria();
    EXPECT_NE(nullptr, criteria);

    const auto& criterions = criteria->metadataMatchCriteria();
    EXPECT_EQ(2, criterions.size());

    EXPECT_EQ("k1", criterions[0]->name());
    EXPECT_EQ(hv1, criterions[0]->value());

    EXPECT_EQ("k2", criterions[1]->name());
    EXPECT_EQ(hv2, criterions[1]->value());
  }

  {
    ProtobufWkt::Value v3, v4;
    v3.set_string_value("v3");
    v4.set_string_value("v4");
    HashedValue hv3(v3), hv4(v4);

    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(factory_context.server_factory_context_.api_.random_, random()).WillOnce(Return(2));

    const auto route = config_obj.getRouteFromEntries(connection);
    EXPECT_NE(nullptr, route);

    EXPECT_EQ("cluster2", route->clusterName());

    const auto* criteria = route->metadataMatchCriteria();
    EXPECT_NE(nullptr, criteria);

    const auto& criterions = criteria->metadataMatchCriteria();
    EXPECT_EQ(2, criterions.size());

    EXPECT_EQ("k3", criterions[0]->name());
    EXPECT_EQ(hv3, criterions[0]->value());

    EXPECT_EQ("k4", criterions[1]->name());
    EXPECT_EQ(hv4, criterions[1]->value());
  }
}

// Tests that an individual endpoint selector of a weighted cluster gets merged with the top-level
// endpoint selector.
TEST(ConfigTest, WeightedClustersWithMetadataMatchAndTopLevelMetadataMatchConfig) {
  const std::string yaml = R"EOF(
  stat_prefix: name
  weighted_clusters:
    clusters:
    - name: cluster1
      weight: 1
      metadata_match:
        filter_metadata:
          envoy.lb:
            k1: v1
            k2: v2
    - name: cluster2
      weight: 2
      metadata_match:
        filter_metadata:
          envoy.lb:
            k3: v3
            k4: v4
  metadata_match:
    filter_metadata:
      envoy.lb:
        k0: v00
        k1: v01
        k4: v04
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromYaml(yaml, factory_context));

  ProtobufWkt::Value v00, v01, v04;
  v00.set_string_value("v00");
  v01.set_string_value("v01");
  v04.set_string_value("v04");
  HashedValue hv00(v00), hv01(v01), hv04(v04);

  {
    ProtobufWkt::Value v1, v2;
    v1.set_string_value("v1");
    v2.set_string_value("v2");
    HashedValue hv1(v1), hv2(v2);

    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(factory_context.server_factory_context_.api_.random_, random()).WillOnce(Return(0));

    const auto route = config_obj.getRouteFromEntries(connection);
    EXPECT_NE(nullptr, route);

    EXPECT_EQ("cluster1", route->clusterName());

    const auto* criteria = route->metadataMatchCriteria();
    EXPECT_NE(nullptr, criteria);

    const auto& criterions = criteria->metadataMatchCriteria();
    EXPECT_EQ(4, criterions.size());

    EXPECT_EQ("k0", criterions[0]->name());
    EXPECT_EQ(hv00, criterions[0]->value());

    EXPECT_EQ("k1", criterions[1]->name());
    EXPECT_EQ(hv1, criterions[1]->value());

    EXPECT_EQ("k2", criterions[2]->name());
    EXPECT_EQ(hv2, criterions[2]->value());

    EXPECT_EQ("k4", criterions[3]->name());
    EXPECT_EQ(hv04, criterions[3]->value());
  }

  {
    ProtobufWkt::Value v3, v4;
    v3.set_string_value("v3");
    v4.set_string_value("v4");
    HashedValue hv3(v3), hv4(v4);

    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(factory_context.server_factory_context_.api_.random_, random()).WillOnce(Return(2));

    const auto route = config_obj.getRouteFromEntries(connection);
    EXPECT_NE(nullptr, route);

    EXPECT_EQ("cluster2", route->clusterName());

    const auto* criteria = route->metadataMatchCriteria();
    EXPECT_NE(nullptr, criteria);

    const auto& criterions = criteria->metadataMatchCriteria();
    EXPECT_EQ(4, criterions.size());

    EXPECT_EQ("k0", criterions[0]->name());
    EXPECT_EQ(hv00, criterions[0]->value());

    EXPECT_EQ("k1", criterions[1]->name());
    EXPECT_EQ(hv01, criterions[1]->value());

    EXPECT_EQ("k3", criterions[2]->name());
    EXPECT_EQ(hv3, criterions[2]->value());

    EXPECT_EQ("k4", criterions[3]->name());
    EXPECT_EQ(hv4, criterions[3]->value());
  }
}

// Tests that a weighted cluster gets the top-level endpoint selector.
TEST(ConfigTest, WeightedClustersWithTopLevelMetadataMatchConfig) {
  const std::string yaml = R"EOF(
  stat_prefix: name
  weighted_clusters:
    clusters:
    - name: cluster1
      weight: 1
  metadata_match:
    filter_metadata:
      envoy.lb:
        k1: v1
        k2: v2
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromYaml(yaml, factory_context));

  ProtobufWkt::Value v1, v2;
  v1.set_string_value("v1");
  v2.set_string_value("v2");
  HashedValue hv1(v1), hv2(v2);

  NiceMock<Network::MockConnection> connection;
  const auto route = config_obj.getRouteFromEntries(connection);
  EXPECT_NE(nullptr, route);

  EXPECT_EQ("cluster1", route->clusterName());

  const auto* criteria = route->metadataMatchCriteria();
  EXPECT_NE(nullptr, criteria);

  const auto& criterions = criteria->metadataMatchCriteria();
  EXPECT_EQ(2, criterions.size());

  EXPECT_EQ("k1", criterions[0]->name());
  EXPECT_EQ(hv1, criterions[0]->value());

  EXPECT_EQ("k2", criterions[1]->name());
  EXPECT_EQ(hv2, criterions[1]->value());
}

// Tests that it is possible to define the top-level endpoint selector.
TEST(ConfigTest, TopLevelMetadataMatchConfig) {
  const std::string yaml = R"EOF(
  stat_prefix: name
  cluster: foo
  metadata_match:
    filter_metadata:
      envoy.lb:
        k1: v1
        k2: v2
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromYaml(yaml, factory_context));

  ProtobufWkt::Value v1, v2;
  v1.set_string_value("v1");
  v2.set_string_value("v2");
  HashedValue hv1(v1), hv2(v2);

  const auto* criteria = config_obj.metadataMatchCriteria();
  EXPECT_NE(nullptr, criteria);

  const auto& criterions = criteria->metadataMatchCriteria();
  EXPECT_EQ(2, criterions.size());

  EXPECT_EQ("k1", criterions[0]->name());
  EXPECT_EQ(hv1, criterions[0]->value());

  EXPECT_EQ("k2", criterions[1]->name());
  EXPECT_EQ(hv2, criterions[1]->value());
}

// Tests that a regular cluster gets the top-level endpoint selector.
TEST(ConfigTest, ClusterWithTopLevelMetadataMatchConfig) {
  const std::string yaml = R"EOF(
  stat_prefix: name
  cluster: foo
  metadata_match:
    filter_metadata:
      envoy.lb:
        k1: v1
        k2: v2
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromYaml(yaml, factory_context));

  ProtobufWkt::Value v1, v2;
  v1.set_string_value("v1");
  v2.set_string_value("v2");
  HashedValue hv1(v1), hv2(v2);

  NiceMock<Network::MockConnection> connection;
  const auto route = config_obj.getRouteFromEntries(connection);
  EXPECT_NE(nullptr, route);

  EXPECT_EQ("foo", route->clusterName());

  const auto* criteria = route->metadataMatchCriteria();
  EXPECT_NE(nullptr, criteria);

  const auto& criterions = criteria->metadataMatchCriteria();
  EXPECT_EQ(2, criterions.size());

  EXPECT_EQ("k1", criterions[0]->name());
  EXPECT_EQ(hv1, criterions[0]->value());

  EXPECT_EQ("k2", criterions[1]->name());
  EXPECT_EQ(hv2, criterions[1]->value());
}

// Tests that a per connection cluster gets the top-level endpoint selector.
TEST(ConfigTest, PerConnectionClusterWithTopLevelMetadataMatchConfig) {
  const std::string yaml = R"EOF(
  stat_prefix: name
  cluster: foo
  metadata_match:
    filter_metadata:
      envoy.lb:
        k1: v1
        k2: v2
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromYaml(yaml, factory_context));

  ProtobufWkt::Value v1, v2;
  v1.set_string_value("v1");
  v2.set_string_value("v2");
  HashedValue hv1(v1), hv2(v2);

  NiceMock<Network::MockConnection> connection;
  connection.stream_info_.filterState()->setData(
      "envoy.tcp_proxy.cluster", std::make_unique<PerConnectionCluster>("filter_state_cluster"),
      StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Connection);

  const auto route = config_obj.getRouteFromEntries(connection);
  EXPECT_NE(nullptr, route);

  EXPECT_EQ("filter_state_cluster", route->clusterName());

  const auto* criteria = route->metadataMatchCriteria();
  EXPECT_NE(nullptr, criteria);

  const auto& criterions = criteria->metadataMatchCriteria();
  EXPECT_EQ(2, criterions.size());

  EXPECT_EQ("k1", criterions[0]->name());
  EXPECT_EQ(hv1, criterions[0]->value());

  EXPECT_EQ("k2", criterions[1]->name());
  EXPECT_EQ(hv2, criterions[1]->value());
}

TEST(ConfigTest, HashWithSourceIpConfig) {
  const std::string yaml = R"EOF(
  stat_prefix: name
  cluster: foo
  hash_policy:
  - source_ip: {}
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromYaml(yaml, factory_context));
  EXPECT_NE(nullptr, config_obj.hashPolicy());
}

TEST(ConfigTest, HashWithFilterStateConfig) {
  const std::string yaml = R"EOF(
  stat_prefix: name
  cluster: foo
  hash_policy:
  - filter_state: {
      key: foo
    }
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromYaml(yaml, factory_context));
  EXPECT_NE(nullptr, config_obj.hashPolicy());
}

TEST(ConfigTest, HashWithDefaultConfig) {
  const std::string yaml = R"EOF(
  stat_prefix: name
  cluster: foo
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromYaml(yaml, factory_context));
  EXPECT_EQ(nullptr, config_obj.hashPolicy());
}

TEST(ConfigTest, AccessLogConfig) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config;
  envoy::config::accesslog::v3::AccessLog* log = config.mutable_access_log()->Add();
  log->set_name("envoy.access_loggers.file");
  {
    envoy::extensions::access_loggers::file::v3::FileAccessLog file_access_log;
    file_access_log.set_path("some_path");
    file_access_log.mutable_log_format()->mutable_text_format_source()->set_inline_string(
        "the format specifier");
    log->mutable_typed_config()->PackFrom(file_access_log);
  }

  log = config.mutable_access_log()->Add();
  log->set_name("envoy.access_loggers.file");
  {
    envoy::extensions::access_loggers::file::v3::FileAccessLog file_access_log;
    file_access_log.set_path("another path");
    log->mutable_typed_config()->PackFrom(file_access_log);
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  Config config_obj(config, factory_context_);

  EXPECT_EQ(2, config_obj.accessLogs().size());
}

class TcpProxyNonDeprecatedConfigRoutingTest : public testing::Test {
public:
  void setup() {
    const std::string yaml = R"EOF(
    stat_prefix: name
    cluster: fake_cluster
    )EOF";

    factory_context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
        {"fake_cluster"});
    config_ = std::make_shared<Config>(constructConfigFromYaml(yaml, factory_context_));
  }

  void initializeFilter() {
    EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

    filter_ = std::make_unique<Filter>(config_,
                                       factory_context_.server_factory_context_.cluster_manager_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  ConfigSharedPtr config_;
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  std::unique_ptr<Filter> filter_;
};

TEST_F(TcpProxyNonDeprecatedConfigRoutingTest, ClusterNameSet) {
  setup();

  initializeFilter();

  // Port 9999 is within the specified destination port range.
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 9999));

  // Expect filter to try to open a connection to specified cluster.
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _))
      .WillOnce(Return(absl::nullopt));
  absl::optional<Upstream::ClusterInfoConstSharedPtr> cluster_info;
  EXPECT_CALL(connection_.stream_info_, setUpstreamClusterInfo(_))
      .WillOnce(
          Invoke([&cluster_info](const Upstream::ClusterInfoConstSharedPtr& upstream_cluster_info) {
            cluster_info = upstream_cluster_info;
          }));
  EXPECT_CALL(connection_.stream_info_, upstreamClusterInfo())
      .WillOnce(ReturnPointee(&cluster_info));

  filter_->onNewConnection();

  EXPECT_EQ(connection_.stream_info_.upstreamClusterInfo().value()->name(), "fake_cluster");
}

class TcpProxyHashingTest : public testing::Test {
public:
  void setup(const std::string& yaml) {
    factory_context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
        {"fake_cluster"});
    config_ = std::make_shared<Config>(constructConfigFromYaml(yaml, factory_context_));
  }

  void initializeFilter() { initializeFilter(filter_callbacks_, connection_); }

  void initializeFilter(Network::MockReadFilterCallbacks& filter_callbacks,
                        Network::MockConnection& connection) {
    EXPECT_CALL(filter_callbacks, connection()).WillRepeatedly(testing::ReturnRef(connection));

    filter_ = std::make_unique<Filter>(config_,
                                       factory_context_.server_factory_context_.cluster_manager_);
    filter_->initializeReadFilterCallbacks(filter_callbacks);
  }

  Event::TestTimeSystem& timeSystem() {
    return factory_context_.server_factory_context_.timeSystem();
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  ConfigSharedPtr config_;
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  std::unique_ptr<Filter> filter_;

  class HashableObj : public StreamInfo::FilterState::Object, public Hashable {
  public:
    absl::optional<uint64_t> hash() const override { return 31337; }
  };
};

// Test TCP proxy using source IP to hash.
TEST_F(TcpProxyHashingTest, HashWithSourceIp) {
  const std::string yaml = R"EOF(
    stat_prefix: name
    cluster: fake_cluster
    hash_policy:
    - source_ip: {}
    )EOF";
  setup(yaml);

  {
    NiceMock<Network::MockReadFilterCallbacks> filter_callbacks;
    NiceMock<Network::MockConnection> mock_connection;
    initializeFilter(filter_callbacks, mock_connection);

    // Ensure there is no remote address (MockStreamInfo sets one by default), and expect no hash.
    mock_connection.stream_info_.downstream_connection_info_provider_->setRemoteAddress(nullptr);
    EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
                tcpConnPool(_, _))
        .WillOnce(Invoke([](Upstream::ResourcePriority, Upstream::LoadBalancerContext* context) {
          EXPECT_FALSE(context->computeHashKey().has_value());
          return absl::nullopt;
        }));
    filter_->onNewConnection();
  }

  // Set remote address, and expect a hash.
  {
    initializeFilter();
    connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 1111));
    EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
                tcpConnPool(_, _))
        .WillOnce(Invoke([](Upstream::ResourcePriority, Upstream::LoadBalancerContext* context) {
          EXPECT_TRUE(context->computeHashKey().has_value());
          return absl::nullopt;
        }));
    filter_->onNewConnection();
  }
}

// Test TCP proxy using filter state to hash.
TEST_F(TcpProxyHashingTest, HashWithFilterState) {
  const std::string yaml = R"EOF(
    stat_prefix: name
    cluster: fake_cluster
    hash_policy:
    - filter_state: {
        key: foo
      }
    )EOF";
  setup(yaml);

  {
    NiceMock<Network::MockReadFilterCallbacks> filter_callbacks;
    NiceMock<Network::MockConnection> mock_connection;
    initializeFilter(filter_callbacks, mock_connection);
    // Expect no hash when filter state is unset.
    EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
                tcpConnPool(_, _))
        .WillOnce(Invoke([](Upstream::ResourcePriority, Upstream::LoadBalancerContext* context) {
          EXPECT_FALSE(context->computeHashKey().has_value());
          return absl::nullopt;
        }));
    filter_->onNewConnection();
  }

  // Set filter state, and expect HashableObj's hash is now used.
  {
    initializeFilter();
    connection_.stream_info_.filter_state_->setData("foo", std::make_unique<HashableObj>(),
                                                    StreamInfo::FilterState::StateType::ReadOnly,
                                                    StreamInfo::FilterState::LifeSpan::FilterChain);
    EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
                tcpConnPool(_, _))
        .WillOnce(Invoke([](Upstream::ResourcePriority, Upstream::LoadBalancerContext* context) {
          EXPECT_EQ(31337, context->computeHashKey().value());
          return absl::nullopt;
        }));
    filter_->onNewConnection();
  }
}

} // namespace
} // namespace TcpProxy
} // namespace Envoy
