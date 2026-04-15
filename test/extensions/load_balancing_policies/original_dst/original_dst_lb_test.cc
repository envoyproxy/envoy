#include <memory>
#include <string>

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/filter_state_dst_address.h"
#include "source/common/network/utility.h"
#include "source/common/singleton/manager_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/clusters/original_dst/original_dst_cluster.h"
#include "source/server/transport_socket_config_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {

class TestLoadBalancerContext : public LoadBalancerContextBase {
public:
  TestLoadBalancerContext(const Network::Connection* connection)
      : TestLoadBalancerContext(connection, nullptr) {}
  TestLoadBalancerContext(const Network::Connection* connection,
                          StreamInfo::StreamInfo* request_stream_info)
      : connection_(connection), request_stream_info_(request_stream_info) {}
  TestLoadBalancerContext(const Network::Connection* connection, const std::string& key,
                          const std::string& value)
      : TestLoadBalancerContext(connection) {
    downstream_headers_ =
        Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{{key, value}}};
  }

  absl::optional<uint64_t> computeHashKey() override { return 0; }
  const Network::Connection* downstreamConnection() const override { return connection_; }
  StreamInfo::StreamInfo* requestStreamInfo() const override { return request_stream_info_; }
  const Http::RequestHeaderMap* downstreamHeaders() const override {
    return downstream_headers_.get();
  }

  const Network::Connection* connection_;
  StreamInfo::StreamInfo* request_stream_info_;
  Http::RequestHeaderMapPtr downstream_headers_;
};

class OriginalDstLbTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  OriginalDstLbTest() = default;

  void setupFromYaml(const std::string& yaml, bool expect_success = true) {
    if (expect_success) {
      cleanup_timer_ = new Event::MockTimer(&server_context_.dispatcher_);
      EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
    }
    setup(parseClusterFromV3Yaml(yaml));
  }

  void setup(const envoy::config::cluster::v3::Cluster& cluster_config) {
    Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr,
                                                               false);

    OriginalDstClusterFactory factory;
    auto status_or_pair = factory.createClusterImpl(cluster_config, factory_context);
    THROW_IF_NOT_OK_REF(status_or_pair.status());

    cluster_ = std::dynamic_pointer_cast<OriginalDstCluster>(status_or_pair.value().first);
    priority_update_cb_ = cluster_->prioritySet().addPriorityUpdateCb(
        [&](uint32_t, const HostVector&, const HostVector&) {
          membership_updated_.ready();
          return absl::OkStatus();
        });
    ON_CALL(initialized_, ready()).WillByDefault(testing::Invoke([this] {
      init_complete_ = true;
    }));
    cluster_->initialize([&]() {
      initialized_.ready();
      return absl::OkStatus();
    });
    handle_ = std::make_shared<OriginalDstClusterHandle>(cluster_);
  }

  void TearDown() override {
    if (init_complete_) {
      EXPECT_CALL(server_context_.dispatcher_, post(_));
      EXPECT_CALL(*cleanup_timer_, disableTimer());
    }
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Stats::TestUtil::TestStore& stats_store_ = server_context_.store_;

  std::shared_ptr<OriginalDstCluster> cluster_;
  OriginalDstClusterHandleSharedPtr handle_;
  ReadyWatcher membership_updated_;
  ReadyWatcher initialized_;
  Event::MockTimer* cleanup_timer_;
  Common::CallbackHandlePtr priority_update_cb_;
  bool init_complete_{false};
};

namespace {

TEST_F(OriginalDstLbTest, NoContext) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.125s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  // No downstream connection => no host.
  {
    TestLoadBalancerContext lb_context(nullptr);
    OriginalDstCluster::LoadBalancer lb(handle_);
    EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
    HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
    EXPECT_EQ(host, nullptr);
  }

  // Verify auxiliary LB methods.
  {
    OriginalDstCluster::LoadBalancer lb(handle_);
    EXPECT_EQ(nullptr, lb.peekAnotherHost(nullptr));
    EXPECT_FALSE(lb.lifetimeCallbacks().has_value());
    std::vector<uint8_t> hash_key;
    auto mock_host = std::make_shared<NiceMock<MockHost>>();
    EXPECT_FALSE(lb.selectExistingConnection(nullptr, *mock_host, hash_key).has_value());
  }
}

