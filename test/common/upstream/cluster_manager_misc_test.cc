#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/upstream/load_balancer_factory_base.h"
#include "source/extensions/load_balancing_policies/subset/subset_lb.h"

#include "test/common/upstream/cluster_manager_impl_test_common.h"
#include "test/common/upstream/metadata_writer_lb.pb.h"
#include "test/mocks/upstream/cluster_update_callbacks.h"
#include "test/mocks/upstream/load_balancer_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

using ::testing::InSequence;
using ::testing::InvokeWithoutArgs;
using ::testing::Return;
using ::testing::ReturnNew;

class ClusterManagerSubsetInitializationTest
    : public ClusterManagerImplTest,
      public testing::WithParamInterface<envoy::config::cluster::v3::Cluster::LbPolicy> {
public:
  ClusterManagerSubsetInitializationTest() = default;

  static std::vector<envoy::config::cluster::v3::Cluster::LbPolicy> lbPolicies() {
    int first = static_cast<int>(envoy::config::cluster::v3::Cluster::LbPolicy_MIN);
    int last = static_cast<int>(envoy::config::cluster::v3::Cluster::LbPolicy_MAX);
    ASSERT(first < last);

    std::vector<envoy::config::cluster::v3::Cluster::LbPolicy> policies;
    for (int i = first; i <= last; i++) {
      if (envoy::config::cluster::v3::Cluster::LbPolicy_IsValid(i)) {
        auto policy = static_cast<envoy::config::cluster::v3::Cluster::LbPolicy>(i);
        policies.push_back(policy);
      }
    }
    return policies;
  }

  static std::string paramName(const testing::TestParamInfo<ParamType>& info) {
    const std::string& name = envoy::config::cluster::v3::Cluster::LbPolicy_Name(info.param);
    return absl::StrReplaceAll(name, {{"_", ""}});
  }
};

// Test initialization of subset load balancer with every possible load balancer policy.
TEST_P(ClusterManagerSubsetInitializationTest, SubsetLoadBalancerInitialization) {
  constexpr absl::string_view yamlPattern = R"EOF(
 static_resources:
  clusters:
  - name: cluster_1
    connect_timeout: 0.250s
    {}
    lb_policy: "{}"
    lb_subset_config:
      fallback_policy: ANY_ENDPOINT
      subset_selectors:
        - keys: [ "x" ]
    load_assignment:
      cluster_name: cluster_1
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 8000
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 8001
  )EOF";

  const std::string& policy_name = envoy::config::cluster::v3::Cluster::LbPolicy_Name(GetParam());

  std::string cluster_type = "type: STATIC";

  if (GetParam() == envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED) {
    // This custom cluster type is registered by linking test/integration/custom/static_cluster.cc.
    cluster_type = "cluster_type: { name: envoy.clusters.custom_static_with_lb }";
  }
  const std::string yaml = fmt::format(yamlPattern, cluster_type, policy_name);

  if (GetParam() == envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED) {
    EXPECT_THROW_WITH_MESSAGE(
        create(parseBootstrapFromV3Yaml(yaml)), EnvoyException,
        fmt::format("cluster: LB policy {} cannot be combined with lb_subset_config",
                    envoy::config::cluster::v3::Cluster::LbPolicy_Name(GetParam())));

  } else if (GetParam() == envoy::config::cluster::v3::Cluster::LOAD_BALANCING_POLICY_CONFIG) {
    EXPECT_THROW_WITH_MESSAGE(create(parseBootstrapFromV3Yaml(yaml)), EnvoyException,
                              "cluster: field load_balancing_policy need to be set");
  } else {
    create(parseBootstrapFromV3Yaml(yaml));
    checkStats(1 /*added*/, 0 /*modified*/, 0 /*removed*/, 1 /*active*/, 0 /*warming*/);

    Upstream::ThreadLocalCluster* tlc = cluster_manager_->getThreadLocalCluster("cluster_1");
    EXPECT_NE(nullptr, tlc);

    if (tlc) {
      Upstream::LoadBalancer& lb = tlc->loadBalancer();
      EXPECT_NE(nullptr, dynamic_cast<Upstream::SubsetLoadBalancer*>(&lb));
    }

    factory_.tls_.shutdownThread();
  }
}

INSTANTIATE_TEST_SUITE_P(ClusterManagerSubsetInitializationTest,
                         ClusterManagerSubsetInitializationTest,
                         testing::ValuesIn(ClusterManagerSubsetInitializationTest::lbPolicies()),
                         ClusterManagerSubsetInitializationTest::paramName);

