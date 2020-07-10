#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.validate.h"

#include "common/buffer/buffer_impl.h"
#include "common/network/address_impl.h"
#include "common/network/application_protocol.h"
#include "common/network/transport_socket_options_impl.h"
#include "common/network/upstream_server_name.h"
#include "common/router/metadatamatchcriteria_impl.h"
#include "common/tcp_proxy/tcp_proxy.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/access_loggers/well_known_names.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace TcpProxy {
namespace {

using ::Envoy::Network::UpstreamServerName;

namespace {
Config constructConfigFromYaml(const std::string& yaml,
                               Server::Configuration::FactoryContext& context,
                               bool avoid_boosting = true) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
  TestUtility::loadFromYamlAndValidate(yaml, tcp_proxy, false, avoid_boosting);
  return Config(tcp_proxy, context);
}

Config constructConfigFromV2Yaml(const std::string& yaml,
                                 Server::Configuration::FactoryContext& context) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy tcp_proxy;
  TestUtility::loadFromYamlAndValidate(yaml, tcp_proxy);
  return Config(tcp_proxy, context);
}

} // namespace

TEST(ConfigTest, DefaultTimeout) {
  const std::string yaml = R"EOF(
stat_prefix: name
cluster: foo
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromV2Yaml(yaml, factory_context));
  EXPECT_EQ(std::chrono::hours(1), config_obj.sharedConfig()->idleTimeout().value());
}

TEST(ConfigTest, DisabledTimeout) {
  const std::string yaml = R"EOF(
stat_prefix: name
cluster: foo
idle_timeout: 0s
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromV2Yaml(yaml, factory_context));
  EXPECT_FALSE(config_obj.sharedConfig()->idleTimeout().has_value());
}