TEST_F(OriginalDstLbTest, NoOriginalDst) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.125s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  // Downstream connection not using original dst (localAddressRestored = false) => no host.
  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);

  OriginalDstCluster::LoadBalancer lb(handle_);
  EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
  HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
  EXPECT_EQ(host, nullptr);
}

TEST_F(OriginalDstLbTest, NonIpAddress) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.125s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      *Network::Address::PipeInstance::create("unix://foo"));

  OriginalDstCluster::LoadBalancer lb(handle_);
  EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
  HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
  EXPECT_EQ(host, nullptr);
}

TEST_F(OriginalDstLbTest, ChooseHostFromRestoredAddress) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11"));

  EXPECT_CALL(membership_updated_, ready());
  Event::PostCb post_cb;
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  OriginalDstCluster::LoadBalancer lb(handle_);
  HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
  post_cb();

  ASSERT_NE(host, nullptr);
  EXPECT_EQ(*connection.connectionInfoProvider().localAddress(), *host->address());
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

TEST_F(OriginalDstLbTest, ChooseHostCacheHit) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11"));

  EXPECT_CALL(membership_updated_, ready());
  Event::PostCb post_cb;
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host1 = OriginalDstCluster::LoadBalancer(handle_).chooseHost(&lb_context).host;
  post_cb();
  ASSERT_NE(host1, nullptr);

  // Same host returned on 2nd call (cache hit).
  HostConstSharedPtr host2 = OriginalDstCluster::LoadBalancer(handle_).chooseHost(&lb_context).host;
  EXPECT_EQ(host2, host1);
}

TEST_F(OriginalDstLbTest, ChooseHostIpv6) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv6Instance>("FD00::1"));

  EXPECT_CALL(membership_updated_, ready());
  Event::PostCb post_cb;
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  OriginalDstCluster::LoadBalancer lb(handle_);
  HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
  post_cb();

  ASSERT_NE(host, nullptr);
  EXPECT_EQ(*connection.connectionInfoProvider().localAddress(), *host->address());
}

TEST_F(OriginalDstLbTest, MultipleDistinctHosts) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  NiceMock<Network::MockConnection> connection1;
  TestLoadBalancerContext lb_context1(&connection1);
  connection1.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11"));

  NiceMock<Network::MockConnection> connection2;
  TestLoadBalancerContext lb_context2(&connection2);
  connection2.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.12"));

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host1 = lb.chooseHost(&lb_context1).host;
  post_cb();
  ASSERT_NE(host1, nullptr);
  EXPECT_EQ(*connection1.connectionInfoProvider().localAddress(), *host1->address());

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host2 = lb.chooseHost(&lb_context2).host;
  post_cb();
  ASSERT_NE(host2, nullptr);
  EXPECT_EQ(*connection2.connectionInfoProvider().localAddress(), *host2->address());

  EXPECT_NE(host1, host2);
  EXPECT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

TEST_F(OriginalDstLbTest, UseHttpHeaderEnabled) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      use_http_header: true
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  // HTTP header override.
  TestLoadBalancerContext lb_context1(nullptr, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host1 = lb.chooseHost(&lb_context1).host;
  post_cb();
  ASSERT_NE(host1, nullptr);
  EXPECT_EQ("127.0.0.1:5555", host1->address()->asString());

  // HTTP header override with connection present.
  NiceMock<Network::MockConnection> connection2;
  TestLoadBalancerContext lb_context2(&connection2, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "127.0.0.1:5556");

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host2 = lb.chooseHost(&lb_context2).host;
  post_cb();
  ASSERT_NE(host2, nullptr);
  EXPECT_EQ("127.0.0.1:5556", host2->address()->asString());
}

TEST_F(OriginalDstLbTest, HttpHeaderInvalidValues) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      use_http_header: true
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);

  // Empty header value.
  TestLoadBalancerContext lb_context1(nullptr, Http::Headers::get().EnvoyOriginalDstHost.get(), "");
  EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
  HostConstSharedPtr host1 = lb.chooseHost(&lb_context1).host;
  EXPECT_EQ(host1, nullptr);
  EXPECT_EQ(
      1, TestUtility::findCounter(stats_store_, "cluster.name.original_dst_host_invalid")->value());

  // Invalid IP.
  TestLoadBalancerContext lb_context2(nullptr, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "a.b.c.d");
  EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
  HostConstSharedPtr host2 = lb.chooseHost(&lb_context2).host;
  EXPECT_EQ(host2, nullptr);
  EXPECT_EQ(
      2, TestUtility::findCounter(stats_store_, "cluster.name.original_dst_host_invalid")->value());
}

