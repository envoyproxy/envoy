#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/config/utility.h"
#include "common/protobuf/utility.h"
#include "common/upstream/egds_api_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Return;
using testing::StrEq;
using testing::Throw;

namespace Envoy {
namespace Upstream {
namespace {

inline envoy::config::endpoint::v3::ClusterLoadAssignment
parseClusterLoadAssignment(const std::string& yaml_config) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;
  TestUtility::loadFromYaml(yaml_config, cluster_load_assignment);
  return cluster_load_assignment;
}

class EgdsApiImplTest : public testing::Test {
protected:
  EgdsApiImplTest() { resetClusterLoadAssignment(); }
  void resetClusterLoadAssignment() {
    resetClusterLoadAssignment(R"EOF(
      cluster_name: cluster_0
      endpoint_groups:
        - config_source:
            path: egds path
          endpoint_group_name: group_name
    )EOF");
  }

  void resetMultiGroupClusterLoadAssignment() {
    resetClusterLoadAssignment(R"EOF(
      cluster_name: cluster_0
      endpoint_groups:
        - config_source:
            path: egds path
          endpoint_group_name: group_name_1
        - config_source:
            path: egds path
          endpoint_group_name: group_name_2
    )EOF");
  }

  void resetClusterLoadAssignment(const std::string& yaml_config) {
    cluster_load_assignment_ = parseClusterLoadAssignment(yaml_config);
    auto scope =
        stats_.createScope(fmt::format("cluster.{}", cluster_load_assignment_.cluster_name()));
    egds_api_ =
        std::make_unique<EgdsApiImpl>(eg_manager_, validation_visitor_, subscription_factory_,
                                      *scope, cluster_load_assignment_.endpoint_groups());
    egds_callbacks_ = subscription_factory_.callbacks_;
  }

  void initialize() {
    EXPECT_CALL(*subscription_factory_.subscription_, start(_));
    egds_api_->initialize([&] { this->initialization_fail_ = true; });
  }

  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment_;
  NiceMock<MockEgdsClusterMapperDelegate> delegate_;
  NiceMock<MockEndpointGroupMonitorManager> egds_monitor_manager_;
  NiceMock<MockEndpointGroupsManager> eg_manager_;
  NiceMock<MockEndpointGroupMonitor> egds_monitor_;
  NiceMock<Config::MockSubscriptionFactory> subscription_factory_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Stats::IsolatedStoreImpl stats_;
  EgdsApiPtr egds_api_;
  Config::SubscriptionCallbacks* egds_callbacks_{};
  bool initialization_fail_{false};
};

TEST_F(EgdsApiImplTest, OnConfigUpdateEmpty) {
  initialize();
  EXPECT_NE(nullptr, egds_callbacks_);
  egds_callbacks_->onConfigUpdate({}, "");
  EXPECT_EQ(1UL, stats_.counter("cluster.cluster_0.egds.update_empty").value());
}

TEST_F(EgdsApiImplTest, onConfigUpdateWrongName) {
  initialize();
  envoy::config::endpoint::v3::EndpointGroup endpoint_group;
  endpoint_group.set_name("wrong name");
  auto* endpoints = endpoint_group.add_endpoints();
  auto* endpoint = endpoints->add_lb_endpoints();

  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("1.2.3.4");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
  resources.Add()->PackFrom(endpoint_group);
  // TODO(jingyi.lyq) should throw exception when group not be subscribed
  // EXPECT_THROW(egds_callbacks_->onConfigUpdate(resources, ""), EnvoyException);
}

TEST_F(EgdsApiImplTest, onConfigUpdateDuplicateEnpointGroup) {
  initialize();
  envoy::config::endpoint::v3::EndpointGroup endpoint_group;
  endpoint_group.set_name("group_name");
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
  auto* endpoints = endpoint_group.add_endpoints();
  auto* endpoint = endpoints->add_lb_endpoints();

  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("1.2.3.4");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);
  resources.Add()->PackFrom(endpoint_group);
  resources.Add()->PackFrom(endpoint_group);
  EXPECT_THROW(egds_callbacks_->onConfigUpdate(resources, ""), EnvoyException);
}

TEST_F(EgdsApiImplTest, onConfigUpdateSuccess) {
  initialize();
  envoy::config::endpoint::v3::EndpointGroup endpoint_group;
  endpoint_group.set_name("group_name");
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
  auto* endpoints = endpoint_group.add_endpoints();
  auto* endpoint = endpoints->add_lb_endpoints();

  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("1.2.3.4");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);
  resources.Add()->PackFrom(endpoint_group);

  EXPECT_CALL(eg_manager_, addOrUpdateEndpointGroup(_, _)).Times(0);
  EXPECT_CALL(eg_manager_, batchUpdateEndpointGroup(_, _, _))
      .WillOnce(Invoke([&](const std::vector<envoy::config::endpoint::v3::EndpointGroup>& added,
                           const std::vector<std::string> removed, absl::string_view) -> bool {
        EXPECT_EQ(1, added.size());
        EXPECT_EQ(0, removed.size());
        return true;
      }));
  egds_callbacks_->onConfigUpdate(resources, "");
}