TEST(ConfigTest, CustomTimeout) {
  const std::string yaml = R"EOF(
stat_prefix: name
cluster: foo
idle_timeout: 1s
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromV2Yaml(yaml, factory_context));
  EXPECT_EQ(std::chrono::seconds(1), config_obj.sharedConfig()->idleTimeout().value());
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
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.10.10.10");
    EXPECT_EQ(std::string("with_destination_ip_list"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.10.10.11");
    connection.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0");
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit route with destination_ip (10.10.11.0/24)
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11");
    EXPECT_EQ(std::string("with_destination_ip_list"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.10.12.12");
    connection.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0");
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit route with destination_ip (10.11.0.0/16)
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.11.11.11");
    EXPECT_EQ(std::string("with_destination_ip_list"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.12.12.12");
    connection.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0");
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit route with destination_ip (11.0.0.0/8)
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("11.11.11.11");
    EXPECT_EQ(std::string("with_destination_ip_list"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("12.12.12.12");
    connection.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0");
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit route with destination_ip (128.0.0.0/8)
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("128.255.255.255");
    EXPECT_EQ(std::string("with_destination_ip_list"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit route with destination port range
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 12345);
    EXPECT_EQ(std::string("with_destination_ports"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // fall through
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 23456);
    connection.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0");
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit route with source port range
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 23456);
    connection.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0", 23459);
    EXPECT_EQ(std::string("with_source_ports"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // fall through
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 23456);
    connection.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0", 23458);
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit the route with all criteria present
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.0.0.0", 10000);
    connection.remote_address_ =
        std::make_shared<Network::Address::Ipv4Instance>("20.0.0.0", 20000);
    EXPECT_EQ(std::string("with_everything"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // fall through
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.0.0.0", 10000);
    connection.remote_address_ =
        std::make_shared<Network::Address::Ipv4Instance>("30.0.0.0", 20000);
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit route with destination_ip (::1/128)
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv6Instance>("::1");
    EXPECT_EQ(std::string("with_v6_destination"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit route with destination_ip ("2001:abcd/64")
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ =
        std::make_shared<Network::Address::Ipv6Instance>("2001:abcd:0:0:1::");
    EXPECT_EQ(std::string("with_v6_destination"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // hit route with destination_ip ("2002::/32") and source_ip ("2003::/64")
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ =
        std::make_shared<Network::Address::Ipv6Instance>("2002:0:0:0:0:0::1");
    connection.remote_address_ =
        std::make_shared<Network::Address::Ipv6Instance>("2003:0:0:0:0::5");
    EXPECT_EQ(std::string("with_v6_source_and_destination"),
              config_obj.getRouteFromEntries(connection)->clusterName());
  }

  {
    // fall through
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv6Instance>("2004::");
    connection.remote_address_ = std::make_shared<Network::Address::Ipv6Instance>("::");
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection)->clusterName());
  }
}

// Tests that a deprecated_v1 route gets the top-level endpoint selector.
TEST(ConfigTest, DEPRECATED_FEATURE_TEST(RouteWithTopLevelMetadataMatchConfig)) {
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
  EXPECT_THROW(constructConfigFromV2Yaml(yaml, factory_context), EnvoyException);
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
  Config config_obj(constructConfigFromV2Yaml(yaml, factory_context));

  NiceMock<Network::MockConnection> connection;
  EXPECT_CALL(factory_context.random_, random()).WillOnce(Return(0));
  EXPECT_EQ(std::string("cluster1"), config_obj.getRouteFromEntries(connection)->clusterName());

  EXPECT_CALL(factory_context.random_, random()).WillOnce(Return(2));
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
  Config config_obj(constructConfigFromV2Yaml(yaml, factory_context));

  {
    ProtobufWkt::Value v1, v2;
    v1.set_string_value("v1");
    v2.set_string_value("v2");
    HashedValue hv1(v1), hv2(v2);

    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(factory_context.random_, random()).WillOnce(Return(0));

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
    EXPECT_CALL(factory_context.random_, random()).WillOnce(Return(2));

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
  Config config_obj(constructConfigFromV2Yaml(yaml, factory_context));

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
    EXPECT_CALL(factory_context.random_, random()).WillOnce(Return(0));

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
    EXPECT_CALL(factory_context.random_, random()).WillOnce(Return(2));

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
  Config config_obj(constructConfigFromV2Yaml(yaml, factory_context));

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
  Config config_obj(constructConfigFromV2Yaml(yaml, factory_context));

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
  Config config_obj(constructConfigFromV2Yaml(yaml, factory_context));

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
  Config config_obj(constructConfigFromV2Yaml(yaml, factory_context));

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
  Config config_obj(constructConfigFromV2Yaml(yaml, factory_context));
  EXPECT_NE(nullptr, config_obj.hashPolicy());
}

TEST(ConfigTest, HashWithSourceIpDefaultConfig) {
  const std::string yaml = R"EOF(
  stat_prefix: name
  cluster: foo
)EOF";

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  Config config_obj(constructConfigFromV2Yaml(yaml, factory_context));
  EXPECT_EQ(nullptr, config_obj.hashPolicy());
}

TEST(ConfigTest, AccessLogConfig) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config;
  envoy::config::accesslog::v3::AccessLog* log = config.mutable_access_log()->Add();
  log->set_name(Extensions::AccessLoggers::AccessLogNames::get().File);
  {
    envoy::extensions::access_loggers::file::v3::FileAccessLog file_access_log;
    file_access_log.set_path("some_path");
    file_access_log.mutable_log_format()->set_text_format("the format specifier");
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

class TcpProxyTest : public testing::Test {
public:
  TcpProxyTest() {
    ON_CALL(*factory_context_.access_log_manager_.file_, write(_))
        .WillByDefault(SaveArg<0>(&access_log_data_));
    ON_CALL(filter_callbacks_.connection_.stream_info_, onUpstreamHostSelected(_))
        .WillByDefault(Invoke(
            [this](Upstream::HostDescriptionConstSharedPtr host) { upstream_host_ = host; }));
    ON_CALL(filter_callbacks_.connection_.stream_info_, upstreamHost())
        .WillByDefault(ReturnPointee(&upstream_host_));
  }

  ~TcpProxyTest() override {
    if (filter_ != nullptr) {
      filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
    }
  }

  void configure(const envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy& config) {
    config_ = std::make_shared<Config>(config, factory_context_);
  }

  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy defaultConfig() {
    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config;
    config.set_stat_prefix("name");
    auto* route = config.mutable_hidden_envoy_deprecated_deprecated_v1()->mutable_routes()->Add();
    route->set_cluster("fake_cluster");

    return config;
  }

  // Return the default config, plus one file access log with the specified format
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy
  accessLogConfig(const std::string& access_log_format) {
    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
    envoy::config::accesslog::v3::AccessLog* access_log = config.mutable_access_log()->Add();
    access_log->set_name(Extensions::AccessLoggers::AccessLogNames::get().File);
    envoy::extensions::access_loggers::file::v3::FileAccessLog file_access_log;
    file_access_log.set_path("unused");
    file_access_log.mutable_log_format()->set_text_format(access_log_format);
    access_log->mutable_typed_config()->PackFrom(file_access_log);
    return config;
  }

  void setup(uint32_t connections,
             const envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy& config) {
    configure(config);
    upstream_local_address_ = Network::Utility::resolveUrl("tcp://2.2.2.2:50000");
    upstream_remote_address_ = Network::Utility::resolveUrl("tcp://127.0.0.1:80");
    for (uint32_t i = 0; i < connections; i++) {
      upstream_connections_.push_back(std::make_unique<NiceMock<Network::MockClientConnection>>());
      upstream_connection_data_.push_back(
          std::make_unique<NiceMock<Tcp::ConnectionPool::MockConnectionData>>());
      ON_CALL(*upstream_connection_data_.back(), connection())
          .WillByDefault(ReturnRef(*upstream_connections_.back()));
      upstream_hosts_.push_back(std::make_shared<NiceMock<Upstream::MockHost>>());
      conn_pool_handles_.push_back(
          std::make_unique<NiceMock<Envoy::ConnectionPool::MockCancellable>>());

      ON_CALL(*upstream_hosts_.at(i), cluster())
          .WillByDefault(ReturnPointee(
              factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_));
      ON_CALL(*upstream_hosts_.at(i), address()).WillByDefault(Return(upstream_remote_address_));
      upstream_connections_.at(i)->local_address_ = upstream_local_address_;
      EXPECT_CALL(*upstream_connections_.at(i), dispatcher())
          .WillRepeatedly(ReturnRef(filter_callbacks_.connection_.dispatcher_));
    }

    {
      testing::InSequence sequence;
      for (uint32_t i = 0; i < connections; i++) {
        EXPECT_CALL(factory_context_.cluster_manager_, tcpConnPoolForCluster("fake_cluster", _, _))
            .WillOnce(Return(&conn_pool_))
            .RetiresOnSaturation();
        EXPECT_CALL(conn_pool_, newConnection(_))
            .WillOnce(Invoke(
                [=](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
                  conn_pool_callbacks_.push_back(&cb);

                  return onNewConnection(conn_pool_handles_.at(i).get());
                }))
            .RetiresOnSaturation();
      }
      EXPECT_CALL(factory_context_.cluster_manager_, tcpConnPoolForCluster("fake_cluster", _, _))
          .WillRepeatedly(Return(nullptr));
    }

    {
      filter_ = std::make_unique<Filter>(config_, factory_context_.cluster_manager_);
      EXPECT_CALL(filter_callbacks_.connection_, enableHalfClose(true));
      EXPECT_CALL(filter_callbacks_.connection_, readDisable(true));
      filter_->initializeReadFilterCallbacks(filter_callbacks_);
      filter_callbacks_.connection_.streamInfo().setDownstreamSslConnection(
          filter_callbacks_.connection_.ssl());
      filter_callbacks_.connection_.streamInfo().setDownstreamLocalAddress(
          filter_callbacks_.connection_.localAddress());
      filter_callbacks_.connection_.streamInfo().setDownstreamRemoteAddress(
          filter_callbacks_.connection_.remoteAddress());
      EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

      EXPECT_EQ(absl::optional<uint64_t>(), filter_->computeHashKey());
      EXPECT_EQ(&filter_callbacks_.connection_, filter_->downstreamConnection());
      EXPECT_EQ(nullptr, filter_->metadataMatchCriteria());
    }
  }

  void setup(uint32_t connections) { setup(connections, defaultConfig()); }

  void raiseEventUpstreamConnected(uint32_t conn_index) {
    EXPECT_CALL(filter_callbacks_.connection_, readDisable(false));
    EXPECT_CALL(*upstream_connection_data_.at(conn_index), addUpstreamCallbacks(_))
        .WillOnce(Invoke([=](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void {
          upstream_callbacks_ = &cb;

          // Simulate TCP conn pool upstream callbacks. This is safe because the TCP proxy never
          // releases a connection so all events go to the same UpstreamCallbacks instance.
          upstream_connections_.at(conn_index)->addConnectionCallbacks(cb);
        }));
    EXPECT_CALL(*upstream_connections_.at(conn_index), enableHalfClose(true));
    conn_pool_callbacks_.at(conn_index)
        ->onPoolReady(std::move(upstream_connection_data_.at(conn_index)),
                      upstream_hosts_.at(conn_index));
  }

  void raiseEventUpstreamConnectFailed(uint32_t conn_index,
                                       ConnectionPool::PoolFailureReason reason) {
    conn_pool_callbacks_.at(conn_index)->onPoolFailure(reason, upstream_hosts_.at(conn_index));
  }

  Tcp::ConnectionPool::Cancellable* onNewConnection(Tcp::ConnectionPool::Cancellable* connection) {
    if (!new_connection_functions_.empty()) {
      auto fn = new_connection_functions_.front();
      new_connection_functions_.pop_front();
      return fn(connection);
    }
    return connection;
  }

  Event::TestTimeSystem& timeSystem() { return factory_context_.timeSystem(); }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  ConfigSharedPtr config_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  std::unique_ptr<Filter> filter_;
  std::vector<std::shared_ptr<NiceMock<Upstream::MockHost>>> upstream_hosts_{};
  std::vector<std::unique_ptr<NiceMock<Network::MockClientConnection>>> upstream_connections_{};
  std::vector<std::unique_ptr<NiceMock<Tcp::ConnectionPool::MockConnectionData>>>
      upstream_connection_data_{};
  std::vector<Tcp::ConnectionPool::Callbacks*> conn_pool_callbacks_;
  std::vector<std::unique_ptr<NiceMock<Envoy::ConnectionPool::MockCancellable>>> conn_pool_handles_;
  NiceMock<Tcp::ConnectionPool::MockInstance> conn_pool_;
  Tcp::ConnectionPool::UpstreamCallbacks* upstream_callbacks_;
  StringViewSaver access_log_data_;
  Network::Address::InstanceConstSharedPtr upstream_local_address_;
  Network::Address::InstanceConstSharedPtr upstream_remote_address_;
  std::list<std::function<Tcp::ConnectionPool::Cancellable*(Tcp::ConnectionPool::Cancellable*)>>
      new_connection_functions_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_{};
};

TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(DefaultRoutes)) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();

  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy::WeightedCluster::ClusterWeight*
      ignored_cluster = config.mutable_weighted_clusters()->mutable_clusters()->Add();
  ignored_cluster->set_name("ignored_cluster");
  ignored_cluster->set_weight(10);

  configure(config);

  NiceMock<Network::MockConnection> connection;
  EXPECT_EQ(std::string("fake_cluster"), config_->getRouteFromEntries(connection)->clusterName());
}

// Tests that half-closes are proxied and don't themselves cause any connection to be closed.
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(HalfCloseProxy)) {
  setup(1);

  EXPECT_CALL(filter_callbacks_.connection_, close(_)).Times(0);
  EXPECT_CALL(*upstream_connections_.at(0), close(_)).Times(0);

  raiseEventUpstreamConnected(0);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferEqual(&buffer), true));
  filter_->onData(buffer, true);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), true));
  upstream_callbacks_->onUpstreamData(response, true);

  EXPECT_CALL(filter_callbacks_.connection_, close(_));
  upstream_callbacks_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// Test that downstream is closed after an upstream LocalClose.
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(UpstreamLocalDisconnect)) {
  setup(1);

  raiseEventUpstreamConnected(0);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferEqual(&buffer), false));
  filter_->onData(buffer, false);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), _));
  upstream_callbacks_->onUpstreamData(response, false);

  EXPECT_CALL(filter_callbacks_.connection_, close(_));
  upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
}

// Test that downstream is closed after an upstream RemoteClose.
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(UpstreamRemoteDisconnect)) {
  setup(1);

  raiseEventUpstreamConnected(0);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferEqual(&buffer), false));
  filter_->onData(buffer, false);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), _));
  upstream_callbacks_->onUpstreamData(response, false);

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  upstream_callbacks_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// Test that reconnect is attempted after a local connect failure
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(ConnectAttemptsUpstreamLocalFail)) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_max_connect_attempts()->set_value(2);

  setup(2, config);

  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::LocalConnectionFailure);
  raiseEventUpstreamConnected(1);

  EXPECT_EQ(0U, factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_cx_connect_attempts_exceeded")
                    .value());
}

