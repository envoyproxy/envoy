#include "test/common/tcp_proxy/tcp_proxy_test_base.h"

namespace Envoy {
namespace TcpProxy {

namespace {
TEST(ConfigTest, DefaultTimeout) {
  const std::string yaml = R"EOF(
stat_prefix: name
cluster: foo
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromV3Yaml(yaml, factory_context));
  EXPECT_EQ(std::chrono::hours(1), config_obj.sharedConfig()->idleTimeout().value());
}

TEST(ConfigTest, DisabledTimeout) {
  const std::string yaml = R"EOF(
stat_prefix: name
cluster: foo
idle_timeout: 0s
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromV3Yaml(yaml, factory_context));
  EXPECT_FALSE(config_obj.sharedConfig()->idleTimeout().has_value());
}

TEST(ConfigTest, CustomTimeout) {
  const std::string yaml = R"EOF(
stat_prefix: name
cluster: foo
idle_timeout: 1s
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromV3Yaml(yaml, factory_context));
  EXPECT_EQ(std::chrono::seconds(1), config_obj.sharedConfig()->idleTimeout().value());
}

TEST(ConfigTest, MaxDownstreamConnectionDuration) {
  const std::string yaml = R"EOF(
stat_prefix: name
cluster: foo
max_downstream_connection_duration: 10s
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromV3Yaml(yaml, factory_context));
  EXPECT_EQ(std::chrono::seconds(10), config_obj.maxDownstreamConnectionDuration().value());
}

TEST(ConfigTest, NoRouteConfig) {
  const std::string yaml = R"EOF(
  stat_prefix: name
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  EXPECT_THROW(constructConfigFromYaml(yaml, factory_context), EnvoyException);
}

TEST(ConfigTest, DEPRECATED_FEATURE_TEST(BadConfig)) {
  const std::string yaml_string = R"EOF(
  stat_prefix: 1
  cluster: cluster
  deprecated_v1:
    routes:
    - cluster: fake_cluster
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  EXPECT_THROW(constructConfigFromYaml(yaml_string, factory_context, false), EnvoyException);
}

TEST(ConfigTest, DEPRECATED_FEATURE_TEST(EmptyRouteConfig)) {
  const std::string yaml = R"EOF(
  stat_prefix: name
  cluster: cluster
  deprecated_v1:
    routes: []
  )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  EXPECT_THROW(constructConfigFromYaml(yaml, factory_context_, false), EnvoyException);
}

TEST(ConfigTest, DEPRECATED_FEATURE_TEST(Routes)) {
  TestDeprecatedV2Api _deprecated_v2_api;
  const std::string yaml = R"EOF(
  stat_prefix: name
  cluster: cluster
  deprecated_v1:
    routes:
    - destination_ip_list:
      - address_prefix: 10.10.10.10
        prefix_len: 32
      - address_prefix: 10.10.11.0
        prefix_len: 24
      - address_prefix: 10.11.0.0
        prefix_len: 16
      - address_prefix: 11.0.0.0
        prefix_len: 8
      - address_prefix: 128.0.0.0
        prefix_len: 1
      cluster: with_destination_ip_list
    - destination_ip_list:
      - address_prefix: "::1"
        prefix_len: 128
      - address_prefix: "2001:abcd::"
        prefix_len: 64
      cluster: with_v6_destination
    - destination_ports: 1-1024,2048-4096,12345
      cluster: with_destination_ports
    - source_ports: '23457,23459'
      cluster: with_source_ports
    - destination_ip_list:
      - address_prefix: "2002::"
        prefix_len: 32
      source_ip_list:
      - address_prefix: "2003::"
        prefix_len: 64
      cluster: with_v6_source_and_destination
    - destination_ip_list:
      - address_prefix: 10.0.0.0
        prefix_len: 24
      source_ip_list:
      - address_prefix: 20.0.0.0
        prefix_len: 24
      destination_ports: '10000'
      source_ports: '20000'
      cluster: with_everything
    - cluster: catch_all
    )EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  Config config_obj(constructConfigFromYaml(yaml, factory_context_, false));

  {
    // hit route with destination_ip (10.10.10.10/32)
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("10.10.10.10"));
    EXPECT_EQ(std::string("with_destination_ip_list"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("10.10.10.11"));
    connection.stream_info_.downstream_address_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0"));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit route with destination_ip (10.10.11.0/24)
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11"));
    EXPECT_EQ(std::string("with_destination_ip_list"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("10.10.12.12"));
    connection.stream_info_.downstream_address_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0"));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit route with destination_ip (10.11.0.0/16)
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("10.11.11.11"));
    EXPECT_EQ(std::string("with_destination_ip_list"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("10.12.12.12"));
    connection.stream_info_.downstream_address_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0"));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit route with destination_ip (11.0.0.0/8)
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("11.11.11.11"));
    EXPECT_EQ(std::string("with_destination_ip_list"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("12.12.12.12"));
    connection.stream_info_.downstream_address_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0"));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit route with destination_ip (128.0.0.0/8)
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("128.255.255.255"));
    EXPECT_EQ(std::string("with_destination_ip_list"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit route with destination port range
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 12345));
    EXPECT_EQ(std::string("with_destination_ports"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // fall through
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 23456));
    connection.stream_info_.downstream_address_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0"));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit route with source port range
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 23456));
    connection.stream_info_.downstream_address_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0", 23459));
    EXPECT_EQ(std::string("with_source_ports"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // fall through
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 23456));
    connection.stream_info_.downstream_address_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0", 23458));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit the route with all criteria present
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("10.0.0.0", 10000));
    connection.stream_info_.downstream_address_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("20.0.0.0", 20000));
    EXPECT_EQ(std::string("with_everything"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // fall through
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("10.0.0.0", 10000));
    connection.stream_info_.downstream_address_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>("30.0.0.0", 20000));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit route with destination_ip (::1/128)
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv6Instance>("::1"));
    EXPECT_EQ(std::string("with_v6_destination"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit route with destination_ip ("2001:abcd/64")
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv6Instance>("2001:abcd:0:0:1::"));
    EXPECT_EQ(std::string("with_v6_destination"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit route with destination_ip ("2002::/32") and source_ip ("2003::/64")
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv6Instance>("2002:0:0:0:0:0::1"));
    connection.stream_info_.downstream_address_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv6Instance>("2003:0:0:0:0::5"));
    EXPECT_EQ(std::string("with_v6_source_and_destination"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // fall through
    NiceMock<Network::MockConnection> connection;
    connection.stream_info_.downstream_address_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv6Instance>("2004::"));
    connection.stream_info_.downstream_address_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv6Instance>("::"));
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection)->clusterName());
  }
}