TEST_F(EgdsApiImplTest, onConfigUpdateReplace) {
  initialize();
  envoy::config::endpoint::v3::EndpointGroup endpoint_group;
  endpoint_group.set_name("group_name");
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
  auto* endpoints = endpoint_group.add_endpoints();
  auto* endpoint = endpoints->add_lb_endpoints();

  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("1.2.3.4");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);
  resources.Add()->PackFrom(endpoint_group);
  egds_callbacks_->onConfigUpdate(resources, "");

  EXPECT_CALL(eg_manager_, addOrUpdateEndpointGroup(_, _)).Times(0);
  EXPECT_CALL(eg_manager_, batchUpdateEndpointGroup(_, _, _))
      .WillOnce(Invoke([&](const std::vector<envoy::config::endpoint::v3::EndpointGroup>& added,
                           const std::vector<std::string> removed, absl::string_view) -> bool {
        EXPECT_EQ(1, added.size());
        EXPECT_EQ(0, removed.size());
        return true;
      }));
  egds_callbacks_->onConfigUpdate(resources, "");
}

TEST_F(EgdsApiImplTest, onConfigUpdateRemove) {
  initialize();
  envoy::config::endpoint::v3::EndpointGroup endpoint_group;
  endpoint_group.set_name("group_name");
  Protobuf::RepeatedPtrField<ProtobufWkt::Any> resources;
  auto* endpoints = endpoint_group.add_endpoints();
  auto* endpoint = endpoints->add_lb_endpoints();

  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address("1.2.3.4");
  endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(80);
  resources.Add()->PackFrom(endpoint_group);
  egds_callbacks_->onConfigUpdate(resources, "");

  Protobuf::RepeatedPtrField<ProtobufWkt::Any> new_resources;
  endpoint_group.clear_endpoints();
  new_resources.Add()->PackFrom(endpoint_group);
  egds_callbacks_->onConfigUpdate(resources, "");

  EXPECT_CALL(eg_manager_, clearEndpointGroup(_, _)).Times(0);
  EXPECT_CALL(eg_manager_, batchUpdateEndpointGroup(_, _, _))
      .WillOnce(Invoke([&](const std::vector<envoy::config::endpoint::v3::EndpointGroup>& added,
                           const std::vector<std::string> removed, absl::string_view) -> bool {
        EXPECT_EQ(0, added.size());
        EXPECT_EQ(1, removed.size());
        return true;
      }));
  egds_callbacks_->onConfigUpdate(new_resources, "");
}

TEST_F(EgdsApiImplTest, multiConfigSource) {
  // Two config sources of the same type.
  {
    const std::string yaml_config = R"EOF(
      cluster_name: cluster_0
      endpoint_groups:
        - config_source:
            path: egds path
          endpoint_group_name: group_name_1
        - config_source:
            path: egds path
          endpoint_group_name: group_name_2
    )EOF";
    cluster_load_assignment_ = parseClusterLoadAssignment(yaml_config);
    auto scope =
        stats_.createScope(fmt::format("cluster.{}", cluster_load_assignment_.cluster_name()));
    std::unordered_map<Config::MockSubscription*, Config::SubscriptionCallbacks*> subscription_map;
    ON_CALL(subscription_factory_, subscriptionFromConfigSource(_, _, _, _))
        .WillByDefault(testing::Invoke(
            [&](const envoy::config::core::v3::ConfigSource&, absl::string_view, Stats::Scope&,
                Config::SubscriptionCallbacks& callbacks) -> Config::SubscriptionPtr {
              auto ret = std::make_unique<testing::NiceMock<Config::MockSubscription>>();
              subscription_map.emplace(ret.get(), &callbacks);
              return ret;
            }));
    egds_api_ =
        std::make_unique<EgdsApiImpl>(eg_manager_, validation_visitor_, subscription_factory_,
                                      *scope, cluster_load_assignment_.endpoint_groups());
    EXPECT_EQ(1, subscription_map.size());
  }

  // Two config sources of different types.
  {
    const std::string yaml_config = R"EOF(
      cluster_name: cluster_0
      endpoint_groups:
        - config_source:
            path: egds path
          endpoint_group_name: group_name_1
        - config_source:
            api_config_source:
              api_type: DELTA_GRPC
              grpc_services:
                envoy_grpc:
                  cluster_name: xds_cluster
          endpoint_group_name: group_name_2
    )EOF";
    cluster_load_assignment_ = parseClusterLoadAssignment(yaml_config);
    auto scope =
        stats_.createScope(fmt::format("cluster.{}", cluster_load_assignment_.cluster_name()));
    std::unordered_map<Config::MockSubscription*, Config::SubscriptionCallbacks*> subscription_map;
    ON_CALL(subscription_factory_, subscriptionFromConfigSource(_, _, _, _))
        .WillByDefault(testing::Invoke(
            [&](const envoy::config::core::v3::ConfigSource&, absl::string_view, Stats::Scope&,
                Config::SubscriptionCallbacks& callbacks) -> Config::SubscriptionPtr {
              auto ret = std::make_unique<testing::NiceMock<Config::MockSubscription>>();
              subscription_map.emplace(ret.get(), &callbacks);
              return ret;
            }));
    egds_api_ =
        std::make_unique<EgdsApiImpl>(eg_manager_, validation_visitor_, subscription_factory_,
                                      *scope, cluster_load_assignment_.endpoint_groups());
    EXPECT_EQ(2, subscription_map.size());

    uint8_t i = 1;
    for (const auto& pair : subscription_map) {
      EXPECT_CALL(*pair.first, start(_))
          .WillOnce(Invoke([&](const std::set<std::string> resources) -> void {
            EXPECT_TRUE(resources.count(fmt::format("group_name_{}", i++)));
            EXPECT_EQ(1, resources.size());
          }));
    }
    egds_api_->initialize([&] { this->initialization_fail_ = true; });
  }
}

} // namespace
} // namespace Upstream
} // namespace Envoy