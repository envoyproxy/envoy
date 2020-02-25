#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/egds.pb.h"
#include "envoy/stats/scope.h"

#include "common/config/utility.h"
#include "common/singleton/manager_impl.h"
#include "common/upstream/eds.h"
#include "common/upstream/egds_cluster_mapper.h"

#include "server/transport_socket_config_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Upstream {
namespace {

class EgdsClusterMapperTest : public testing::Test {
protected:
  EgdsClusterMapperTest() : api_(Api::createApiForTest(stats_)) { resetCluster(); }

  void resetCluster() {
    resetCluster(R"EOF(
      name: name
      connect_timeout: 0.25s
      type: EDS
      lb_policy: ROUND_ROBIN
      eds_cluster_config:
        service_name: fare
        eds_config:
          api_config_source:
            api_type: REST
            cluster_names:
            - eds
            refresh_delay: 1s
    )EOF",
                 Cluster::InitializePhase::Secondary);
  }

  void resetCluster(const std::string& yaml_config, Cluster::InitializePhase initialize_phase) {
    local_info_.node_.mutable_locality()->set_zone("us-east-1a");
    eds_cluster_ = parseClusterFromV2Yaml(yaml_config);
    Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
        "cluster.{}.",
        eds_cluster_.alt_stat_name().empty() ? eds_cluster_.name() : eds_cluster_.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
        singleton_manager_, tls_, validation_visitor_, *api_);
    cluster_.reset(
        new EdsClusterImpl(eds_cluster_, runtime_, factory_context, std::move(scope), false));
    EXPECT_EQ(initialize_phase, cluster_->initializePhase());
    eds_callbacks_ = cm_.subscription_factory_.callbacks_;
  }

  Stats::IsolatedStoreImpl stats_;
  Ssl::MockContextManager ssl_context_manager_;
  envoy::config::cluster::v3::Cluster eds_cluster_;
  NiceMock<MockClusterManager> cm_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<EdsClusterImpl> cluster_;
  Config::SubscriptionCallbacks* eds_callbacks_{};
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::MockAdmin> admin_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_;
};

