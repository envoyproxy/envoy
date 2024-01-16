#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.validate.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/common/singleton/manager_impl.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/extensions/clusters/dynamic_forward_proxy/cluster.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/mocks/http/conn_pool.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/options.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/load_balancer.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"

using testing::AtLeast;
using testing::DoAll;
using testing::InSequence;
using testing::Return;
using testing::SizeIs;

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicForwardProxy {

class ClusterTest : public testing::Test,
                    public Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory {
public:
  void initialize(const std::string& yaml_config, bool uses_tls) {
    envoy::config::cluster::v3::Cluster cluster_config =
        Upstream::parseClusterFromV3Yaml(yaml_config);
    envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig config;
    Config::Utility::translateOpaqueConfig(cluster_config.cluster_type().typed_config(),
                                           ProtobufMessage::getStrictValidationVisitor(), config);

    Envoy::Upstream::ClusterFactoryContextImpl factory_context(
        server_context_, server_context_.cluster_manager_, nullptr, ssl_context_manager_, nullptr,
        false);

    ON_CALL(server_context_, api()).WillByDefault(testing::ReturnRef(*api_));

    if (uses_tls) {
      EXPECT_CALL(ssl_context_manager_, createSslClientContext(_, _));
    }
    EXPECT_CALL(*dns_cache_manager_, getCache(_));
    // Below we return a nullptr handle which has no effect on the code under test but isn't
    // actually correct. It's possible this will have to change in the future.
    EXPECT_CALL(*dns_cache_manager_->dns_cache_, addUpdateCallbacks_(_))
        .WillOnce(DoAll(SaveArgAddress(&update_callbacks_), Return(nullptr)));
    auto cache = dns_cache_manager_->getCache(config.dns_cache_config()).value();
    cluster_.reset(
        new Cluster(cluster_config, std::move(cache), config, factory_context, this->get()));
    thread_aware_lb_ = std::make_unique<Cluster::ThreadAwareLoadBalancer>(*cluster_);
    lb_factory_ = thread_aware_lb_->factory();
    refreshLb();

    ON_CALL(lb_context_, downstreamHeaders()).WillByDefault(Return(&downstream_headers_));
    ON_CALL(connection_, streamInfo()).WillByDefault(ReturnRef(stream_info_));
    ON_CALL(lb_context_, requestStreamInfo()).WillByDefault(Return(&stream_info_));
    ON_CALL(lb_context_, downstreamConnection()).WillByDefault(Return(&connection_));

    member_update_cb_ = cluster_->prioritySet().addMemberUpdateCb(
        [this](const Upstream::HostVector& hosts_added,
               const Upstream::HostVector& hosts_removed) -> void {
          onMemberUpdateCb(hosts_added, hosts_removed);
        });

    absl::flat_hash_map<std::string, Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr>
        existing_hosts;
    for (const auto& host : host_map_) {
      existing_hosts.emplace(host.first, host.second);
    }
    EXPECT_CALL(*dns_cache_manager_->dns_cache_, iterateHostMap(_)).WillOnce(Invoke([&](auto cb) {
      for (const auto& host : host_map_) {
        cb(host.first, host.second);
      }
    }));
    if (!existing_hosts.empty()) {
      EXPECT_CALL(*this, onMemberUpdateCb(SizeIs(existing_hosts.size()), SizeIs(0)));
    }
    cluster_->initialize([] {});
  }

  Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr get() override {
    return dns_cache_manager_;
  }

  void makeTestHost(const std::string& host, const std::string& address) {
    EXPECT_TRUE(host_map_.find(host) == host_map_.end());
    host_map_[host] = std::make_shared<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>();
    host_map_[host]->address_ = Network::Utility::parseInternetAddress(address);

    // Allow touch() to still be strict.
    EXPECT_CALL(*host_map_[host], address()).Times(AtLeast(0));
    EXPECT_CALL(*host_map_[host], addressList()).Times(AtLeast(0));
    EXPECT_CALL(*host_map_[host], isIpAddress()).Times(AtLeast(0));
    EXPECT_CALL(*host_map_[host], resolvedHost()).Times(AtLeast(0));
  }

  void updateTestHostAddress(const std::string& host, const std::string& address) {
    EXPECT_FALSE(host_map_.find(host) == host_map_.end());
    host_map_[host]->address_ = Network::Utility::parseInternetAddress(address);
  }

  void refreshLb() { lb_ = lb_factory_->create(lb_params_); }

  Upstream::MockLoadBalancerContext* setHostAndReturnContext(const std::string& host) {
    downstream_headers_.remove(":authority");
    downstream_headers_.addCopy(":authority", host);
    return &lb_context_;
  }

  Upstream::MockLoadBalancerContext* setFilterStateHostAndReturnContext(const std::string& host) {
    StreamInfo::FilterState& filter_state =
        const_cast<StreamInfo::FilterState&>(lb_context_.requestStreamInfo()->filterState());

    filter_state.setData(
        "envoy.upstream.dynamic_host", std::make_shared<Router::StringAccessorImpl>(host),
        StreamInfo::FilterState::StateType::Mutable, StreamInfo::FilterState::LifeSpan::Connection,
        StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);

    return &lb_context_;
  }

  void setOutlierFailed(const std::string& host) {
    for (auto& h : cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()) {
      if (h->hostname() == host) {
        h->healthFlagSet(Upstream::Host::HealthFlag::FAILED_OUTLIER_CHECK);
        break;
      }
    }
  }

  void clearOutlierFailed(const std::string& host) {
    for (auto& h : cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()) {
      if (h->hostname() == host) {
        h->healthFlagClear(Upstream::Host::HealthFlag::FAILED_OUTLIER_CHECK);
        break;
      }
    }
  }

  MOCK_METHOD(void, onMemberUpdateCb,
              (const Upstream::HostVector& hosts_added, const Upstream::HostVector& hosts_removed));

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Stats::TestUtil::TestStore& stats_store_ = server_context_.store_;
  Api::ApiPtr api_{Api::createApiForTest(stats_store_)};
  Ssl::MockContextManager ssl_context_manager_;

  std::shared_ptr<Extensions::Common::DynamicForwardProxy::MockDnsCacheManager> dns_cache_manager_{
      new Extensions::Common::DynamicForwardProxy::MockDnsCacheManager()};
  std::shared_ptr<Cluster> cluster_;
  Upstream::ThreadAwareLoadBalancerPtr thread_aware_lb_;
  Upstream::LoadBalancerFactorySharedPtr lb_factory_;
  Upstream::LoadBalancerPtr lb_;
  NiceMock<Upstream::MockLoadBalancerContext> lb_context_;
  Http::TestRequestHeaderMapImpl downstream_headers_;
  Extensions::Common::DynamicForwardProxy::DnsCache::UpdateCallbacks* update_callbacks_{};
  absl::flat_hash_map<std::string,
                      std::shared_ptr<Extensions::Common::DynamicForwardProxy::MockDnsHostInfo>>
      host_map_;
  Envoy::Common::CallbackHandlePtr member_update_cb_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<Network::MockConnection> connection_;

  // Just use this as parameters of create() method but thread aware load balancer will not use it.
  NiceMock<Upstream::MockPrioritySet> worker_priority_set_;
  Upstream::LoadBalancerParams lb_params_{worker_priority_set_, {}};

  const std::string sub_cluster_yaml_config_ = R"EOF(
name: name
connect_timeout: 0.25s
cluster_type:
  name: dynamic_forward_proxy
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
    sub_clusters_config:
      max_sub_clusters: 1024
)EOF";

  const std::string default_yaml_config_ = R"EOF(
name: name
connect_timeout: 0.25s
cluster_type:
  name: dynamic_forward_proxy
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
    dns_cache_config:
      name: foo
      dns_lookup_family: AUTO
)EOF";

  const std::string coalesce_connection_config_ = R"EOF(