class ClusterManagerImplThreadAwareLbTest : public ClusterManagerImplTest {
public:
  void doTest(const std::string& factory_name) {
    const std::string json = fmt::sprintf("{\"static_resources\":{%s}}",
                                          clustersJson({defaultStaticClusterJson("cluster_0")}));

    std::shared_ptr<MockClusterMockPrioritySet> cluster1(
        new NiceMock<MockClusterMockPrioritySet>());
    cluster1->info_->name_ = "cluster_0";
    cluster1->info_->lb_factory_ =
        Config::Utility::getFactoryByName<Upstream::TypedLoadBalancerFactory>(factory_name);
    auto proto_message = cluster1->info_->lb_factory_->createEmptyConfigProto();
    cluster1->info_->typed_lb_config_ =
        cluster1->info_->lb_factory_->loadConfig(*server_.server_factory_context_, *proto_message)
            .value();

    InSequence s;
    EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
        .WillOnce(Return(std::make_pair(cluster1, nullptr)));
    ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
    create(parseBootstrapFromV3Json(json));

    EXPECT_EQ(nullptr, cluster_manager_->getThreadLocalCluster("cluster_0"));

    cluster1->prioritySet().getMockHostSet(0)->hosts_ = {
        makeTestHost(cluster1->info_, "tcp://127.0.0.1:80")};
    cluster1->prioritySet().getMockHostSet(0)->runCallbacks(
        cluster1->prioritySet().getMockHostSet(0)->hosts_, {});
    cluster1->initialize_callback_();
    EXPECT_EQ(cluster1->prioritySet().getMockHostSet(0)->hosts_[0],
              cluster_manager_->getThreadLocalCluster("cluster_0")
                  ->loadBalancer()
                  .chooseHost(nullptr)
                  .host);
  }
};

// Test that the cluster manager correctly re-creates the worker local LB when there is a host
// set change.
TEST_F(ClusterManagerImplThreadAwareLbTest, RingHashLoadBalancerThreadAwareUpdate) {
  doTest("envoy.load_balancing_policies.ring_hash");
}

// Test that the cluster manager correctly re-creates the worker local LB when there is a host
// set change.
TEST_F(ClusterManagerImplThreadAwareLbTest, MaglevLoadBalancerThreadAwareUpdate) {
  doTest("envoy.load_balancing_policies.maglev");
}

// Test load balancing policy that validates that request metadata can be mutated
// during host picking.
class MetadataWriterLbImpl : public Upstream::ThreadAwareLoadBalancer {
public:
  MetadataWriterLbImpl() = default;

  Upstream::LoadBalancerFactorySharedPtr factory() override {
    return std::make_shared<LbFactory>();
  }
  absl::Status initialize() override { return absl::OkStatus(); }

private:
  class LbImpl : public Upstream::LoadBalancer {
  public:
    LbImpl() = default;

    Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext* context) override {
      if (context && context->requestStreamInfo()) {
        ProtobufWkt::Struct value;
        (*value.mutable_fields())["foo"] = ValueUtil::stringValue("bar");
        context->requestStreamInfo()->setDynamicMetadata("envoy.load_balancers.metadata_writer",
                                                         value);
      }
      return {nullptr};
    }
    Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override {
      return nullptr;
    }
    OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
      return {};
    }
    absl::optional<Upstream::SelectedPoolAndConnection>
    selectExistingConnection(Upstream::LoadBalancerContext*, const Upstream::Host&,
                             std::vector<uint8_t>&) override {
      return {};
    }
  };

  class LbFactory : public Upstream::LoadBalancerFactory {
  public:
    LbFactory() = default;

    Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams) override {
      return std::make_unique<LbImpl>();
    }
  };
};

class MetadataWriterLbFactory
    : public Upstream::TypedLoadBalancerFactoryBase<
          ::test::load_balancing_policies::metadata_writer::MetadataWriterLbConfig> {
public:
  MetadataWriterLbFactory()
      : TypedLoadBalancerFactoryBase("envoy.load_balancers.metadata_writer") {}

  Upstream::ThreadAwareLoadBalancerPtr create(OptRef<const Upstream::LoadBalancerConfig>,
                                              const Upstream::ClusterInfo&,
                                              const Upstream::PrioritySet&, Runtime::Loader&,
                                              Random::RandomGenerator&, TimeSource&) override {
    return std::make_unique<MetadataWriterLbImpl>();
  }

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext&, const Protobuf::Message&) override {
    return std::make_unique<Upstream::LoadBalancerConfig>();
  }
};

// Validate that load balancing policy can update request's dynamic metadata
TEST_F(ClusterManagerImplThreadAwareLbTest, LoadBalancerCanUpdateMetadata) {
  MetadataWriterLbFactory lb_factory;
  Registry::InjectFactory<Envoy::Upstream::TypedLoadBalancerFactory> registered(lb_factory);
  NiceMock<MockLoadBalancerContext> load_balancer_context;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  const std::string json = fmt::sprintf("{\"static_resources\":{%s}}",
                                        clustersJson({defaultStaticClusterJson("cluster_0")}));

  std::shared_ptr<MockClusterMockPrioritySet> cluster1(new NiceMock<MockClusterMockPrioritySet>());
  cluster1->info_->name_ = "cluster_0";
  cluster1->info_->lb_factory_ =
      Config::Utility::getFactoryByName<Upstream::TypedLoadBalancerFactory>(
          "envoy.load_balancers.metadata_writer");

  InSequence s;
  EXPECT_CALL(factory_, clusterFromProto_(_, _, _, _))
      .WillOnce(Return(std::make_pair(cluster1, nullptr)));
  ON_CALL(*cluster1, initializePhase()).WillByDefault(Return(Cluster::InitializePhase::Primary));
  create(parseBootstrapFromV3Json(json));

  EXPECT_EQ(nullptr, cluster_manager_->getThreadLocalCluster("cluster_0"));

  cluster1->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster1->info_, "tcp://127.0.0.1:80")};
  cluster1->prioritySet().getMockHostSet(0)->runCallbacks(
      cluster1->prioritySet().getMockHostSet(0)->hosts_, {});
  cluster1->initialize_callback_();
  ON_CALL(load_balancer_context, requestStreamInfo()).WillByDefault(Return(&stream_info));
  EXPECT_CALL(stream_info, setDynamicMetadata("envoy.load_balancers.metadata_writer", _));
  cluster_manager_->getThreadLocalCluster("cluster_0")
      ->loadBalancer()
      .chooseHost(&load_balancer_context);
}