// Make sure that the tcp proxy code handles reentrant calls to onPoolFailure.
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(ConnectAttemptsUpstreamLocalFailReentrant)) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_max_connect_attempts()->set_value(2);

  // Set up a call to onPoolFailure from inside the first newConnection call.
  // This simulates a connection failure from under the stack of newStream.
  new_connection_functions_.push_back(
      [&](Tcp::ConnectionPool::Cancellable*) -> Tcp::ConnectionPool::Cancellable* {
        raiseEventUpstreamConnectFailed(0,
                                        ConnectionPool::PoolFailureReason::LocalConnectionFailure);
        return nullptr;
      });

  setup(2, config);

  // Make sure the last connection pool to be created is the one which gets the
  // cancellation call.
  EXPECT_CALL(*conn_pool_handles_.at(0), cancel(Tcp::ConnectionPool::CancelPolicy::CloseExcess))
      .Times(0);
  EXPECT_CALL(*conn_pool_handles_.at(1), cancel(Tcp::ConnectionPool::CancelPolicy::CloseExcess))
      .Times(1);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Test that reconnect is attempted after a remote connect failure
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(ConnectAttemptsUpstreamRemoteFail)) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_max_connect_attempts()->set_value(2);
  setup(2, config);

  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  raiseEventUpstreamConnected(1);

  EXPECT_EQ(0U, factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_cx_connect_attempts_exceeded")
                    .value());
}

// Test that reconnect is attempted after a connect timeout
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(ConnectAttemptsUpstreamTimeout)) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_max_connect_attempts()->set_value(2);
  setup(2, config);

  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::Timeout);
  raiseEventUpstreamConnected(1);

  EXPECT_EQ(0U, factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_cx_connect_attempts_exceeded")
                    .value());
}