name: name
connect_timeout: 0.25s
cluster_type:
  name: dynamic_forward_proxy
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
    allow_coalesced_connections: true
    dns_cache_config:
      name: foo
      dns_lookup_family: AUTO
)EOF";
};

// createSubClusterConfig twice.
TEST_F(ClusterTest, CreateSubClusterConfig) {
  initialize(sub_cluster_yaml_config_, false);

  const std::string cluster_name = "fake_cluster_name";
  const std::string host = "localhost";
  const int port = 80;
  std::pair<bool, absl::optional<envoy::config::cluster::v3::Cluster>> sub_cluster_pair =
      cluster_->createSubClusterConfig(cluster_name, host, port);
  EXPECT_EQ(true, sub_cluster_pair.first);
  EXPECT_EQ(true, sub_cluster_pair.second.has_value());

  // create again, already exists
  sub_cluster_pair = cluster_->createSubClusterConfig(cluster_name, host, port);
  EXPECT_EQ(true, sub_cluster_pair.first);
  EXPECT_EQ(false, sub_cluster_pair.second.has_value());
}

// Basic flow of the cluster including adding hosts and removing them.
TEST_F(ClusterTest, BasicFlow) {
  initialize(default_yaml_config_, false);
  makeTestHost("host1:0", "1.2.3.4");
  InSequence s;

  EXPECT_EQ(nullptr, lb_->chooseHost(setHostAndReturnContext("")));

  // Verify no host LB cases.
  EXPECT_EQ(nullptr, lb_->chooseHost(setHostAndReturnContext("foo")));
  EXPECT_EQ(nullptr, lb_->peekAnotherHost(setHostAndReturnContext("foo")));

  // LB will immediately resolve host1.
  EXPECT_CALL(*this, onMemberUpdateCb(SizeIs(1), SizeIs(0)));
  update_callbacks_->onDnsHostAddOrUpdate("host1:0", host_map_["host1:0"]);
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ("1.2.3.4:0",
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->address()->asString());
  EXPECT_CALL(*host_map_["host1:0"], touch());
  EXPECT_EQ("1.2.3.4:0",
            lb_->chooseHost(setHostAndReturnContext("host1:0"))->address()->asString());

  // After changing the address, LB will immediately resolve the new address with a refresh.
  updateTestHostAddress("host1:0", "2.3.4.5");
  update_callbacks_->onDnsHostAddOrUpdate("host1:0", host_map_["host1:0"]);
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ("2.3.4.5:0",
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->address()->asString());
  EXPECT_CALL(*host_map_["host1:0"], touch());
  EXPECT_EQ("2.3.4.5:0",
            lb_->chooseHost(setHostAndReturnContext("host1:0"))->address()->asString());

  // Remove the host, LB will immediately fail to find the host in the map.
  EXPECT_CALL(*this, onMemberUpdateCb(SizeIs(0), SizeIs(1)));
  update_callbacks_->onDnsHostRemove("host1:0");
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(nullptr, lb_->chooseHost(setHostAndReturnContext("host1:0")));
}

