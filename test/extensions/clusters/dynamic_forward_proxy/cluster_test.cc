#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.validate.h"

#include "common/singleton/manager_impl.h"
#include "common/upstream/cluster_factory_impl.h"

#include "extensions/clusters/dynamic_forward_proxy/cluster.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/common/dynamic_forward_proxy/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/load_balancer.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/test_common/environment.h"

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
                                           ProtobufWkt::Struct::default_instance(),
                                           ProtobufMessage::getStrictValidationVisitor(), config);
    Stats::ScopePtr scope = stats_store_.createScope("cluster.name.");
    Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, stats_store_,
        singleton_manager_, tls_, validation_visitor_, *api_);
    if (uses_tls) {
      EXPECT_CALL(ssl_context_manager_, createSslClientContext(_, _, _));
    }
    EXPECT_CALL(*dns_cache_manager_, getCache(_));
    // Below we return a nullptr handle which has no effect on the code under test but isn't
    // actually correct. It's possible this will have to change in the future.
    EXPECT_CALL(*dns_cache_manager_->dns_cache_, addUpdateCallbacks_(_))
        .WillOnce(DoAll(SaveArgAddress(&update_callbacks_), Return(nullptr)));
    cluster_ = std::make_shared<Cluster>(cluster_config, config, runtime_, *this, local_info_,
                                         factory_context, std::move(scope), false);
    thread_aware_lb_ = std::make_unique<Cluster::ThreadAwareLoadBalancer>(*cluster_);
    lb_factory_ = thread_aware_lb_->factory();
    refreshLb();

    ON_CALL(lb_context_, downstreamHeaders()).WillByDefault(Return(&downstream_headers_));

    cluster_->prioritySet().addMemberUpdateCb(
        [this](const Upstream::HostVector& hosts_added,
               const Upstream::HostVector& hosts_removed) -> void {
          onMemberUpdateCb(hosts_added, hosts_removed);
        });

    absl::flat_hash_map<std::string, Extensions::Common::DynamicForwardProxy::DnsHostInfoSharedPtr>
        existing_hosts;
    for (const auto& host : host_map_) {
      existing_hosts.emplace(host.first, host.second);
    }
    EXPECT_CALL(*dns_cache_manager_->dns_cache_, hosts()).WillOnce(Return(existing_hosts));
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
    EXPECT_CALL(*host_map_[host], isIpAddress()).Times(AtLeast(0));
    EXPECT_CALL(*host_map_[host], resolvedHost()).Times(AtLeast(0));
  }

  void updateTestHostAddress(const std::string& host, const std::string& address) {
    EXPECT_FALSE(host_map_.find(host) == host_map_.end());
    host_map_[host]->address_ = Network::Utility::parseInternetAddress(address);
  }

  void refreshLb() { lb_ = lb_factory_->create(); }

  Upstream::MockLoadBalancerContext* setHostAndReturnContext(const std::string& host) {
    downstream_headers_.remove(":authority");
    downstream_headers_.addCopy(":authority", host);
    return &lb_context_;
  }

  MOCK_METHOD(void, onMemberUpdateCb,
              (const Upstream::HostVector& hosts_added, const Upstream::HostVector& hosts_removed));

  Stats::IsolatedStoreImpl stats_store_;
  Ssl::MockContextManager ssl_context_manager_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::MockAdmin> admin_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_{Api::createApiForTest(stats_store_)};
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

  const std::string default_yaml_config_ = R"EOF(
name: name
connect_timeout: 0.25s
cluster_type:
  name: dynamic_forward_proxy
  typed_config:
    "@type": type.googleapis.com/envoy.config.cluster.dynamic_forward_proxy.v2alpha.ClusterConfig
    dns_cache_config:
      name: foo
      dns_lookup_family: AUTO
)EOF";
};

// Basic flow of the cluster including adding hosts and removing them.
TEST_F(ClusterTest, BasicFlow) {
  initialize(default_yaml_config_, false);
  makeTestHost("host1", "1.2.3.4");
  InSequence s;

  // Verify no host LB cases.
  EXPECT_EQ(nullptr, lb_->chooseHost(setHostAndReturnContext("foo")));

  // LB will not resolve host1 until it has been updated.
  EXPECT_CALL(*this, onMemberUpdateCb(SizeIs(1), SizeIs(0)));
  update_callbacks_->onDnsHostAddOrUpdate("host1", host_map_["host1"]);
  EXPECT_EQ(nullptr, lb_->chooseHost(setHostAndReturnContext("host1")));
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ("1.2.3.4:0",
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->address()->asString());
  refreshLb();
  EXPECT_CALL(*host_map_["host1"], touch());
  EXPECT_EQ("1.2.3.4:0", lb_->chooseHost(setHostAndReturnContext("host1"))->address()->asString());

  // After changing the address, LB will immediately resolve the new address with a refresh.
  updateTestHostAddress("host1", "2.3.4.5");
  update_callbacks_->onDnsHostAddOrUpdate("host1", host_map_["host1"]);
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ("2.3.4.5:0",
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->address()->asString());
  EXPECT_CALL(*host_map_["host1"], touch());
  EXPECT_EQ("2.3.4.5:0", lb_->chooseHost(setHostAndReturnContext("host1"))->address()->asString());

  // Remove the host, LB will still resolve until it is refreshed.
  EXPECT_CALL(*this, onMemberUpdateCb(SizeIs(0), SizeIs(1)));
  update_callbacks_->onDnsHostRemove("host1");
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_CALL(*host_map_["host1"], touch());
  EXPECT_EQ("2.3.4.5:0", lb_->chooseHost(setHostAndReturnContext("host1"))->address()->asString());
  refreshLb();
  EXPECT_EQ(nullptr, lb_->chooseHost(setHostAndReturnContext("host1")));
}