using NameVals = std::vector<std::pair<Network::SocketOptionName, int>>;

// Validate that when options are set in the ClusterManager and/or Cluster, we see the socket option
// propagated to setsockopt(). This is as close to an end-to-end test as we have for this feature,
// due to the complexity of creating an integration test involving the network stack. We only test
// the IPv4 case here, as the logic around IPv4/IPv6 handling is tested generically in
// socket_option_impl_test.cc.
class SockOptsTest : public ClusterManagerImplTest {
public:
  void initialize(const std::string& yaml) { create(parseBootstrapFromV3Yaml(yaml)); }

  void TearDown() override { factory_.tls_.shutdownThread(); }

  // TODO(tschroed): Extend this to support socket state as well.
  void expectSetsockopts(const NameVals& names_vals) {
    NiceMock<Api::MockOsSysCalls> os_sys_calls;
    TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
    NiceMock<Network::MockConnectionSocket> socket;
    bool expect_success = true;
    for (const auto& name_val : names_vals) {
      if (!name_val.first.hasValue()) {
        expect_success = false;
        continue;
      }
      EXPECT_CALL(socket,
                  setSocketOption(name_val.first.level(), name_val.first.option(), _, sizeof(int)))
          .WillOnce(
              Invoke([&name_val](int, int, const void* optval, socklen_t) -> Api::SysCallIntResult {
                EXPECT_EQ(name_val.second, *static_cast<const int*>(optval));
                return {0, 0};
              }));
    }
    EXPECT_CALL(socket, ipVersion())
        .WillRepeatedly(testing::Return(Network::Address::IpVersion::v4));
    EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Invoke([this, &names_vals, expect_success, &socket](
                             Network::Address::InstanceConstSharedPtr,
                             Network::Address::InstanceConstSharedPtr, Network::TransportSocketPtr&,
                             const Network::ConnectionSocket::OptionsSharedPtr& options)
                             -> Network::ClientConnection* {
          EXPECT_NE(nullptr, options.get()) << "Unexpected null options";
          if (options.get() != nullptr) { // Don't crash the entire test.
            EXPECT_EQ(names_vals.size(), options->size());
          }
          if (expect_success) {
            EXPECT_TRUE((Network::Socket::applyOptions(
                options, socket, envoy::config::core::v3::SocketOption::STATE_PREBIND)));
          } else {
            EXPECT_FALSE((Network::Socket::applyOptions(
                options, socket, envoy::config::core::v3::SocketOption::STATE_PREBIND)));
          }
          return connection_;
        }));
    cluster_manager_->getThreadLocalCluster("SockOptsCluster")->tcpConn(nullptr);
  }

  void expectSetsockoptFreebind() {
    NameVals names_vals{{ENVOY_SOCKET_IP_FREEBIND, 1}};
    if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
      names_vals.emplace_back(std::make_pair(ENVOY_SOCKET_SO_NOSIGPIPE, 1));
    }
    expectSetsockopts(names_vals);
  }

  void expectOnlyNoSigpipeOptions() {
    NameVals names_vals{{std::make_pair(ENVOY_SOCKET_SO_NOSIGPIPE, 1)}};
    expectSetsockopts(names_vals);
  }

  void expectNoSocketOptions() {
    EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(
            Invoke([this](Network::Address::InstanceConstSharedPtr,
                          Network::Address::InstanceConstSharedPtr, Network::TransportSocketPtr&,
                          const Network::ConnectionSocket::OptionsSharedPtr& options)
                       -> Network::ClientConnection* {
              EXPECT_TRUE(options != nullptr && options->empty());
              return connection_;
            }));
    auto conn_data = cluster_manager_->getThreadLocalCluster("SockOptsCluster")->tcpConn(nullptr);
    EXPECT_EQ(connection_, conn_data.connection_.get());
  }

  Network::MockClientConnection* connection_ = new NiceMock<Network::MockClientConnection>();
};

TEST_F(SockOptsTest, SockOptsUnset) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockOptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockOptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
  )EOF";
  initialize(yaml);
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    expectOnlyNoSigpipeOptions();
  } else {
    expectNoSocketOptions();
  }
}

TEST_F(SockOptsTest, FreebindClusterOnly) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockOptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockOptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_bind_config:
        freebind: true
  )EOF";
  initialize(yaml);
  expectSetsockoptFreebind();
}

TEST_F(SockOptsTest, FreebindClusterManagerOnly) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockOptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockOptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
  cluster_manager:
    upstream_bind_config:
      freebind: true
  )EOF";
  initialize(yaml);
  expectSetsockoptFreebind();
}