// Test that only the configured number of connect attempts occur
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(ConnectAttemptsLimit)) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config =
      accessLogConfig("%RESPONSE_FLAGS%");
  config.mutable_max_connect_attempts()->set_value(3);
  setup(3, config);

  EXPECT_CALL(upstream_hosts_.at(0)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  EXPECT_CALL(upstream_hosts_.at(1)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  EXPECT_CALL(upstream_hosts_.at(2)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));

  // Try both failure modes
  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::Timeout);
  raiseEventUpstreamConnectFailed(1, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  raiseEventUpstreamConnectFailed(2, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  filter_.reset();
  EXPECT_EQ(access_log_data_, "UF,URX");
}

// Test that the tcp proxy sends the correct notifications to the outlier detector
TEST_F(TcpProxyTest, OutlierDetection) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_max_connect_attempts()->set_value(3);
  setup(3, config);

  EXPECT_CALL(upstream_hosts_.at(0)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::Timeout);

  EXPECT_CALL(upstream_hosts_.at(1)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  raiseEventUpstreamConnectFailed(1, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  EXPECT_CALL(upstream_hosts_.at(2)->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccessFinal, _));
  raiseEventUpstreamConnected(2);
}

TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(UpstreamDisconnectDownstreamFlowControl)) {
  setup(1);

  raiseEventUpstreamConnected(0);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferEqual(&buffer), _));
  filter_->onData(buffer, false);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), _));
  upstream_callbacks_->onUpstreamData(response, false);

  EXPECT_CALL(*upstream_connections_.at(0), readDisable(true));
  filter_callbacks_.connection_.runHighWatermarkCallbacks();

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  upstream_callbacks_->onEvent(Network::ConnectionEvent::RemoteClose);

  filter_callbacks_.connection_.runLowWatermarkCallbacks();
}

TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(DownstreamDisconnectRemote)) {
  setup(1);

  raiseEventUpstreamConnected(0);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferEqual(&buffer), _));
  filter_->onData(buffer, false);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), _));
  upstream_callbacks_->onUpstreamData(response, false);

  EXPECT_CALL(*upstream_connections_.at(0), close(Network::ConnectionCloseType::FlushWrite));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(DownstreamDisconnectLocal)) {
  setup(1);

  raiseEventUpstreamConnected(0);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*upstream_connections_.at(0), write(BufferEqual(&buffer), _));
  filter_->onData(buffer, false);

  Buffer::OwnedImpl response("world");
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferEqual(&response), _));
  upstream_callbacks_->onUpstreamData(response, false);

  EXPECT_CALL(*upstream_connections_.at(0), close(Network::ConnectionCloseType::NoFlush));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
}

TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(UpstreamConnectTimeout)) {
  setup(1, accessLogConfig("%RESPONSE_FLAGS%"));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::Timeout);

  filter_.reset();
  EXPECT_EQ(access_log_data_, "UF,URX");
}

TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(NoHost)) {
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  setup(0, accessLogConfig("%RESPONSE_FLAGS%"));
  filter_.reset();
  EXPECT_EQ(access_log_data_, "UH");
}

TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(RouteWithMetadataMatch)) {
  auto v1 = ProtobufWkt::Value();
  v1.set_string_value("v1");
  auto v2 = ProtobufWkt::Value();
  v2.set_number_value(2.0);
  auto v3 = ProtobufWkt::Value();
  v3.set_bool_value(true);

  std::vector<Router::MetadataMatchCriterionImpl> criteria = {{"a", v1}, {"b", v2}, {"c", v3}};

  auto metadata_struct = ProtobufWkt::Struct();
  auto mutable_fields = metadata_struct.mutable_fields();

  for (const auto& criterion : criteria) {
    mutable_fields->insert({criterion.name(), criterion.value().value()});
  }

  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_metadata_match()->mutable_filter_metadata()->insert(
      {Envoy::Config::MetadataFilters::get().ENVOY_LB, metadata_struct});

  configure(config);
  filter_ = std::make_unique<Filter>(config_, factory_context_.cluster_manager_);
  filter_->initializeReadFilterCallbacks(filter_callbacks_);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  const auto effective_criteria = filter_->metadataMatchCriteria();
  EXPECT_NE(nullptr, effective_criteria);

  const auto& effective_criterions = effective_criteria->metadataMatchCriteria();
  EXPECT_EQ(effective_criterions.size(), criteria.size());
  for (size_t i = 0; i < criteria.size(); ++i) {
    EXPECT_EQ(effective_criterions[i]->name(), criteria[i].name());
    EXPECT_EQ(effective_criterions[i]->value(), criteria[i].value());
  }
}

// Tests that the endpoint selector of a weighted cluster gets included into the
// LoadBalancerContext.
TEST_F(TcpProxyTest, WeightedClusterWithMetadataMatch) {
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
    - name: cluster2
      weight: 2
      metadata_match:
        filter_metadata:
          envoy.lb:
            k2: v2
  metadata_match:
    filter_metadata:
      envoy.lb:
        k0: v0
)EOF";

  config_ = std::make_shared<Config>(constructConfigFromYaml(yaml, factory_context_));

  ProtobufWkt::Value v0, v1, v2;
  v0.set_string_value("v0");
  v1.set_string_value("v1");
  v2.set_string_value("v2");
  HashedValue hv0(v0), hv1(v1), hv2(v2);

  filter_ = std::make_unique<Filter>(config_, factory_context_.cluster_manager_);
  filter_->initializeReadFilterCallbacks(filter_callbacks_);

  // Expect filter to try to open a connection to cluster1.
  {
    Upstream::LoadBalancerContext* context;

    EXPECT_CALL(factory_context_.random_, random()).WillOnce(Return(0));
    EXPECT_CALL(factory_context_.cluster_manager_, tcpConnPoolForCluster("cluster1", _, _))
        .WillOnce(DoAll(SaveArg<2>(&context), Return(nullptr)));
    EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

    EXPECT_NE(nullptr, context);

    const auto effective_criteria = context->metadataMatchCriteria();
    EXPECT_NE(nullptr, effective_criteria);

    const auto& effective_criterions = effective_criteria->metadataMatchCriteria();
    EXPECT_EQ(2, effective_criterions.size());

    EXPECT_EQ("k0", effective_criterions[0]->name());
    EXPECT_EQ(hv0, effective_criterions[0]->value());

    EXPECT_EQ("k1", effective_criterions[1]->name());
    EXPECT_EQ(hv1, effective_criterions[1]->value());
  }

  // Expect filter to try to open a connection to cluster2.
  {
    Upstream::LoadBalancerContext* context;

    EXPECT_CALL(factory_context_.random_, random()).WillOnce(Return(2));
    EXPECT_CALL(factory_context_.cluster_manager_, tcpConnPoolForCluster("cluster2", _, _))
        .WillOnce(DoAll(SaveArg<2>(&context), Return(nullptr)));
    EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

    EXPECT_NE(nullptr, context);

    const auto effective_criteria = context->metadataMatchCriteria();
    EXPECT_NE(nullptr, effective_criteria);

    const auto& effective_criterions = effective_criteria->metadataMatchCriteria();
    EXPECT_EQ(2, effective_criterions.size());

    EXPECT_EQ("k0", effective_criterions[0]->name());
    EXPECT_EQ(hv0, effective_criterions[0]->value());

    EXPECT_EQ("k2", effective_criterions[1]->name());
    EXPECT_EQ(hv2, effective_criterions[1]->value());
  }
}

TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(DisconnectBeforeData)) {
  configure(defaultConfig());
  filter_ = std::make_unique<Filter>(config_, factory_context_.cluster_manager_);
  filter_->initializeReadFilterCallbacks(filter_callbacks_);

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Test that if the downstream connection is closed before the upstream connection
// is established, the upstream connection is cancelled.
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(RemoteClosedBeforeUpstreamConnected)) {
  setup(1);
  EXPECT_CALL(*conn_pool_handles_.at(0), cancel(Tcp::ConnectionPool::CancelPolicy::CloseExcess));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Test that if the downstream connection is closed before the upstream connection
// is established, the upstream connection is cancelled.
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(LocalClosetBeforeUpstreamConnected)) {
  setup(1);
  EXPECT_CALL(*conn_pool_handles_.at(0), cancel(Tcp::ConnectionPool::CancelPolicy::CloseExcess));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
}

TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(UpstreamConnectFailure)) {
  setup(1, accessLogConfig("%RESPONSE_FLAGS%"));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  raiseEventUpstreamConnectFailed(0, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  filter_.reset();
  EXPECT_EQ(access_log_data_, "UF,URX");
}

TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(UpstreamConnectionLimit)) {
  configure(accessLogConfig("%RESPONSE_FLAGS%"));
  factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->resetResourceManager(
      0, 0, 0, 0, 0);

  // setup sets up expectation for tcpConnForCluster but this test is expected to NOT call that
  filter_ = std::make_unique<Filter>(config_, factory_context_.cluster_manager_);
  // The downstream connection closes if the proxy can't make an upstream connection.
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  filter_->initializeReadFilterCallbacks(filter_callbacks_);
  filter_->onNewConnection();

  filter_.reset();
  EXPECT_EQ(access_log_data_, "UO");
}

// Tests that the idle timer closes both connections, and gets updated when either
// connection has activity.
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(IdleTimeout)) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);
  setup(1, config);

  Event::MockTimer* idle_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  raiseEventUpstreamConnected(0);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  filter_->onData(buffer, false);

  buffer.add("hello2");
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  upstream_callbacks_->onUpstreamData(buffer, false);

  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  filter_callbacks_.connection_.raiseBytesSentCallbacks(1);

  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  upstream_connections_.at(0)->raiseBytesSentCallbacks(2);

  EXPECT_CALL(*upstream_connections_.at(0), close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*idle_timer, disableTimer());
  idle_timer->invokeCallback();
}

// Tests that the idle timer is disabled when the downstream connection is closed.
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(IdleTimerDisabledDownstreamClose)) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);
  setup(1, config);

  Event::MockTimer* idle_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  raiseEventUpstreamConnected(0);

  EXPECT_CALL(*idle_timer, disableTimer());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Tests that the idle timer is disabled when the upstream connection is closed.
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(IdleTimerDisabledUpstreamClose)) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);
  setup(1, config);

  Event::MockTimer* idle_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  raiseEventUpstreamConnected(0);

  EXPECT_CALL(*idle_timer, disableTimer());
  upstream_callbacks_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// Tests that flushing data during an idle timeout doesn't cause problems.
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(IdleTimeoutWithOutstandingDataFlushed)) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);
  setup(1, config);

  Event::MockTimer* idle_timer = new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  raiseEventUpstreamConnected(0);

  Buffer::OwnedImpl buffer("hello");
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  filter_->onData(buffer, false);

  buffer.add("hello2");
  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  upstream_callbacks_->onUpstreamData(buffer, false);

  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  filter_callbacks_.connection_.raiseBytesSentCallbacks(1);

  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  upstream_connections_.at(0)->raiseBytesSentCallbacks(2);

  // Mark the upstream connection as blocked.
  // This should read-disable the downstream connection.
  EXPECT_CALL(filter_callbacks_.connection_, readDisable(_));
  upstream_connections_.at(0)->runHighWatermarkCallbacks();

  // When Envoy has an idle timeout, the following happens.
  // Envoy closes the downstream connection
  // Envoy closes the upstream connection.
  // When closing the upstream connection with ConnectionCloseType::NoFlush,
  // if there is data in the buffer, Envoy does a best-effort flush.
  // If the write succeeds, Envoy may go under the flow control limit and start
  // the callbacks to read-enable the already-closed downstream connection.
  //
  // In this case we expect readDisable to not be called on the already closed
  // connection.
  EXPECT_CALL(filter_callbacks_.connection_, readDisable(true)).Times(0);
  EXPECT_CALL(*upstream_connections_.at(0), close(Network::ConnectionCloseType::NoFlush))
      .WillOnce(InvokeWithoutArgs(
          [&]() -> void { upstream_connections_.at(0)->runLowWatermarkCallbacks(); }));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*idle_timer, disableTimer());
  idle_timer->invokeCallback();
}