// Outlier detection
TEST_F(ClusterTest, OutlierDetection) {
  initialize(default_yaml_config_, false);
  makeTestHost("host1:0", "1.2.3.4");
  makeTestHost("host2:0", "5.6.7.8");
  InSequence s;

  EXPECT_CALL(*this, onMemberUpdateCb(SizeIs(1), SizeIs(0)));
  update_callbacks_->onDnsHostAddOrUpdate("host1:0", host_map_["host1:0"]);
  EXPECT_CALL(*host_map_["host1:0"], touch());
  EXPECT_EQ("1.2.3.4:0",
            lb_->chooseHost(setHostAndReturnContext("host1:0"))->address()->asString());

  EXPECT_CALL(*this, onMemberUpdateCb(SizeIs(1), SizeIs(0)));
  update_callbacks_->onDnsHostAddOrUpdate("host2:0", host_map_["host2:0"]);
  EXPECT_CALL(*host_map_["host2:0"], touch());
  EXPECT_EQ("5.6.7.8:0",
            lb_->chooseHost(setHostAndReturnContext("host2:0"))->address()->asString());

  // Fail outlier check for host1
  setOutlierFailed("host1:0");
  EXPECT_EQ(nullptr, lb_->chooseHost(setHostAndReturnContext("host1:0")));
  // "host2:0" should not be affected
  EXPECT_CALL(*host_map_["host2:0"], touch());
  EXPECT_EQ("5.6.7.8:0",
            lb_->chooseHost(setHostAndReturnContext("host2:0"))->address()->asString());

  // Clear outlier check failure for host1, it should be available again
  clearOutlierFailed("host1:0");
  EXPECT_CALL(*host_map_["host1:0"], touch());
  EXPECT_EQ("1.2.3.4:0",
            lb_->chooseHost(setHostAndReturnContext("host1:0"))->address()->asString());
}