TEST_F(EgdsClusterMapperTest, Basic) {
  MockEndpointGroupMonitorManager mock_manager;
  MockEgdsClusterMapperDelegate mock_delegate;
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  EgdsClusterMapper mapper(mock_manager, mock_delegate, *cluster_, cluster_load_assignment,
                           local_info_);

  const std::vector<std::string> egds_resource_names = {"test1", "test2", "test3"};
  EndpointGroupMonitorSharedPtr active_monitor_test1, active_monitor_test2, active_monitor_test3;

  {
    EXPECT_FALSE(mapper.resourceExists(egds_resource_names.at(0)));
    EXPECT_CALL(mock_manager, addMonitor(_, _))
        .WillOnce(Invoke(
            [&](EndpointGroupMonitorSharedPtr monitor, absl::string_view group_name) -> void {
              active_monitor_test1 = monitor;
              EXPECT_STREQ(egds_resource_names.at(0).c_str(), group_name.data());
            }));
    mapper.addResource(egds_resource_names.at(0));
    EXPECT_TRUE(mapper.resourceExists(egds_resource_names.at(0)));
  }

  {
    EXPECT_CALL(mock_manager, addMonitor(_, _))
        .WillOnce(Invoke(
            [&](EndpointGroupMonitorSharedPtr monitor, absl::string_view group_name) -> void {
              active_monitor_test2 = monitor;
              EXPECT_STREQ(egds_resource_names.at(1).c_str(), group_name.data());
            }));
    mapper.addResource(egds_resource_names.at(1));
    EXPECT_TRUE(mapper.resourceExists(egds_resource_names.at(1)));
  }

  {
    EXPECT_CALL(mock_manager, addMonitor(_, _))
        .WillOnce(Invoke(
            [&](EndpointGroupMonitorSharedPtr monitor, absl::string_view group_name) -> void {
              active_monitor_test3 = monitor;
              EXPECT_STREQ(egds_resource_names.at(2).c_str(), group_name.data());
            }));
    mapper.addResource(egds_resource_names.at(2));
    EXPECT_TRUE(mapper.resourceExists(egds_resource_names.at(2)));
  }

  auto egds_names = mapper.egds_resource_names();
  for (const auto& name : egds_resource_names) {
    EXPECT_TRUE(egds_names.count(name));
  }

  {
    // The onUpdate interface is not called because the other two dependent resources are not
    // retrieved, test_data_2 and test_data_3 isn't ready.
    EXPECT_CALL(mock_delegate, initializeCluster(_)).Times(0);
    const std::string version("1.0");
    envoy::config::endpoint::v3::EndpointGroup group_data;
    group_data.set_name("test_data_1");
    auto* endpoints = group_data.add_endpoints();
    auto* socket_address = endpoints->add_lb_endpoints()
                               ->mutable_endpoint()
                               ->mutable_address()
                               ->mutable_socket_address();
    socket_address->set_address("1.2.3.4");
    socket_address->set_port_value(1234);
    active_monitor_test1->update(group_data, version);
  }

  {
    // The onUpdate interface is not called because the other dependent resources are not
    // retrieved, test_data_3 isn't ready.
    EXPECT_CALL(mock_delegate, initializeCluster(_)).Times(0);
    const std::string version("1.0");
    envoy::config::endpoint::v3::EndpointGroup group_data;
    group_data.set_name("test_data_2");
    auto* endpoints = group_data.add_endpoints();
    auto* socket_address = endpoints->add_lb_endpoints()
                               ->mutable_endpoint()
                               ->mutable_address()
                               ->mutable_socket_address();
    socket_address->set_address("2.3.4.5");
    socket_address->set_port_value(2345);
    active_monitor_test2->update(group_data, version);
  }

  {
    // The onUpdate interface is called because the dependent resources has been
    // retrieved.
    EXPECT_CALL(mock_delegate, initializeCluster(_))
        .WillOnce(Invoke(
            [&](const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment)
                -> void { EXPECT_EQ(3, cluster_load_assignment.endpoints_size()); }));
    const std::string version("1.0");
    envoy::config::endpoint::v3::EndpointGroup group_data;
    group_data.set_name("test_data_3");
    auto* endpoints = group_data.add_endpoints();
    auto* socket_address = endpoints->add_lb_endpoints()
                               ->mutable_endpoint()
                               ->mutable_address()
                               ->mutable_socket_address();
    socket_address->set_address("3.4.5.6");
    socket_address->set_port_value(3456);
    active_monitor_test3->update(group_data, version);
  }

  {
    // The onUpdate interface is called because the dependent resources has been
    // retrieved.
    EXPECT_CALL(mock_delegate, initializeCluster(_)).Times(0);
    EXPECT_CALL(mock_delegate, updateHosts(_, _, _, _, _, _))
        .WillOnce(Invoke([&](uint32_t priority, const HostVector& hosts_added,
                             const HostVector& hosts_removed, PriorityStateManager&,
                             LocalityWeightsMap&, absl::optional<uint32_t>) -> void {
          EXPECT_EQ(0, priority);
          EXPECT_EQ("4.5.6.7:4567", hosts_added.at(0)->address()->asString());
          EXPECT_EQ("1.2.3.4:1234", hosts_removed.at(0)->address()->asString());
        }));
    const std::string version("1.0");
    envoy::config::endpoint::v3::EndpointGroup group_data;
    group_data.set_name("test_data_1");
    auto* endpoints = group_data.add_endpoints();
    auto* socket_address = endpoints->add_lb_endpoints()
                               ->mutable_endpoint()
                               ->mutable_address()
                               ->mutable_socket_address();
    socket_address->set_address("4.5.6.7");
    socket_address->set_port_value(4567);
    active_monitor_test1->update(group_data, version);

    const auto& hosts_set = mapper.getPrioritySetInMonitorForTest("test1").hostSetsPerPriority()[0];
    const auto& hosts = hosts_set->hosts();
    EXPECT_EQ(1, hosts.size());
    EXPECT_EQ("4.5.6.7:4567", hosts.at(0)->address()->asString());
  }

  EXPECT_CALL(mock_manager, removeMonitor(_, _)).Times(1);
  mapper.removeResource(egds_resource_names.at(0));
  EXPECT_FALSE(mapper.resourceExists(egds_resource_names.at(0)));

  EXPECT_CALL(mock_manager, removeMonitor(_, _)).Times(1);
  mapper.removeResource(egds_resource_names.at(2));
  EXPECT_FALSE(mapper.resourceExists(egds_resource_names.at(2)));

  // Adding duplicate resources.
  EXPECT_CALL(mock_manager, addMonitor(_, _)).Times(0);
  mapper.addResource(egds_resource_names.at(1));
}