TEST_F(SockOptsTest, FreebindClusterOverride) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockOptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockOptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_bind_config:
        freebind: true
  cluster_manager:
    upstream_bind_config:
      freebind: false
  )EOF";
  initialize(yaml);
  expectSetsockoptFreebind();
}

TEST_F(SockOptsTest, SockOptsClusterOnly) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockOptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockOptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_bind_config:
        socket_options: [
          { level: 1, name: 2, int_value: 3, state: STATE_PREBIND },
          { level: 4, name: 5, int_value: 6, state: STATE_PREBIND }]

  )EOF";
  initialize(yaml);
  NameVals names_vals{{ENVOY_MAKE_SOCKET_OPTION_NAME(1, 2), 3},
                      {ENVOY_MAKE_SOCKET_OPTION_NAME(4, 5), 6}};
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    names_vals.emplace_back(std::make_pair(ENVOY_SOCKET_SO_NOSIGPIPE, 1));
  }
  expectSetsockopts(names_vals);
}

TEST_F(SockOptsTest, SockOptsClusterManagerOnly) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockOptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockOptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
  cluster_manager:
    upstream_bind_config:
      socket_options: [
        { level: 1, name: 2, int_value: 3, state: STATE_PREBIND },
        { level: 4, name: 5, int_value: 6, state: STATE_PREBIND }]
  )EOF";
  initialize(yaml);
  NameVals names_vals{{ENVOY_MAKE_SOCKET_OPTION_NAME(1, 2), 3},
                      {ENVOY_MAKE_SOCKET_OPTION_NAME(4, 5), 6}};
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    names_vals.emplace_back(std::make_pair(ENVOY_SOCKET_SO_NOSIGPIPE, 1));
  }
  expectSetsockopts(names_vals);
}

// The cluster options should override/replace the cluster manager options when both are specified.
TEST_F(SockOptsTest, SockOptsClusterAndClusterManagerNoAddress) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockOptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockOptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_bind_config:
        socket_options: [
          { level: 1, name: 2, int_value: 3, state: STATE_PREBIND }]
  cluster_manager:
    upstream_bind_config:
      socket_options: [
        { level: 4, name: 5, int_value: 6, state: STATE_PREBIND }]
  )EOF";
  initialize(yaml);
  NameVals names_vals{{ENVOY_MAKE_SOCKET_OPTION_NAME(1, 2), 3}};
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    names_vals.emplace_back(std::make_pair(ENVOY_SOCKET_SO_NOSIGPIPE, 1));
  }
  expectSetsockopts(names_vals);
}

// The cluster options should override/replace the cluster manager options when both are specified
// when a source_address is only specified on the cluster manager bind config.
TEST_F(SockOptsTest, SockOptsClusterAndClusterManagerAddress) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockOptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockOptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_bind_config:
        socket_options: [
          { level: 1, name: 2, int_value: 3, state: STATE_PREBIND }]
  cluster_manager:
    upstream_bind_config:
      source_address:
        address: 1.2.3.4
        port_value: 80
      socket_options: [
        { level: 4, name: 5, int_value: 6, state: STATE_PREBIND }]
  )EOF";
  initialize(yaml);
  NameVals names_vals{{ENVOY_MAKE_SOCKET_OPTION_NAME(1, 2), 3}};
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    names_vals.emplace_back(std::make_pair(ENVOY_SOCKET_SO_NOSIGPIPE, 1));
  }
  expectSetsockopts(names_vals);
}

TEST_F(SockOptsTest, SockOptsWithExtraSourceAddressAndOpts) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockOptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockOptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_bind_config:
        source_address:
          address: '::'
          port_value: 12345
        socket_options: [
          { level: 1, name: 2, int_value: 3, state: STATE_PREBIND },
          { level: 4, name: 5, int_value: 6, state: STATE_PREBIND }]
        extra_source_addresses:
        - address:
            address: 127.0.0.3
            port_value: 12345
          socket_options:
            socket_options: [
              { level: 10, name: 12, int_value: 13, state: STATE_PREBIND },
              { level: 14, name: 15, int_value: 16, state: STATE_PREBIND }]
  cluster_manager:
    upstream_bind_config:
      socket_options: [{ level: 7, name: 8, int_value: 9, state: STATE_PREBIND }]
  )EOF";
  initialize(yaml);
  NameVals names_vals{{ENVOY_MAKE_SOCKET_OPTION_NAME(10, 12), 13},
                      {ENVOY_MAKE_SOCKET_OPTION_NAME(14, 15), 16}};
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    names_vals.emplace_back(std::make_pair(ENVOY_SOCKET_SO_NOSIGPIPE, 1));
  }
  expectSetsockopts(names_vals);
}

TEST_F(SockOptsTest, SockOptsWithExtraSourceAddressAndEmptyOpts) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockOptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockOptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_bind_config:
        source_address:
          address: '::'
          port_value: 12345
        socket_options: [
          { level: 1, name: 2, int_value: 3, state: STATE_PREBIND },
          { level: 4, name: 5, int_value: 6, state: STATE_PREBIND }]
        extra_source_addresses:
        - address:
            address: 127.0.0.3
            port_value: 12345
          socket_options:
            socket_options: []
  cluster_manager:
    upstream_bind_config:
      socket_options: [{ level: 7, name: 8, int_value: 9, state: STATE_PREBIND }]
  )EOF";
  initialize(yaml);
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    expectOnlyNoSigpipeOptions();
  } else {
    expectNoSocketOptions();
  }
}