// Various invalid LB context permutations in case the cluster is used outside of HTTP.
TEST_F(ClusterTest, InvalidLbContext) {
  initialize(default_yaml_config_, false);
  ON_CALL(lb_context_, downstreamHeaders()).WillByDefault(Return(nullptr));
  EXPECT_EQ(nullptr, lb_->chooseHost(&lb_context_));
  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));
}

TEST_F(ClusterTest, FilterStateHostOverride) {
  initialize(default_yaml_config_, false);
  makeTestHost("host1:0", "1.2.3.4");

  EXPECT_CALL(*this, onMemberUpdateCb(SizeIs(1), SizeIs(0)));
  update_callbacks_->onDnsHostAddOrUpdate("host1:0", host_map_["host1:0"]);
  EXPECT_CALL(*host_map_["host1:0"], touch());
  EXPECT_EQ("1.2.3.4:0",
            lb_->chooseHost(setFilterStateHostAndReturnContext("host1:0"))->address()->asString());
}

// Verify cluster attaches to a populated cache.
TEST_F(ClusterTest, PopulatedCache) {
  makeTestHost("host1:0", "1.2.3.4");
  makeTestHost("host2:0", "1.2.3.5");
  initialize(default_yaml_config_, false);
  EXPECT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

TEST_F(ClusterTest, LoadBalancer_LifetimeCallbacksWithoutCoalescing) {
  initialize(default_yaml_config_, false);

  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetime_callbacks =
      lb_->lifetimeCallbacks();
  ASSERT_FALSE(lifetime_callbacks.has_value());
}

TEST_F(ClusterTest, LoadBalancer_LifetimeCallbacksWithCoalescing) {
  initialize(coalesce_connection_config_, false);

  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetime_callbacks =
      lb_->lifetimeCallbacks();
  ASSERT_TRUE(lifetime_callbacks.has_value());
}

TEST_F(ClusterTest, LoadBalancer_SelectPoolNoConnections) {
  initialize(coalesce_connection_config_, false);

  const std::string hostname = "mail.example.org";
  Upstream::MockHost host;
  EXPECT_CALL(host, hostname()).WillRepeatedly(testing::ReturnRef(hostname));
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://10.0.0.3:50000");
  EXPECT_CALL(host, address()).WillRepeatedly(testing::Return(address));
  std::vector<uint8_t> hash_key = {1, 2, 3};

  absl::optional<Upstream::SelectedPoolAndConnection> selection =
      lb_->selectExistingConnection(&lb_context_, host, hash_key);

  EXPECT_FALSE(selection.has_value());
}

TEST_F(ClusterTest, LoadBalancer_SelectPoolMatchingConnection) {
  initialize(coalesce_connection_config_, false);

  const std::string hostname = "mail.example.org";
  Upstream::MockHost host;
  EXPECT_CALL(host, hostname()).WillRepeatedly(testing::ReturnRef(hostname));
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://10.0.0.3:50000");
  EXPECT_CALL(host, address()).WillRepeatedly(testing::Return(address));
  std::vector<uint8_t> hash_key = {1, 2, 3};

  Envoy::Http::ConnectionPool::MockInstance pool;
  Envoy::Network::MockConnection connection;
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetime_callbacks =
      lb_->lifetimeCallbacks();
  ASSERT_TRUE(lifetime_callbacks.has_value());

  EXPECT_CALL(connection, connectionInfoProvider()).Times(testing::AnyNumber());
  EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return("h2"));
  auto ssl_info = std::make_shared<Ssl::MockConnectionInfo>();
  EXPECT_CALL(connection, ssl()).WillRepeatedly(Return(ssl_info));
  lifetime_callbacks->onConnectionOpen(pool, hash_key, connection);
  std::vector<std::string> dns_sans = {"www.example.org", "mail.example.org"};
  EXPECT_CALL(*ssl_info, dnsSansPeerCertificate()).WillOnce(Return(dns_sans));

  absl::optional<Upstream::SelectedPoolAndConnection> selection =
      lb_->selectExistingConnection(&lb_context_, host, hash_key);

  ASSERT_TRUE(selection.has_value());
  EXPECT_EQ(&pool, &selection->pool_);
  EXPECT_EQ(&connection, &selection->connection_);
}