// Test that access log fields %UPSTREAM_HOST% and %UPSTREAM_CLUSTER% are correctly logged.
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(AccessLogUpstreamHost)) {
  setup(1, accessLogConfig("%UPSTREAM_HOST% %UPSTREAM_CLUSTER%"));
  raiseEventUpstreamConnected(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();
  EXPECT_EQ(access_log_data_, "127.0.0.1:80 fake_cluster");
}

// Test that access log field %UPSTREAM_LOCAL_ADDRESS% is correctly logged.
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(AccessLogUpstreamLocalAddress)) {
  setup(1, accessLogConfig("%UPSTREAM_LOCAL_ADDRESS%"));
  raiseEventUpstreamConnected(0);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();
  EXPECT_EQ(access_log_data_, "2.2.2.2:50000");
}

// Test that access log fields %DOWNSTREAM_PEER_URI_SAN% is correctly logged.
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(AccessLogPeerUriSan)) {
  filter_callbacks_.connection_.local_address_ =
      Network::Utility::resolveUrl("tcp://1.1.1.2:20000");
  filter_callbacks_.connection_.remote_address_ =
      Network::Utility::resolveUrl("tcp://1.1.1.1:40000");

  const std::vector<std::string> uriSan{"someSan"};
  auto mockConnectionInfo = std::make_shared<Ssl::MockConnectionInfo>();
  EXPECT_CALL(*mockConnectionInfo, uriSanPeerCertificate()).WillOnce(Return(uriSan));
  EXPECT_CALL(filter_callbacks_.connection_, ssl()).WillRepeatedly(Return(mockConnectionInfo));

  setup(1, accessLogConfig("%DOWNSTREAM_PEER_URI_SAN%"));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();
  EXPECT_EQ(access_log_data_, "someSan");
}

// Test that access log fields %DOWNSTREAM_TLS_SESSION_ID% is correctly logged.
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(AccessLogTlsSessionId)) {
  filter_callbacks_.connection_.local_address_ =
      Network::Utility::resolveUrl("tcp://1.1.1.2:20000");
  filter_callbacks_.connection_.remote_address_ =
      Network::Utility::resolveUrl("tcp://1.1.1.1:40000");

  const std::string tlsSessionId{
      "D62A523A65695219D46FE1FFE285A4C371425ACE421B110B5B8D11D3EB4D5F0B"};
  auto mockConnectionInfo = std::make_shared<Ssl::MockConnectionInfo>();
  EXPECT_CALL(*mockConnectionInfo, sessionId()).WillOnce(ReturnRef(tlsSessionId));
  EXPECT_CALL(filter_callbacks_.connection_, ssl()).WillRepeatedly(Return(mockConnectionInfo));

  setup(1, accessLogConfig("%DOWNSTREAM_TLS_SESSION_ID%"));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();
  EXPECT_EQ(access_log_data_, "D62A523A65695219D46FE1FFE285A4C371425ACE421B110B5B8D11D3EB4D5F0B");
}

// Test that access log fields %DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT% and
// %DOWNSTREAM_LOCAL_ADDRESS% are correctly logged.
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(AccessLogDownstreamAddress)) {
  filter_callbacks_.connection_.local_address_ =
      Network::Utility::resolveUrl("tcp://1.1.1.2:20000");
  filter_callbacks_.connection_.remote_address_ =
      Network::Utility::resolveUrl("tcp://1.1.1.1:40000");
  setup(1, accessLogConfig("%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT% %DOWNSTREAM_LOCAL_ADDRESS%"));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();
  EXPECT_EQ(access_log_data_, "1.1.1.1 1.1.1.2:20000");
}

TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(AccessLogUpstreamSSLConnection)) {
  setup(1);

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  const std::string session_id = "D62A523A65695219D46FE1FFE285A4C371425ACE421B110B5B8D11D3EB4D5F0B";
  auto ssl_info = std::make_shared<Ssl::MockConnectionInfo>();
  EXPECT_CALL(*ssl_info, sessionId()).WillRepeatedly(ReturnRef(session_id));
  stream_info.setDownstreamSslConnection(ssl_info);
  EXPECT_CALL(*upstream_connections_.at(0), streamInfo()).WillRepeatedly(ReturnRef(stream_info));

  raiseEventUpstreamConnected(0);
  ASSERT_NE(nullptr, filter_->getStreamInfo().upstreamSslConnection());
  EXPECT_EQ(session_id, filter_->getStreamInfo().upstreamSslConnection()->sessionId());
}

// Tests that upstream flush works properly with no idle timeout configured.
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(UpstreamFlushNoTimeout)) {
  setup(1);
  raiseEventUpstreamConnected(0);

  EXPECT_CALL(*upstream_connections_.at(0),
              close(Network::ConnectionCloseType::FlushWrite))
      .WillOnce(Return()); // Cancel default action of raising LocalClose
  EXPECT_CALL(*upstream_connections_.at(0), state())
      .WillOnce(Return(Network::Connection::State::Closing));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();

  EXPECT_EQ(1U, config_->stats().upstream_flush_active_.value());

  // Send some bytes; no timeout configured so this should be a no-op (not a crash).
  upstream_connections_.at(0)->raiseBytesSentCallbacks(1);

  // Simulate flush complete.
  upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
  EXPECT_EQ(1U, config_->stats().upstream_flush_total_.value());
  EXPECT_EQ(0U, config_->stats().upstream_flush_active_.value());
}

// Tests that upstream flush works with an idle timeout configured, but the connection
// finishes draining before the timer expires.
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(UpstreamFlushTimeoutConfigured)) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);
  setup(1, config);

  NiceMock<Event::MockTimer>* idle_timer =
      new NiceMock<Event::MockTimer>(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*idle_timer, enableTimer(_, _));
  raiseEventUpstreamConnected(0);

  EXPECT_CALL(*upstream_connections_.at(0),
              close(Network::ConnectionCloseType::FlushWrite))
      .WillOnce(Return()); // Cancel default action of raising LocalClose
  EXPECT_CALL(*upstream_connections_.at(0), state())
      .WillOnce(Return(Network::Connection::State::Closing));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  filter_.reset();
  EXPECT_EQ(1U, config_->stats().upstream_flush_active_.value());

  EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(1000), _));
  upstream_connections_.at(0)->raiseBytesSentCallbacks(1);

  // Simulate flush complete.
  EXPECT_CALL(*idle_timer, disableTimer());
  upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
  EXPECT_EQ(1U, config_->stats().upstream_flush_total_.value());
  EXPECT_EQ(0U, config_->stats().upstream_flush_active_.value());
  EXPECT_EQ(0U, config_->stats().idle_timeout_.value());
}