TEST_F(SockOptsTest, SockOptsWithExtraSourceAddressAndClusterOpts) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockOptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockOptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_bind_config:
        source_address:
          address: '::'
          port_value: 12345
        socket_options: [
          { level: 1, name: 2, int_value: 3, state: STATE_PREBIND },
          { level: 4, name: 5, int_value: 6, state: STATE_PREBIND }]
        extra_source_addresses:
        - address:
            address: 127.0.0.3
            port_value: 12345
  cluster_manager:
    upstream_bind_config:
      socket_options: [{ level: 7, name: 8, int_value: 9, state: STATE_PREBIND }]
  )EOF";
  initialize(yaml);
  NameVals names_vals{{ENVOY_MAKE_SOCKET_OPTION_NAME(1, 2), 3},
                      {ENVOY_MAKE_SOCKET_OPTION_NAME(4, 5), 6}};
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    names_vals.emplace_back(std::make_pair(ENVOY_SOCKET_SO_NOSIGPIPE, 1));
  }
  expectSetsockopts(names_vals);
}

TEST_F(SockOptsTest, SockOptsWithExtraSourceAddressAndClusterManagerOpts) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: SockOptsCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: SockOptsCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_bind_config:
        source_address:
          address: '::'
          port_value: 12345
        extra_source_addresses:
        - address:
            address: 127.0.0.3
            port_value: 12345
  cluster_manager:
    upstream_bind_config:
      socket_options: [{ level: 7, name: 8, int_value: 9, state: STATE_PREBIND }]
  )EOF";
  initialize(yaml);
  NameVals names_vals{{ENVOY_MAKE_SOCKET_OPTION_NAME(7, 8), 9}};
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    names_vals.emplace_back(std::make_pair(ENVOY_SOCKET_SO_NOSIGPIPE, 1));
  }
  expectSetsockopts(names_vals);
}

// Validate that when tcp keepalives are set in the Cluster, we see the socket
// option propagated to setsockopt(). This is as close to an end-to-end test as we have for this
// feature, due to the complexity of creating an integration test involving the network stack. We
// only test the IPv4 case here, as the logic around IPv4/IPv6 handling is tested generically in
// tcp_keepalive_option_impl_test.cc.
class TcpKeepaliveTest : public ClusterManagerImplTest {
public:
  void initialize(const std::string& yaml) { create(parseBootstrapFromV3Yaml(yaml)); }

  void TearDown() override { factory_.tls_.shutdownThread(); }