TEST_F(EgdsClusterMapperTest, RemoveInactiveHosts) {
  MockEndpointGroupMonitorManager mock_manager;
  MockEgdsClusterMapperDelegate mock_delegate;
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  EgdsClusterMapper mapper(mock_manager, mock_delegate, *cluster_, cluster_load_assignment,
                           local_info_);

  const std::vector<std::string> egds_resource_names = {"test1", "test2"};
  EndpointGroupMonitorSharedPtr active_monitor_test1, active_monitor_test2;

  EXPECT_CALL(mock_manager, addMonitor(_, _))
      .WillOnce(
          Invoke([&](EndpointGroupMonitorSharedPtr monitor, absl::string_view group_name) -> void {
            active_monitor_test1 = monitor;
            EXPECT_STREQ(egds_resource_names.at(0).c_str(), group_name.data());
          }));
  mapper.addResource(egds_resource_names.at(0));

  EXPECT_CALL(mock_manager, addMonitor(_, _))
      .WillOnce(
          Invoke([&](EndpointGroupMonitorSharedPtr monitor, absl::string_view group_name) -> void {
            active_monitor_test2 = monitor;
            EXPECT_STREQ(egds_resource_names.at(1).c_str(), group_name.data());
          }));
  mapper.addResource(egds_resource_names.at(1));

  {
    // The onUpdate interface is not called because the other two dependent resources are not
    // retrieved, test_data_2 and test_data_3 isn't ready.
    EXPECT_CALL(mock_delegate, initializeCluster(_)).Times(0);
    const std::string version("1.0");
    envoy::config::endpoint::v3::EndpointGroup group_data;
    group_data.set_name("test_data_1");
    auto* endpoints = group_data.add_endpoints();
    auto* socket_address = endpoints->add_lb_endpoints()
                               ->mutable_endpoint()
                               ->mutable_address()
                               ->mutable_socket_address();
    socket_address->set_address("1.2.3.4");
    socket_address->set_port_value(1234);
    active_monitor_test1->update(group_data, version);
  }

  {
    // The onUpdate interface is not called because the other dependent resources are not
    // retrieved, test_data_3 isn't ready.
    EXPECT_CALL(mock_delegate, initializeCluster(_)).Times(1);
    const std::string version("1.0");
    envoy::config::endpoint::v3::EndpointGroup group_data;
    group_data.set_name("test_data_2");
    auto* endpoints = group_data.add_endpoints();
    auto* socket_address = endpoints->add_lb_endpoints()
                               ->mutable_endpoint()
                               ->mutable_address()
                               ->mutable_socket_address();
    socket_address->set_address("2.3.4.5");
    socket_address->set_port_value(2345);

    auto* endpoints_2 = group_data.add_endpoints();
    auto* socket_address_2 = endpoints_2->add_lb_endpoints()
                                 ->mutable_endpoint()
                                 ->mutable_address()
                                 ->mutable_socket_address();
    socket_address_2->set_address("8.3.4.5");
    socket_address_2->set_port_value(2345);

    active_monitor_test2->update(group_data, version);
  }

  std::shared_ptr<Upstream::MockHost> mock_host(new NiceMock<Upstream::MockHost>());
  auto address_ptr = Network::Utility::resolveUrl("tcp://2.3.4.5:2345");
  ON_CALL(*mock_host, address()).WillByDefault(Return(address_ptr));
  ON_CALL(*mock_host, healthFlagGet(_)).WillByDefault(Return(true));
  mapper.reloadHealthyHostsHelper(0, mock_host);

  const auto& hosts_set = mapper.getPrioritySetInMonitorForTest("test2").hostSetsPerPriority()[0];
  const auto& hosts = hosts_set->hosts();
  EXPECT_EQ(1, hosts.size());

  const auto existing_itr =
      std::find_if(hosts.begin(), hosts.end(), [address_ptr](const HostSharedPtr current) {
        return current->address()->asString() == address_ptr->asString();
      });
  EXPECT_TRUE(existing_itr == hosts.end());
}

} // namespace
} // namespace Upstream
} // namespace Envoy