// Various invalid LB context permutations in case the cluster is used outside of HTTP.
TEST_F(ClusterTest, InvalidLbContext) {
  initialize(default_yaml_config_, false);
  ON_CALL(lb_context_, downstreamHeaders()).WillByDefault(Return(nullptr));
  EXPECT_EQ(nullptr, lb_->chooseHost(&lb_context_));
  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));
}

// Verify cluster attaches to a populated cache.
TEST_F(ClusterTest, PopulatedCache) {
  makeTestHost("host1", "1.2.3.4");
  makeTestHost("host2", "1.2.3.5");
  initialize(default_yaml_config_, false);
  EXPECT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

class ClusterFactoryTest : public testing::Test {
protected:
  void createCluster(const std::string& yaml_config, bool avoid_boosting = true) {
    envoy::config::cluster::v3::Cluster cluster_config =
        Upstream::parseClusterFromV3Yaml(yaml_config, avoid_boosting);
    Upstream::ClusterFactoryContextImpl cluster_factory_context(
        cm_, stats_store_, tls_, nullptr, ssl_context_manager_, runtime_, dispatcher_, log_manager_,
        local_info_, admin_, singleton_manager_, nullptr, true, validation_visitor_, *api_);
    std::unique_ptr<Upstream::ClusterFactory> cluster_factory = std::make_unique<ClusterFactory>();

    std::tie(cluster_, thread_aware_lb_) =
        cluster_factory->create(cluster_config, cluster_factory_context);
  }

private:
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Ssl::MockContextManager> ssl_context_manager_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<AccessLog::MockAccessLogManager> log_manager_;
  NiceMock<Server::MockAdmin> admin_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_{Api::createApiForTest(stats_store_)};
  Upstream::ClusterSharedPtr cluster_;
  Upstream::ThreadAwareLoadBalancerPtr thread_aware_lb_;
};

// Verify that using 'sni' causes a failure.
TEST_F(ClusterFactoryTest, DEPRECATED_FEATURE_TEST(InvalidSNI)) {
  const std::string yaml_config = TestEnvironment::substitute(R"EOF(
name: name
connect_timeout: 0.25s
cluster_type:
  name: dynamic_forward_proxy
  typed_config:
    "@type": type.googleapis.com/envoy.config.cluster.dynamic_forward_proxy.v2alpha.ClusterConfig
    dns_cache_config:
      name: foo
tls_context:
  sni: api.lyft.com
  common_tls_context:
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
)EOF");

  EXPECT_THROW_WITH_MESSAGE(
      createCluster(yaml_config, false), EnvoyException,
      "dynamic_forward_proxy cluster cannot configure 'sni' or 'verify_subject_alt_name'");
}

// Verify that using 'verify_subject_alt_name' causes a failure.
TEST_F(ClusterFactoryTest, DEPRECATED_FEATURE_TEST(InvalidVerifySubjectAltName)) {
  const std::string yaml_config = TestEnvironment::substitute(R"EOF(
name: name
connect_timeout: 0.25s
cluster_type:
  name: dynamic_forward_proxy
  typed_config:
    "@type": type.googleapis.com/envoy.config.cluster.dynamic_forward_proxy.v2alpha.ClusterConfig
    dns_cache_config:
      name: foo
tls_context:
  common_tls_context:
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
      verify_subject_alt_name: [api.lyft.com]
)EOF");

  EXPECT_THROW_WITH_MESSAGE(
      createCluster(yaml_config, false), EnvoyException,
      "dynamic_forward_proxy cluster cannot configure 'sni' or 'verify_subject_alt_name'");
}

TEST_F(ClusterFactoryTest, InvalidUpstreamHttpProtocolOptions) {
  const std::string yaml_config = TestEnvironment::substitute(R"EOF(
name: name
connect_timeout: 0.25s
cluster_type:
  name: dynamic_forward_proxy
  typed_config:
    "@type": type.googleapis.com/envoy.config.cluster.dynamic_forward_proxy.v2alpha.ClusterConfig
    dns_cache_config:
      name: foo
upstream_http_protocol_options: {}
)EOF");

  EXPECT_THROW_WITH_MESSAGE(
      createCluster(yaml_config), EnvoyException,
      "dynamic_forward_proxy cluster must have auto_sni and auto_san_validation true when "
      "configured with upstream_http_protocol_options");
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

} // namespace DynamicForwardProxy
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