TEST_F(OriginalDstLbTest, UseHttpHeaderDisabledIgnoresHeader) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);

  // Header present but use_http_header disabled — uses connection's restored address instead.
  NiceMock<Network::MockConnection> connection;
  connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11"));
  TestLoadBalancerContext lb_context(&connection, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                     "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready());
  Event::PostCb post_cb;
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
  post_cb();
  ASSERT_NE(host, nullptr);
  // Uses restored local address, not the header value.
  EXPECT_EQ(*connection.connectionInfoProvider().localAddress(), *host->address());
}

TEST_F(OriginalDstLbTest, UseCustomHttpHeader) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      use_http_header: true
      http_header_name: ":authority"
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  // Custom header override by :authority.
  TestLoadBalancerContext lb_context(nullptr, Http::Headers::get().Host.get(), "127.0.0.1:6666");
  lb_context.downstream_headers_->setCopy(Http::Headers::get().EnvoyOriginalDstHost,
                                           "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
  post_cb();
  ASSERT_NE(host, nullptr);
  // Uses :authority value, not x-envoy-original-dst-host.
  EXPECT_EQ("127.0.0.1:6666", host->address()->asString());
}

TEST_F(OriginalDstLbTest, FilterStateTakesPriority) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      use_http_header: true
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  NiceMock<Network::MockConnection> connection;
  connection.stream_info_.filterState()->setData(
      Upstream::OriginalDstClusterFilterStateKey,
      std::make_shared<Network::AddressObject>(
          std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11", 6666)),
      StreamInfo::FilterState::StateType::ReadOnly);
  TestLoadBalancerContext lb_context(&connection, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                     "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
  post_cb();
  ASSERT_NE(host, nullptr);
  // Filter state address takes priority over header.
  EXPECT_EQ("10.10.11.11:6666", host->address()->asString());
}

TEST_F(OriginalDstLbTest, MetadataKeyOverride) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      metadata_key:
        key: xxx
        path:
        - key: a
        - key: b
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);

  envoy::config::core::v3::Metadata dynamic_metadata;
  (*(*(*dynamic_metadata.mutable_filter_metadata())["xxx"].mutable_fields())["a"]
        .mutable_struct_value()
        ->mutable_fields())["b"]
      .set_string_value("127.0.0.1:6666");

  EXPECT_CALL(Const(connection.stream_info_), dynamicMetadata())
      .WillRepeatedly(ReturnRef(dynamic_metadata));

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
  post_cb();
  ASSERT_NE(host, nullptr);
  EXPECT_EQ("127.0.0.1:6666", host->address()->asString());
}

TEST_F(OriginalDstLbTest, MetadataKeyWithRequestStreamInfo) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      metadata_key:
        key: xxx
        path:
        - key: a
        - key: b
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  NiceMock<Network::MockConnection> connection;
  NiceMock<StreamInfo::MockStreamInfo> request_stream_info;
  TestLoadBalancerContext lb_context(&connection, &request_stream_info);

  envoy::config::core::v3::Metadata req_dynamic_metadata;
  (*(*(*req_dynamic_metadata.mutable_filter_metadata())["xxx"].mutable_fields())["a"]
        .mutable_struct_value()
        ->mutable_fields())["b"]
      .set_string_value("127.0.0.1:7777");
  envoy::config::core::v3::Metadata conn_dynamic_metadata;
  (*(*(*conn_dynamic_metadata.mutable_filter_metadata())["xxx"].mutable_fields())["a"]
        .mutable_struct_value()
        ->mutable_fields())["b"]
      .set_string_value("127.0.0.1:8888");

  EXPECT_CALL(Const(request_stream_info), dynamicMetadata())
      .WillRepeatedly(ReturnRef(req_dynamic_metadata));
  EXPECT_CALL(Const(connection.stream_info_), dynamicMetadata())
      .WillRepeatedly(ReturnRef(conn_dynamic_metadata));

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
  post_cb();
  ASSERT_NE(host, nullptr);
  // Request stream info takes priority over connection stream info.
  EXPECT_EQ("127.0.0.1:7777", host->address()->asString());
}