TEST_F(ClusterTest, LoadBalancer_SelectPoolMatchingConnectionHttp3) {
  initialize(coalesce_connection_config_, false);

  const std::string hostname = "mail.example.org";
  Upstream::MockHost host;
  EXPECT_CALL(host, hostname()).WillRepeatedly(testing::ReturnRef(hostname));
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://10.0.0.3:50000");
  EXPECT_CALL(host, address()).WillRepeatedly(testing::Return(address));
  std::vector<uint8_t> hash_key = {1, 2, 3};

  Envoy::Http::ConnectionPool::MockInstance pool;
  Envoy::Network::MockConnection connection;
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetime_callbacks =
      lb_->lifetimeCallbacks();
  ASSERT_TRUE(lifetime_callbacks.has_value());

  EXPECT_CALL(connection, connectionInfoProvider()).Times(testing::AnyNumber());
  EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return("h3"));
  auto ssl_info = std::make_shared<Ssl::MockConnectionInfo>();
  EXPECT_CALL(connection, ssl()).WillRepeatedly(Return(ssl_info));
  lifetime_callbacks->onConnectionOpen(pool, hash_key, connection);
  std::vector<std::string> dns_sans = {"www.example.org", "mail.example.org"};
  EXPECT_CALL(*ssl_info, dnsSansPeerCertificate()).WillOnce(Return(dns_sans));

  absl::optional<Upstream::SelectedPoolAndConnection> selection =
      lb_->selectExistingConnection(&lb_context_, host, hash_key);

  ASSERT_TRUE(selection.has_value());
  EXPECT_EQ(&pool, &selection->pool_);
  EXPECT_EQ(&connection, &selection->connection_);
}

TEST_F(ClusterTest, LoadBalancer_SelectPoolNoMatchingConnectionAfterDraining) {
  initialize(coalesce_connection_config_, false);

  const std::string hostname = "mail.example.org";
  Upstream::MockHost host;
  EXPECT_CALL(host, hostname()).WillRepeatedly(testing::ReturnRef(hostname));
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://10.0.0.3:50000");
  EXPECT_CALL(host, address()).WillRepeatedly(testing::Return(address));
  std::vector<uint8_t> hash_key = {1, 2, 3};

  Envoy::Http::ConnectionPool::MockInstance pool;
  Envoy::Network::MockConnection connection;
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetime_callbacks =
      lb_->lifetimeCallbacks();
  ASSERT_TRUE(lifetime_callbacks.has_value());

  EXPECT_CALL(connection, connectionInfoProvider()).Times(testing::AnyNumber());
  EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return("h2"));
  auto ssl_info = std::make_shared<Ssl::MockConnectionInfo>();
  EXPECT_CALL(connection, ssl()).WillRepeatedly(Return(ssl_info));
  lifetime_callbacks->onConnectionOpen(pool, hash_key, connection);

  // Drain the connection then no verify that no connection is subsequently selected.
  lifetime_callbacks->onConnectionDraining(pool, hash_key, connection);

  absl::optional<Upstream::SelectedPoolAndConnection> selection =
      lb_->selectExistingConnection(&lb_context_, host, hash_key);

  ASSERT_FALSE(selection.has_value());
}