  void expectSetsockoptSoKeepalive(absl::optional<int> keepalive_probes,
                                   absl::optional<int> keepalive_time,
                                   absl::optional<int> keepalive_interval) {
    if (!ENVOY_SOCKET_SO_KEEPALIVE.hasValue()) {
      EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
          .WillOnce(
              Invoke([this](Network::Address::InstanceConstSharedPtr,
                            Network::Address::InstanceConstSharedPtr, Network::TransportSocketPtr&,
                            const Network::ConnectionSocket::OptionsSharedPtr& options)
                         -> Network::ClientConnection* {
                EXPECT_NE(nullptr, options.get());
                EXPECT_EQ(1, options->size());
                NiceMock<Network::MockConnectionSocket> socket;
                EXPECT_FALSE((Network::Socket::applyOptions(
                    options, socket, envoy::config::core::v3::SocketOption::STATE_PREBIND)));
                return connection_;
              }));
      cluster_manager_->getThreadLocalCluster("TcpKeepaliveCluster")->tcpConn(nullptr);
      return;
    }
    NiceMock<Api::MockOsSysCalls> os_sys_calls;
    TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
    NiceMock<Network::MockConnectionSocket> socket;
    EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Invoke([this, &socket](Network::Address::InstanceConstSharedPtr,
                                         Network::Address::InstanceConstSharedPtr,
                                         Network::TransportSocketPtr&,
                                         const Network::ConnectionSocket::OptionsSharedPtr& options)
                             -> Network::ClientConnection* {
          EXPECT_NE(nullptr, options.get());
          EXPECT_TRUE((Network::Socket::applyOptions(
              options, socket, envoy::config::core::v3::SocketOption::STATE_PREBIND)));
          return connection_;
        }));
    if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
      EXPECT_CALL(socket, setSocketOption(ENVOY_SOCKET_SO_NOSIGPIPE.level(),
                                          ENVOY_SOCKET_SO_NOSIGPIPE.option(), _, sizeof(int)))
          .WillOnce(Invoke([](int, int, const void* optval, socklen_t) -> Api::SysCallIntResult {
            EXPECT_EQ(1, *static_cast<const int*>(optval));
            return {0, 0};
          }));
    }
    EXPECT_CALL(socket, setSocketOption(ENVOY_SOCKET_SO_KEEPALIVE.level(),
                                        ENVOY_SOCKET_SO_KEEPALIVE.option(), _, sizeof(int)))
        .WillOnce(Invoke([](int, int, const void* optval, socklen_t) -> Api::SysCallIntResult {
          EXPECT_EQ(1, *static_cast<const int*>(optval));
          return {0, 0};
        }));
    if (keepalive_probes.has_value()) {
      EXPECT_CALL(socket, setSocketOption(ENVOY_SOCKET_TCP_KEEPCNT.level(),
                                          ENVOY_SOCKET_TCP_KEEPCNT.option(), _, sizeof(int)))
          .WillOnce(Invoke([&keepalive_probes](int, int, const void* optval,
                                               socklen_t) -> Api::SysCallIntResult {
            EXPECT_EQ(keepalive_probes.value(), *static_cast<const int*>(optval));
            return {0, 0};
          }));
    }
    if (keepalive_time.has_value()) {
      EXPECT_CALL(socket, setSocketOption(ENVOY_SOCKET_TCP_KEEPIDLE.level(),
                                          ENVOY_SOCKET_TCP_KEEPIDLE.option(), _, sizeof(int)))
          .WillOnce(Invoke(
              [&keepalive_time](int, int, const void* optval, socklen_t) -> Api::SysCallIntResult {
                EXPECT_EQ(keepalive_time.value(), *static_cast<const int*>(optval));
                return {0, 0};
              }));
    }
    if (keepalive_interval.has_value()) {
      EXPECT_CALL(socket, setSocketOption(ENVOY_SOCKET_TCP_KEEPINTVL.level(),
                                          ENVOY_SOCKET_TCP_KEEPINTVL.option(), _, sizeof(int)))
          .WillOnce(Invoke([&keepalive_interval](int, int, const void* optval,
                                                 socklen_t) -> Api::SysCallIntResult {
            EXPECT_EQ(keepalive_interval.value(), *static_cast<const int*>(optval));
            return {0, 0};
          }));
    }
    auto conn_data =
        cluster_manager_->getThreadLocalCluster("TcpKeepaliveCluster")->tcpConn(nullptr);
    EXPECT_EQ(connection_, conn_data.connection_.get());
  }

  void expectOnlyNoSigpipeOptions() {
    NiceMock<Network::MockConnectionSocket> socket;
    EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Invoke([this, &socket](Network::Address::InstanceConstSharedPtr,
                                         Network::Address::InstanceConstSharedPtr,
                                         Network::TransportSocketPtr&,
                                         const Network::ConnectionSocket::OptionsSharedPtr& options)
                             -> Network::ClientConnection* {
          EXPECT_NE(nullptr, options.get());
          EXPECT_TRUE((Network::Socket::applyOptions(
              options, socket, envoy::config::core::v3::SocketOption::STATE_PREBIND)));
          return connection_;
        }));
    EXPECT_CALL(socket, setSocketOption(ENVOY_SOCKET_SO_NOSIGPIPE.level(),
                                        ENVOY_SOCKET_SO_NOSIGPIPE.option(), _, sizeof(int)))
        .WillOnce(Invoke([](int, int, const void* optval, socklen_t) -> Api::SysCallIntResult {
          EXPECT_EQ(1, *static_cast<const int*>(optval));
          return {0, 0};
        }));
    auto conn_data =
        cluster_manager_->getThreadLocalCluster("TcpKeepaliveCluster")->tcpConn(nullptr);
    EXPECT_EQ(connection_, conn_data.connection_.get());
  }

  void expectNoSocketOptions() {
    EXPECT_CALL(factory_.tls_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(
            Invoke([this](Network::Address::InstanceConstSharedPtr,
                          Network::Address::InstanceConstSharedPtr, Network::TransportSocketPtr&,
                          const Network::ConnectionSocket::OptionsSharedPtr& options)
                       -> Network::ClientConnection* {
              EXPECT_TRUE(options != nullptr && options->empty());
              return connection_;
            }));
    auto conn_data =
        cluster_manager_->getThreadLocalCluster("TcpKeepaliveCluster")->tcpConn(nullptr);
    EXPECT_EQ(connection_, conn_data.connection_.get());
  }

  Network::MockClientConnection* connection_ = new NiceMock<Network::MockClientConnection>();
};

TEST_F(TcpKeepaliveTest, TcpKeepaliveUnset) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: TcpKeepaliveCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: TcpKeepaliveCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
  )EOF";
  initialize(yaml);
  if (ENVOY_SOCKET_SO_NOSIGPIPE.hasValue()) {
    expectOnlyNoSigpipeOptions();
  } else {
    expectNoSocketOptions();
  }
}

TEST_F(TcpKeepaliveTest, TcpKeepaliveCluster) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: TcpKeepaliveCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: TcpKeepaliveCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_connection_options:
        tcp_keepalive: {}
  )EOF";
  initialize(yaml);
  expectSetsockoptSoKeepalive({}, {}, {});
}

TEST_F(TcpKeepaliveTest, TcpKeepaliveClusterProbes) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: TcpKeepaliveCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: TcpKeepaliveCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_connection_options:
        tcp_keepalive:
          keepalive_probes: 7
  )EOF";
  initialize(yaml);
  expectSetsockoptSoKeepalive(7, {}, {});
}

TEST_F(TcpKeepaliveTest, TcpKeepaliveWithAllOptions) {
  const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: TcpKeepaliveCluster
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
      load_assignment:
        cluster_name: TcpKeepaliveCluster
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 11001
      upstream_connection_options:
        tcp_keepalive:
          keepalive_probes: 7
          keepalive_time: 4
          keepalive_interval: 1
  )EOF";
  initialize(yaml);
  expectSetsockoptSoKeepalive(7, 4, 1);
}