// Tests that upstream flush closes the connection when the idle timeout fires.
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(UpstreamFlushTimeoutExpired)) {
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy config = defaultConfig();
  config.mutable_idle_timeout()->set_seconds(1);
  setup(1, config);

  NiceMock<Event::MockTimer>* idle_timer =
      new NiceMock<Event::MockTimer>(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*idle_timer, enableTimer(_, _));
  raiseEventUpstreamConnected(0);

  EXPECT_CALL(*upstream_connections_.at(0),
              close(Network::ConnectionCloseType::FlushWrite))
      .WillOnce(Return()); // Cancel default action of raising LocalClose
  EXPECT_CALL(*upstream_connections_.at(0), state())
      .WillOnce(Return(Network::Connection::State::Closing));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);

  filter_.reset();
  EXPECT_EQ(1U, config_->stats().upstream_flush_active_.value());

  EXPECT_CALL(*upstream_connections_.at(0), close(Network::ConnectionCloseType::NoFlush));
  idle_timer->invokeCallback();
  EXPECT_EQ(1U, config_->stats().upstream_flush_total_.value());
  EXPECT_EQ(0U, config_->stats().upstream_flush_active_.value());
  EXPECT_EQ(1U, config_->stats().idle_timeout_.value());
}

// Tests that upstream flush will close a connection if it reads data from the upstream
// connection after the downstream connection is closed (nowhere to send it).
TEST_F(TcpProxyTest, DEPRECATED_FEATURE_TEST(UpstreamFlushReceiveUpstreamData)) {
  setup(1);
  raiseEventUpstreamConnected(0);

  EXPECT_CALL(*upstream_connections_.at(0),
              close(Network::ConnectionCloseType::FlushWrite))
      .WillOnce(Return()); // Cancel default action of raising LocalClose
  EXPECT_CALL(*upstream_connections_.at(0), state())
      .WillOnce(Return(Network::Connection::State::Closing));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
  filter_.reset();

  EXPECT_EQ(1U, config_->stats().upstream_flush_active_.value());

  // Send some bytes; no timeout configured so this should be a no-op (not a crash).
  Buffer::OwnedImpl buffer("a");
  EXPECT_CALL(*upstream_connections_.at(0), close(Network::ConnectionCloseType::NoFlush));
  upstream_callbacks_->onUpstreamData(buffer, false);
}

// Tests that downstream connection can access upstream connections filter state.
TEST_F(TcpProxyTest, ShareFilterState) {
  setup(1);

  upstream_connections_.at(0)->streamInfo().filterState()->setData(
      "envoy.tcp_proxy.cluster", std::make_unique<PerConnectionCluster>("filter_state_cluster"),
      StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Connection);
  raiseEventUpstreamConnected(0);
  EXPECT_EQ("filter_state_cluster",
            filter_callbacks_.connection_.streamInfo()
                .upstreamFilterState()
                ->getDataReadOnly<PerConnectionCluster>("envoy.tcp_proxy.cluster")
                .value());
}

// Tests that filter callback can access downstream and upstream address and ssl properties.
TEST_F(TcpProxyTest, AccessDownstreamAndUpstreamProperties) {
  setup(1);

  raiseEventUpstreamConnected(0);
  EXPECT_EQ(filter_callbacks_.connection().streamInfo().downstreamLocalAddress(),
            filter_callbacks_.connection().localAddress());
  EXPECT_EQ(filter_callbacks_.connection().streamInfo().downstreamRemoteAddress(),
            filter_callbacks_.connection().remoteAddress());
  EXPECT_EQ(filter_callbacks_.connection().streamInfo().downstreamSslConnection(),
            filter_callbacks_.connection().ssl());
  EXPECT_EQ(filter_callbacks_.connection().streamInfo().upstreamLocalAddress(),
            upstream_connections_.at(0)->localAddress());
  EXPECT_EQ(filter_callbacks_.connection().streamInfo().upstreamSslConnection(),
            upstream_connections_.at(0)->streamInfo().downstreamSslConnection());
}

class TcpProxyRoutingTest : public testing::Test {
public:
  TcpProxyRoutingTest() = default;

  void setup(bool avoid_boosting = true) {
    const std::string yaml = R"EOF(
    stat_prefix: name
    cluster: fallback_cluster
    deprecated_v1:
      routes:
      - destination_ports: 1-9999
        cluster: fake_cluster
    )EOF";

    config_ =
        std::make_shared<Config>(constructConfigFromYaml(yaml, factory_context_, avoid_boosting));
  }

  void initializeFilter() {
    EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

    filter_ = std::make_unique<Filter>(config_, factory_context_.cluster_manager_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  Event::TestTimeSystem& timeSystem() { return factory_context_.timeSystem(); }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  ConfigSharedPtr config_;
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  std::unique_ptr<Filter> filter_;
};

TEST_F(TcpProxyRoutingTest, DEPRECATED_FEATURE_TEST(NonRoutableConnection)) {
  setup(false);

  const uint32_t total_cx = config_->stats().downstream_cx_total_.value();
  const uint32_t non_routable_cx = config_->stats().downstream_cx_no_route_.value();

  initializeFilter();

  // Port 10000 is outside the specified destination port range.
  connection_.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 10000);

  // Expect filter to try to open a connection to the fallback cluster.
  EXPECT_CALL(factory_context_.cluster_manager_, tcpConnPoolForCluster("fallback_cluster", _, _))
      .WillOnce(Return(nullptr));

  filter_->onNewConnection();

  EXPECT_EQ(total_cx + 1, config_->stats().downstream_cx_total_.value());
  EXPECT_EQ(non_routable_cx, config_->stats().downstream_cx_no_route_.value());
}

TEST_F(TcpProxyRoutingTest, DEPRECATED_FEATURE_TEST(RoutableConnection)) {
  setup(false);

  const uint32_t total_cx = config_->stats().downstream_cx_total_.value();
  const uint32_t non_routable_cx = config_->stats().downstream_cx_no_route_.value();

  initializeFilter();

  // Port 9999 is within the specified destination port range.
  connection_.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 9999);

  // Expect filter to try to open a connection to specified cluster.
  EXPECT_CALL(factory_context_.cluster_manager_, tcpConnPoolForCluster("fake_cluster", _, _))
      .WillOnce(Return(nullptr));

  filter_->onNewConnection();

  EXPECT_EQ(total_cx + 1, config_->stats().downstream_cx_total_.value());
  EXPECT_EQ(non_routable_cx, config_->stats().downstream_cx_no_route_.value());
}