TEST_F(ClusterTest, LoadBalancer_SelectPoolInvalidAlpn) {
  initialize(coalesce_connection_config_, false);

  const std::string hostname = "mail.example.org";
  Upstream::MockHost host;
  EXPECT_CALL(host, hostname()).WillRepeatedly(testing::ReturnRef(hostname));
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://10.0.0.3:50000");
  EXPECT_CALL(host, address()).WillRepeatedly(testing::Return(address));
  std::vector<uint8_t> hash_key = {1, 2, 3};

  Envoy::Http::ConnectionPool::MockInstance pool;
  Envoy::Network::MockConnection connection;
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetime_callbacks =
      lb_->lifetimeCallbacks();
  ASSERT_TRUE(lifetime_callbacks.has_value());

  EXPECT_CALL(connection, connectionInfoProvider()).Times(testing::AnyNumber());
  EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return("hello"));
  auto ssl_info = std::make_shared<Ssl::MockConnectionInfo>();
  EXPECT_CALL(connection, ssl()).WillRepeatedly(Return(ssl_info));
  lifetime_callbacks->onConnectionOpen(pool, hash_key, connection);

  absl::optional<Upstream::SelectedPoolAndConnection> selection =
      lb_->selectExistingConnection(&lb_context_, host, hash_key);

  ASSERT_FALSE(selection.has_value());
}

TEST_F(ClusterTest, LoadBalancer_SelectPoolSanMismatch) {
  initialize(coalesce_connection_config_, false);

  const std::string hostname = "mail.example.org";
  Upstream::MockHost host;
  EXPECT_CALL(host, hostname()).WillRepeatedly(testing::ReturnRef(hostname));
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://10.0.0.3:50000");
  EXPECT_CALL(host, address()).WillRepeatedly(testing::Return(address));
  std::vector<uint8_t> hash_key = {1, 2, 3};

  Envoy::Http::ConnectionPool::MockInstance pool;
  Envoy::Network::MockConnection connection;
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetime_callbacks =
      lb_->lifetimeCallbacks();
  ASSERT_TRUE(lifetime_callbacks.has_value());
  EXPECT_CALL(connection, connectionInfoProvider()).Times(testing::AnyNumber());
  EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return("h2"));
  auto ssl_info = std::make_shared<Ssl::MockConnectionInfo>();
  EXPECT_CALL(connection, ssl()).WillRepeatedly(Return(ssl_info));
  lifetime_callbacks->onConnectionOpen(pool, hash_key, connection);
  std::vector<std::string> dns_sans = {"www.example.org"};
  EXPECT_CALL(*ssl_info, dnsSansPeerCertificate()).WillOnce(Return(dns_sans));

  absl::optional<Upstream::SelectedPoolAndConnection> selection =
      lb_->selectExistingConnection(&lb_context_, host, hash_key);

  ASSERT_FALSE(selection.has_value());
}

TEST_F(ClusterTest, LoadBalancer_SelectPoolHashMismatch) {
  initialize(coalesce_connection_config_, false);

  const std::string hostname = "mail.example.org";
  Upstream::MockHost host;
  EXPECT_CALL(host, hostname()).WillRepeatedly(testing::ReturnRef(hostname));
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://10.0.0.3:50000");
  EXPECT_CALL(host, address()).WillRepeatedly(testing::Return(address));
  std::vector<uint8_t> hash_key = {1, 2, 3};

  Envoy::Http::ConnectionPool::MockInstance pool;
  Envoy::Network::MockConnection connection;
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetime_callbacks =
      lb_->lifetimeCallbacks();
  ASSERT_TRUE(lifetime_callbacks.has_value());
  EXPECT_CALL(connection, connectionInfoProvider()).Times(testing::AnyNumber());
  EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return("h2"));
  auto ssl_info = std::make_shared<Ssl::MockConnectionInfo>();
  EXPECT_CALL(connection, ssl()).WillRepeatedly(Return(ssl_info));
  lifetime_callbacks->onConnectionOpen(pool, hash_key, connection);

  hash_key[0]++;
  absl::optional<Upstream::SelectedPoolAndConnection> selection =
      lb_->selectExistingConnection(&lb_context_, host, hash_key);

  ASSERT_FALSE(selection.has_value());
}