class PreconnectTest : public ClusterManagerImplTest {
public:
  void initialize(float ratio) {
    const std::string yaml = R"EOF(
  static_resources:
    clusters:
    - name: cluster_1
      connect_timeout: 0.250s
      lb_policy: ROUND_ROBIN
      type: STATIC
  )EOF";

    ReadyWatcher initialized;
    EXPECT_CALL(initialized, ready());
    Bootstrap config = parseBootstrapFromV3Yaml(yaml);
    if (ratio != 0) {
      config.mutable_static_resources()
          ->mutable_clusters(0)
          ->mutable_preconnect_policy()
          ->mutable_predictive_preconnect_ratio()
          ->set_value(ratio);
    }
    create(config);

    // Set up for an initialize callback.
    cluster_manager_->setInitializedCb([&]() -> void { initialized.ready(); });

    std::unique_ptr<MockClusterUpdateCallbacks> callbacks(
        new NiceMock<MockClusterUpdateCallbacks>());
    ClusterUpdateCallbacksHandlePtr cb =
        cluster_manager_->addThreadLocalClusterUpdateCallbacks(*callbacks);

    cluster_ = &cluster_manager_->activeClusters().begin()->second.get();

    // Set up the HostSet.
    host1_ = makeTestHost(cluster_->info(), "tcp://127.0.0.1:80");
    host2_ = makeTestHost(cluster_->info(), "tcp://127.0.0.1:80");
    host3_ = makeTestHost(cluster_->info(), "tcp://127.0.0.1:80");
    host4_ = makeTestHost(cluster_->info(), "tcp://127.0.0.1:80");

    HostVector hosts{host1_, host2_, host3_, host4_};
    auto hosts_ptr = std::make_shared<HostVector>(hosts);

    // Sending non-mergeable updates.
    cluster_->prioritySet().updateHosts(
        0, HostSetImpl::partitionHosts(hosts_ptr, HostsPerLocalityImpl::empty()), nullptr, hosts,
        {}, 123, absl::nullopt, 100);
  }

  Cluster* cluster_{};
  HostSharedPtr host1_;
  HostSharedPtr host2_;
  HostSharedPtr host3_;
  HostSharedPtr host4_;
  Http::MockResponseDecoder decoder_;
  Http::ConnectionPool::MockCallbacks http_callbacks_;
  Tcp::ConnectionPool::MockCallbacks tcp_callbacks_;
};

TEST_F(PreconnectTest, PreconnectOff) {
  // With preconnect set to 0, each request for a connection pool will only
  // allocate that conn pool.
  initialize(0);
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .Times(1)
      .WillRepeatedly(ReturnNew<NiceMock<Http::ConnectionPool::MockInstance>>());
  auto http_handle =
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, nullptr);
  http_handle.value().newStream(decoder_, http_callbacks_, {false, true});

  EXPECT_CALL(factory_, allocateTcpConnPool_(_))
      .Times(1)
      .WillRepeatedly(ReturnNew<NiceMock<Tcp::ConnectionPool::MockInstance>>());
  auto tcp_handle = cluster_manager_->getThreadLocalCluster("cluster_1")
                        ->tcpConnPool(ResourcePriority::Default, nullptr);
  ASSERT_TRUE(tcp_handle.has_value());
  tcp_handle.value().newConnection(tcp_callbacks_);
}

TEST_F(PreconnectTest, PreconnectOn) {
  // With preconnect set to 1.1, maybePreconnect will kick off
  // preconnecting, so create the pool for both the current connection and the
  // anticipated one.
  initialize(1.1);
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .Times(2)
      .WillRepeatedly(ReturnNew<NiceMock<Http::ConnectionPool::MockInstance>>());
  auto http_handle =
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, nullptr);
  http_handle.value().newStream(decoder_, http_callbacks_, {false, true});

  EXPECT_CALL(factory_, allocateTcpConnPool_)
      .Times(2)
      .WillRepeatedly(ReturnNew<NiceMock<Tcp::ConnectionPool::MockInstance>>());
  auto tcp_handle = cluster_manager_->getThreadLocalCluster("cluster_1")
                        ->tcpConnPool(ResourcePriority::Default, nullptr);
  ASSERT_TRUE(tcp_handle.has_value());
  tcp_handle.value().newConnection(tcp_callbacks_);
}

TEST_F(PreconnectTest, PreconnectOnWithOverrideHost) {
  // With preconnect set to 1.1, maybePreconnect will kick off
  // preconnecting, so create the pool for both the current connection and the
  // anticipated one.
  initialize(1.1);

  NiceMock<MockLoadBalancerContext> context;
  EXPECT_CALL(context, overrideHostToSelect())
      .WillRepeatedly(Return(absl::make_optional<Upstream::LoadBalancerContext::OverrideHost>(
          std::make_pair("127.0.0.1:80", false))));

  // Only allocate connection pool once.
  EXPECT_CALL(factory_, allocateTcpConnPool_)
      .WillOnce(ReturnNew<NiceMock<Tcp::ConnectionPool::MockInstance>>());
  auto tcp_handle = cluster_manager_->getThreadLocalCluster("cluster_1")
                        ->tcpConnPool(ResourcePriority::Default, &context);
  ASSERT_TRUE(tcp_handle.has_value());
  tcp_handle.value().newConnection(tcp_callbacks_);
}