// Test that the tcp proxy uses the cluster from FilterState if set
TEST_F(TcpProxyRoutingTest, DEPRECATED_FEATURE_TEST(UseClusterFromPerConnectionCluster)) {
  setup(false);
  initializeFilter();

  connection_.streamInfo().filterState()->setData(
      "envoy.tcp_proxy.cluster", std::make_unique<PerConnectionCluster>("filter_state_cluster"),
      StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Connection);

  // Expect filter to try to open a connection to specified cluster.
  EXPECT_CALL(factory_context_.cluster_manager_,
              tcpConnPoolForCluster("filter_state_cluster", _, _))
      .WillOnce(Return(nullptr));

  filter_->onNewConnection();
}

// Test that the tcp proxy forwards the requested server name from FilterState if set
TEST_F(TcpProxyRoutingTest, DEPRECATED_FEATURE_TEST(UpstreamServerName)) {
  setup(false);
  initializeFilter();

  connection_.streamInfo().filterState()->setData(
      "envoy.network.upstream_server_name", std::make_unique<UpstreamServerName>("www.example.com"),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Connection);

  // Expect filter to try to open a connection to a cluster with the transport socket options with
  // override-server-name
  EXPECT_CALL(factory_context_.cluster_manager_, tcpConnPoolForCluster(_, _, _))
      .WillOnce(
          Invoke([](const std::string& cluster, Upstream::ResourcePriority,
                    Upstream::LoadBalancerContext* context) -> Tcp::ConnectionPool::Instance* {
            EXPECT_EQ(cluster, "fake_cluster");
            Network::TransportSocketOptionsSharedPtr transport_socket_options =
                context->upstreamTransportSocketOptions();
            EXPECT_NE(transport_socket_options, nullptr);
            EXPECT_TRUE(transport_socket_options->serverNameOverride().has_value());
            EXPECT_EQ(transport_socket_options->serverNameOverride().value(), "www.example.com");
            return nullptr;
          }));

  // Port 9999 is within the specified destination port range.
  connection_.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 9999);

  filter_->onNewConnection();
}

// Test that the tcp proxy override ALPN from FilterState if set
TEST_F(TcpProxyRoutingTest, DEPRECATED_FEATURE_TEST(ApplicationProtocols)) {
  setup(false);
  initializeFilter();

  connection_.streamInfo().filterState()->setData(
      Network::ApplicationProtocols::key(),
      std::make_unique<Network::ApplicationProtocols>(std::vector<std::string>{"foo", "bar"}),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Connection);

  // Expect filter to try to open a connection to a cluster with the transport socket options with
  // override-application-protocol
  EXPECT_CALL(factory_context_.cluster_manager_, tcpConnPoolForCluster(_, _, _))
      .WillOnce(
          Invoke([](const std::string& cluster, Upstream::ResourcePriority,
                    Upstream::LoadBalancerContext* context) -> Tcp::ConnectionPool::Instance* {
            EXPECT_EQ(cluster, "fake_cluster");
            Network::TransportSocketOptionsSharedPtr transport_socket_options =
                context->upstreamTransportSocketOptions();
            EXPECT_NE(transport_socket_options, nullptr);
            EXPECT_FALSE(transport_socket_options->applicationProtocolListOverride().empty());
            EXPECT_EQ(transport_socket_options->applicationProtocolListOverride().size(), 2);
            EXPECT_EQ(transport_socket_options->applicationProtocolListOverride()[0], "foo");
            EXPECT_EQ(transport_socket_options->applicationProtocolListOverride()[1], "bar");
            return nullptr;
          }));

  // Port 9999 is within the specified destination port range.
  connection_.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 9999);

  filter_->onNewConnection();
}

class TcpProxyNonDeprecatedConfigRoutingTest : public TcpProxyRoutingTest {
public:
  TcpProxyNonDeprecatedConfigRoutingTest() = default;

  void setup() {
    const std::string yaml = R"EOF(
    stat_prefix: name
    cluster: fake_cluster
    )EOF";

    config_ = std::make_shared<Config>(constructConfigFromYaml(yaml, factory_context_));
  }
};

TEST_F(TcpProxyNonDeprecatedConfigRoutingTest, ClusterNameSet) {
  setup();

  initializeFilter();

  // Port 9999 is within the specified destination port range.
  connection_.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 9999);

  // Expect filter to try to open a connection to specified cluster.
  EXPECT_CALL(factory_context_.cluster_manager_, tcpConnPoolForCluster("fake_cluster", _, _))
      .WillOnce(Return(nullptr));
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
  TcpProxyHashingTest() = default;

  void setup() {
    const std::string yaml = R"EOF(
    stat_prefix: name
    cluster: fake_cluster
    hash_policy:
    - source_ip: {}
    )EOF";

    config_ = std::make_shared<Config>(constructConfigFromYaml(yaml, factory_context_));
  }

  void initializeFilter() {
    EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

    filter_ = std::make_unique<Filter>(config_, factory_context_.cluster_manager_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  Event::TestTimeSystem& timeSystem() { return factory_context_.timeSystem(); }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  ConfigSharedPtr config_;
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  std::unique_ptr<Filter> filter_;
};

// Test TCP proxy use source IP to hash.
TEST_F(TcpProxyHashingTest, HashWithSourceIp) {
  setup();
  initializeFilter();
  EXPECT_CALL(factory_context_.cluster_manager_, tcpConnPoolForCluster(_, _, _))
      .WillOnce(
          Invoke([](const std::string& cluster, Upstream::ResourcePriority,
                    Upstream::LoadBalancerContext* context) -> Tcp::ConnectionPool::Instance* {
            EXPECT_EQ(cluster, "fake_cluster");
            EXPECT_TRUE(context->computeHashKey().has_value());
            return nullptr;
          }));

  connection_.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 1111);
  connection_.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("2.3.4.5", 2222);

  filter_->onNewConnection();
}

} // namespace
} // namespace TcpProxy
} // namespace Envoy