TEST_F(ClusterTest, LoadBalancer_SelectPoolIpMismatch) {
  initialize(coalesce_connection_config_, false);

  const std::string hostname = "mail.example.org";
  Upstream::MockHost host;
  EXPECT_CALL(host, hostname()).WillRepeatedly(testing::ReturnRef(hostname));
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://10.0.0.4:50000");
  EXPECT_CALL(host, address()).WillRepeatedly(testing::Return(address));
  std::vector<uint8_t> hash_key = {1, 2, 3};

  Envoy::Http::ConnectionPool::MockInstance pool;
  Envoy::Network::MockConnection connection;
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetime_callbacks =
      lb_->lifetimeCallbacks();
  ASSERT_TRUE(lifetime_callbacks.has_value());
  EXPECT_CALL(connection, connectionInfoProvider()).Times(testing::AnyNumber());
  EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return("h2"));
  auto ssl_info = std::make_shared<Ssl::MockConnectionInfo>();
  EXPECT_CALL(connection, ssl()).WillRepeatedly(Return(ssl_info));
  lifetime_callbacks->onConnectionOpen(pool, hash_key, connection);
  std::vector<std::string> dns_sans = {"www.example.org", "mail.example.org"};
  EXPECT_CALL(*ssl_info, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));

  absl::optional<Upstream::SelectedPoolAndConnection> selection =
      lb_->selectExistingConnection(&lb_context_, host, hash_key);

  ASSERT_FALSE(selection.has_value());
}

TEST_F(ClusterTest, LoadBalancer_SelectPoolEmptyHostname) {
  initialize(coalesce_connection_config_, false);

  const std::string hostname = "mail.example.org";
  Upstream::MockHost host;
  EXPECT_CALL(host, hostname()).WillRepeatedly(testing::ReturnRef(hostname));
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://10.0.0.4:50000");
  EXPECT_CALL(host, address()).WillRepeatedly(testing::Return(address));
  std::vector<uint8_t> hash_key = {1, 2, 3};

  Envoy::Http::ConnectionPool::MockInstance pool;
  Envoy::Network::MockConnection connection;
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetime_callbacks =
      lb_->lifetimeCallbacks();
  ASSERT_TRUE(lifetime_callbacks.has_value());
  EXPECT_CALL(connection, connectionInfoProvider()).Times(testing::AnyNumber());
  EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return("h2"));
  auto ssl_info = std::make_shared<Ssl::MockConnectionInfo>();
  std::vector<std::string> dns_sans = {"www.example.org", "mail.example.org"};
  EXPECT_CALL(connection, ssl()).WillRepeatedly(Return(ssl_info));
  lifetime_callbacks->onConnectionOpen(pool, hash_key, connection);
  EXPECT_CALL(*ssl_info, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));

  const std::string empty_hostname = "";
  Upstream::MockHost empty_host;
  EXPECT_CALL(empty_host, hostname()).WillRepeatedly(testing::ReturnRef(empty_hostname));

  absl::optional<Upstream::SelectedPoolAndConnection> selection =
      lb_->selectExistingConnection(&lb_context_, empty_host, hash_key);

  ASSERT_FALSE(selection.has_value());
}

TEST_F(ClusterTest, LoadBalancer_SelectPoolNoSSSL) {
  initialize(coalesce_connection_config_, false);

  const std::string hostname = "mail.example.org";
  Upstream::MockHost host;
  EXPECT_CALL(host, hostname()).WillRepeatedly(testing::ReturnRef(hostname));
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://10.0.0.4:50000");
  EXPECT_CALL(host, address()).WillRepeatedly(testing::Return(address));
  std::vector<uint8_t> hash_key = {1, 2, 3};

  Envoy::Http::ConnectionPool::MockInstance pool;
  Envoy::Network::MockConnection connection;
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetime_callbacks =
      lb_->lifetimeCallbacks();
  ASSERT_TRUE(lifetime_callbacks.has_value());
  EXPECT_CALL(connection, connectionInfoProvider()).Times(testing::AnyNumber());
  EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return("h2"));
  auto ssl_info = nullptr;
  EXPECT_CALL(connection, ssl()).WillRepeatedly(Return(ssl_info));
  lifetime_callbacks->onConnectionOpen(pool, hash_key, connection);

  absl::optional<Upstream::SelectedPoolAndConnection> selection =
      lb_->selectExistingConnection(&lb_context_, host, hash_key);

  ASSERT_FALSE(selection.has_value());
}