TEST_F(PreconnectTest, PreconnectHighHttp) {
  // With preconnect set to 3, the first request will kick off 3 preconnect attempts.
  initialize(3);
  int http_preconnect = 0;
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .Times(4)
      .WillRepeatedly(InvokeWithoutArgs([&]() -> Http::ConnectionPool::Instance* {
        auto* ret = new NiceMock<Http::ConnectionPool::MockInstance>();
        ON_CALL(*ret, maybePreconnect(_)).WillByDefault(InvokeWithoutArgs([&]() -> bool {
          ++http_preconnect;
          return true;
        }));
        return ret;
      }));
  auto http_handle =
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, nullptr);
  http_handle.value().newStream(decoder_, http_callbacks_, {false, true});
  // Expect preconnect to be called 3 times across the four hosts.
  EXPECT_EQ(3, http_preconnect);
}

TEST_F(PreconnectTest, PreconnectHighTcp) {
  // With preconnect set to 3, the first request will kick off 3 preconnect attempts.
  initialize(3);
  int tcp_preconnect = 0;
  EXPECT_CALL(factory_, allocateTcpConnPool_(_))
      .Times(4)
      .WillRepeatedly(InvokeWithoutArgs([&]() -> Tcp::ConnectionPool::Instance* {
        auto* ret = new NiceMock<Tcp::ConnectionPool::MockInstance>();
        ON_CALL(*ret, maybePreconnect(_)).WillByDefault(InvokeWithoutArgs([&]() -> bool {
          ++tcp_preconnect;
          return true;
        }));
        return ret;
      }));
  auto tcp_handle = cluster_manager_->getThreadLocalCluster("cluster_1")
                        ->tcpConnPool(ResourcePriority::Default, nullptr);
  tcp_handle.value().newConnection(tcp_callbacks_);
  // Expect preconnect to be called 3 times across the four hosts.
  EXPECT_EQ(3, tcp_preconnect);
}

TEST_F(PreconnectTest, PreconnectCappedAt3) {
  // With preconnect set to 20, no more than 3 connections will be preconnected.
  initialize(20);
  int http_preconnect = 0;
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .Times(4)
      .WillRepeatedly(InvokeWithoutArgs([&]() -> Http::ConnectionPool::Instance* {
        auto* ret = new NiceMock<Http::ConnectionPool::MockInstance>();
        ON_CALL(*ret, maybePreconnect(_)).WillByDefault(InvokeWithoutArgs([&]() -> bool {
          ++http_preconnect;
          return true;
        }));
        return ret;
      }));
  auto http_handle = cluster_manager_->getThreadLocalCluster("cluster_1")
                         ->httpConnPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                            ->loadBalancer()
                                            .chooseHost(nullptr)
                                            .host,
                                        ResourcePriority::Default, Http::Protocol::Http11, nullptr);
  http_handle.value().newStream(decoder_, http_callbacks_, {false, true});
  // Expect preconnect to be called 3 times across the four hosts.
  EXPECT_EQ(3, http_preconnect);

  // A subsequent call to get a connection will consume one of the preconnected
  // connections, leaving two in queue, and kick off 2 more. This time we won't
  // do the full 3 as the number of outstanding preconnects is limited by the
  // number of healthy hosts.
  http_preconnect = 0;
  http_handle = cluster_manager_->getThreadLocalCluster("cluster_1")
                    ->httpConnPool(cluster_manager_->getThreadLocalCluster("cluster_1")
                                       ->loadBalancer()
                                       .chooseHost(nullptr)
                                       .host,
                                   ResourcePriority::Default, Http::Protocol::Http11, nullptr);
  http_handle.value().newStream(decoder_, http_callbacks_, {false, true});
  EXPECT_EQ(2, http_preconnect);
}

TEST_F(PreconnectTest, PreconnectCappedByMaybePreconnect) {
  // Set preconnect high, and verify preconnecting stops when maybePreconnect returns false.
  initialize(20);
  int http_preconnect_calls = 0;
  EXPECT_CALL(factory_, allocateConnPool_(_, _, _, _, _, _, _))
      .Times(2)
      .WillRepeatedly(InvokeWithoutArgs([&]() -> Http::ConnectionPool::Instance* {
        auto* ret = new NiceMock<Http::ConnectionPool::MockInstance>();
        ON_CALL(*ret, maybePreconnect(_)).WillByDefault(InvokeWithoutArgs([&]() -> bool {
          ++http_preconnect_calls;
          // Force maybe preconnect to fail.
          return false;
        }));
        return ret;
      }));
  auto http_handle =
      cluster_manager_->getThreadLocalCluster("cluster_1")
          ->httpConnPool(
              cluster_manager_->getThreadLocalCluster("cluster_1")->chooseHost(nullptr).host,
              ResourcePriority::Default, Http::Protocol::Http11, nullptr);
  http_handle.value().newStream(decoder_, http_callbacks_, {false, true});
  // Expect preconnect to be called once and then preconnecting is stopped.
  EXPECT_EQ(1, http_preconnect_calls);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