// Tests that a deprecated_v1 route gets the top-level endpoint selector.
TEST(ConfigTest, DEPRECATED_FEATURE_TEST(RouteWithTopLevelMetadataMatchConfig)) {
  TestDeprecatedV2Api _deprecated_v2_api;
  const std::string yaml = R"EOF(
  stat_prefix: name
  cluster: cluster
  deprecated_v1:
    routes:
    - cluster: catch_all
  metadata_match:
    filter_metadata:
      envoy.lb:
        k1: v1
        k2: v2
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  Config config_obj(constructConfigFromYaml(yaml, factory_context_, false));

  ProtobufWkt::Value v1, v2;
  v1.set_string_value("v1");
  v2.set_string_value("v2");
  HashedValue hv1(v1), hv2(v2);

  NiceMock<Network::MockConnection> connection;
  const auto route = config_obj.getRouteFromEntries(connection);
  EXPECT_NE(nullptr, route);

  EXPECT_EQ("catch_all", route->clusterName());

  const auto* criteria = route->metadataMatchCriteria();
  EXPECT_NE(nullptr, criteria);

  const auto& criterions = criteria->metadataMatchCriteria();
  EXPECT_EQ(2, criterions.size());

  EXPECT_EQ("k1", criterions[0]->name());
  EXPECT_EQ(hv1, criterions[0]->value());

  EXPECT_EQ("k2", criterions[1]->name());
  EXPECT_EQ(hv2, criterions[1]->value());
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
  EXPECT_THROW(constructConfigFromV3Yaml(yaml, factory_context), EnvoyException);
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
  Config config_obj(constructConfigFromV3Yaml(yaml, factory_context));

  NiceMock<Network::MockConnection> connection;
  EXPECT_CALL(factory_context.api_.random_, random()).WillOnce(Return(0));
  EXPECT_EQ(std::string("cluster1"), config_obj.getRouteFromEntries(connection)->clusterName());

  EXPECT_CALL(factory_context.api_.random_, random()).WillOnce(Return(2));
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
  Config config_obj(constructConfigFromV3Yaml(yaml, factory_context));

  {
    ProtobufWkt::Value v1, v2;
    v1.set_string_value("v1");
    v2.set_string_value("v2");
    HashedValue hv1(v1), hv2(v2);

    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(factory_context.api_.random_, random()).WillOnce(Return(0));

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
    EXPECT_CALL(factory_context.api_.random_, random()).WillOnce(Return(2));

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
  Config config_obj(constructConfigFromV3Yaml(yaml, factory_context));

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
    EXPECT_CALL(factory_context.api_.random_, random()).WillOnce(Return(0));

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
    EXPECT_CALL(factory_context.api_.random_, random()).WillOnce(Return(2));

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
  Config config_obj(constructConfigFromV3Yaml(yaml, factory_context));

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
  Config config_obj(constructConfigFromV3Yaml(yaml, factory_context));

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
  Config config_obj(constructConfigFromV3Yaml(yaml, factory_context));

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
  Config config_obj(constructConfigFromV3Yaml(yaml, factory_context));

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
  Config config_obj(constructConfigFromV3Yaml(yaml, factory_context));
  EXPECT_NE(nullptr, config_obj.hashPolicy());
}

TEST(ConfigTest, HashWithSourceIpDefaultConfig) {
  const std::string yaml = R"EOF(
  stat_prefix: name
  cluster: foo
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromV3Yaml(yaml, factory_context));
  EXPECT_EQ(nullptr, config_obj.hashPolicy());
}

TEST(ConfigTest, AccessLogConfig) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config;
  envoy::config::accesslog::v3::AccessLog* log = config.mutable_access_log()->Add();
  log->set_name(Extensions::AccessLoggers::AccessLogNames::get().File);
  {
    envoy::extensions::access_loggers::file::v3::FileAccessLog file_access_log;
    file_access_log.set_path("some_path");
    file_access_log.mutable_log_format()->mutable_text_format_source()->set_inline_string(
        "the format specifier");
    log->mutable_typed_config()->PackFrom(file_access_log);
  }

  log = config.mutable_access_log()->Add();
  log->set_name(Extensions::AccessLoggers::AccessLogNames::get().File);
  {
    envoy::extensions::access_loggers::file::v3::FileAccessLog file_access_log;
    file_access_log.set_path("another path");
    log->mutable_typed_config()->PackFrom(file_access_log);
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  Config config_obj(config, factory_context_);

  EXPECT_EQ(2, config_obj.accessLogs().size());
}
} // namespace
} // namespace TcpProxy
} // namespace Envoy