TEST_F(OriginalDstLbTest, MetadataKeyInvalidValue) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      metadata_key:
        key: xxx
        path:
        - key: a
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);

  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);

  envoy::config::core::v3::Metadata dynamic_metadata;
  (*(*dynamic_metadata.mutable_filter_metadata())["xxx"].mutable_fields())["a"].set_string_value(
      "$IP$");

  EXPECT_CALL(Const(connection.stream_info_), dynamicMetadata())
      .WillRepeatedly(ReturnRef(dynamic_metadata));

  EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
  HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
  EXPECT_EQ(host, nullptr);
  EXPECT_EQ(
      1, TestUtility::findCounter(stats_store_, "cluster.name.original_dst_host_invalid")->value());
}

TEST_F(OriginalDstLbTest, MetadataKeyListValue) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      metadata_key:
        key: xxx
        path:
        - key: a
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);

  envoy::config::core::v3::Metadata dynamic_metadata;
  (*(*dynamic_metadata.mutable_filter_metadata())["xxx"].mutable_fields())["a"]
      .mutable_list_value()
      ->add_values()
      ->set_string_value("127.0.0.1:6666");

  EXPECT_CALL(Const(connection.stream_info_), dynamicMetadata())
      .WillRepeatedly(ReturnRef(dynamic_metadata));

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
  post_cb();
  ASSERT_NE(host, nullptr);
  EXPECT_EQ("127.0.0.1:6666", host->address()->asString());
}

TEST_F(OriginalDstLbTest, PortOverride) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      upstream_port_override: 443
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  NiceMock<Network::MockConnection> connection;
  connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11", 80));

  TestLoadBalancerContext lb_context(&connection);
  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
  post_cb();
  ASSERT_NE(host, nullptr);
  EXPECT_EQ("10.10.11.11:443", host->address()->asString());
}

TEST_F(OriginalDstLbTest, PortOverrideWithFilterState) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      use_http_header: true
      upstream_port_override: 443
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  NiceMock<Network::MockConnection> connection;
  connection.stream_info_.filterState()->setData(
      Upstream::OriginalDstClusterFilterStateKey,
      std::make_shared<Network::AddressObject>(
          std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11", 6666)),
      StreamInfo::FilterState::StateType::ReadOnly);
  TestLoadBalancerContext lb_context(&connection, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                     "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
  post_cb();
  ASSERT_NE(host, nullptr);
  // Port override applies on top of filter state address.
  EXPECT_EQ("10.10.11.11:443", host->address()->asString());
}

TEST_F(OriginalDstLbTest, HostCleanup) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11"));

  EXPECT_CALL(membership_updated_, ready());
  Event::PostCb post_cb;
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host = OriginalDstCluster::LoadBalancer(handle_).chooseHost(&lb_context).host;
  post_cb();
  ASSERT_NE(host, nullptr);
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());

  // First timeout marks hosts as not recently used.
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  cleanup_timer_->invokeCallback();
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());

  // Second timeout removes the host.
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  EXPECT_CALL(membership_updated_, ready());
  cleanup_timer_->invokeCallback();
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

TEST_F(OriginalDstLbTest, HostRemovedThenRecreated) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11"));

  EXPECT_CALL(membership_updated_, ready());
  Event::PostCb post_cb;
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host1 = OriginalDstCluster::LoadBalancer(handle_).chooseHost(&lb_context).host;
  post_cb();
  ASSERT_NE(host1, nullptr);

  // Remove host via cleanup.
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  cleanup_timer_->invokeCallback();
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  EXPECT_CALL(membership_updated_, ready());
  cleanup_timer_->invokeCallback();
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());

  // New host gets created for the same address.
  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host2 = OriginalDstCluster::LoadBalancer(handle_).chooseHost(&lb_context).host;
  post_cb();
  EXPECT_NE(host2, nullptr);
  EXPECT_NE(host2, host1);
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

} // namespace
} // namespace Upstream
} // namespace Envoy