class ClusterFactoryTest : public testing::Test {
protected:
  void createCluster(const std::string& yaml_config) {
    envoy::config::cluster::v3::Cluster cluster_config =
        Upstream::parseClusterFromV3Yaml(yaml_config);
    Upstream::ClusterFactoryContextImpl cluster_factory_context(
        server_context_, server_context_.cluster_manager_, nullptr, ssl_context_manager_, nullptr,
        true);
    std::unique_ptr<Upstream::ClusterFactory> cluster_factory = std::make_unique<ClusterFactory>();

    auto result = cluster_factory->create(cluster_config, cluster_factory_context);
    if (result.ok()) {
      cluster_ = result->first;
      thread_aware_lb_ = std::move(result->second);
    } else {
      throw EnvoyException(std::string(result.status().message()));
    }
  }

private:
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Stats::TestUtil::TestStore& stats_store_ = server_context_.store_;
  Api::ApiPtr api_{Api::createApiForTest(stats_store_)};

  NiceMock<Ssl::MockContextManager> ssl_context_manager_;
  Upstream::ClusterSharedPtr cluster_;
  Upstream::ThreadAwareLoadBalancerPtr thread_aware_lb_;
};

TEST_F(ClusterFactoryTest, InvalidUpstreamHttpProtocolOptions) {
  const std::string yaml_config = TestEnvironment::substitute(R"EOF(
name: name
connect_timeout: 0.25s
cluster_type:
  name: dynamic_forward_proxy
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
    dns_cache_config:
      name: foo
upstream_http_protocol_options: {}
)EOF");

  EXPECT_THROW_WITH_MESSAGE(
      createCluster(yaml_config), EnvoyException,
      "dynamic_forward_proxy cluster must have auto_sni and auto_san_validation true unless "
      "allow_insecure_cluster_options is set.");
}

TEST_F(ClusterFactoryTest, InsecureUpstreamHttpProtocolOptions) {
  const std::string yaml_config = TestEnvironment::substitute(R"EOF(
name: name
connect_timeout: 0.25s
cluster_type:
  name: dynamic_forward_proxy
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
    allow_insecure_cluster_options: true
    dns_cache_config:
      name: foo
upstream_http_protocol_options: {}
)EOF");

  createCluster(yaml_config);
}

TEST_F(ClusterFactoryTest, InvalidSubprotocolOptions) {
  const std::string yaml_config = R"EOF(
name: name
connect_timeout: 0.25s
cluster_type:
  name: dynamic_forward_proxy
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
    sub_clusters_config:
      max_sub_clusters: 1024
      lb_policy: CLUSTER_PROVIDED
)EOF";

  EXPECT_THROW(createCluster(yaml_config), EnvoyException);
}

TEST(ObjectFactory, DynamicHost) {
  const std::string name = "envoy.upstream.dynamic_host";
  auto* factory =
      Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(name);
  ASSERT_NE(nullptr, factory);
  EXPECT_EQ(name, factory->name());
  const std::string host = "site.com";
  auto object = factory->createFromBytes(host);
  ASSERT_NE(nullptr, object);
  EXPECT_EQ(host, object->serializeAsString());
}

TEST(ObjectFactory, DynamicPort) {
  const std::string name = "envoy.upstream.dynamic_port";
  auto* factory =
      Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(name);
  ASSERT_NE(nullptr, factory);
  EXPECT_EQ(name, factory->name());
  const std::string port = "8080";
  auto object = factory->createFromBytes(port);
  ASSERT_NE(nullptr, object);
  EXPECT_EQ(port, object->serializeAsString());
  ASSERT_EQ(nullptr, factory->createFromBytes("blah"));
}

} // namespace DynamicForwardProxy
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
