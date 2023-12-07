#include <chrono>
#include <memory>
#include <string>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/router/rds_impl.h"
#include "source/common/router/route_config_update_receiver_impl.h"

#ifdef ENVOY_ADMIN_FUNCTIONALITY
#include "source/server/admin/admin.h"
#endif
#include "test/mocks/config/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

class VhdsTest : public testing::Test {
public:
  void SetUp() override {
    default_vhds_config_ = R"EOF(
name: my_route
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
)EOF";
  }

  envoy::config::route::v3::VirtualHost buildVirtualHost(const std::string& name,
                                                         const std::string& domain) {
    return TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(fmt::format(R"EOF(
      name: {}
      domains: [{}]
      routes:
      - match: {{ prefix: "/" }}
        route: {{ cluster: "my_service" }}
    )EOF",
                                                                                     name, domain));
  }

  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>
  buildAddedResources(const std::vector<envoy::config::route::v3::VirtualHost>& added_or_updated) {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> to_ret;

    for (const auto& vhost : added_or_updated) {
      auto* resource = to_ret.Add();
      resource->set_name(vhost.name());
      resource->set_version("1");
      resource->mutable_resource()->PackFrom(vhost);
    }

    return to_ret;
  }

  Protobuf::RepeatedPtrField<std::string>
  buildRemovedResources(const std::vector<std::string>& removed) {
    return Protobuf::RepeatedPtrField<std::string>{removed.begin(), removed.end()};
  }
  RouteConfigUpdatePtr
  makeRouteConfigUpdate(const envoy::config::route::v3::RouteConfiguration& rc) {
    RouteConfigUpdatePtr config_update_info =
        std::make_unique<RouteConfigUpdateReceiverImpl>(proto_traits_, factory_context_);
    config_update_info->onRdsUpdate(rc, "1");
    return config_update_info;
  }

  ProtoTraitsImpl proto_traits_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  Init::ExpectableWatcherImpl init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  const std::string context_ = "vhds_test";
  Envoy::Rds::RouteConfigProvider* provider_ = nullptr;
  Protobuf::util::MessageDifferencer messageDifferencer_;
  std::string default_vhds_config_;
  NiceMock<Envoy::Config::MockSubscriptionFactory> subscription_factory_;
};

// verify that api_type: DELTA_GRPC passes validation
TEST_F(VhdsTest, VhdsInstantiationShouldSucceedWithDELTA_GRPC) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  EXPECT_NO_THROW(VhdsSubscription(config_update_info, factory_context_, context_, provider_));
}

// verify that api_type: GRPC fails validation
TEST_F(VhdsTest, VhdsInstantiationShouldFailWithoutDELTA_GRPC) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
vhds:
  config_source:
    api_config_source:
      api_type: GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
  )EOF");
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  EXPECT_THROW(VhdsSubscription(config_update_info, factory_context_, context_, provider_),
               EnvoyException);
}

// verify addition/updating of virtual hosts
TEST_F(VhdsTest, VhdsAddsVirtualHosts) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  VhdsSubscription subscription(config_update_info, factory_context_, context_, provider_);
  EXPECT_EQ(0UL, config_update_info->protobufConfigurationCast().virtual_hosts_size());

  auto vhost = buildVirtualHost("vhost1", "vhost.first");
  const auto& added_resources = buildAddedResources({vhost});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_TRUE(factory_context_.cluster_manager_.subscription_factory_.callbacks_
                  ->onConfigUpdate(decoded_resources.refvec_, removed_resources, "1")
                  .ok());

  EXPECT_EQ(1UL, config_update_info->protobufConfigurationCast().virtual_hosts_size());
  EXPECT_TRUE(messageDifferencer_.Equals(
      vhost, config_update_info->protobufConfigurationCast().virtual_hosts(0)));
}

// verify that an RDS update of virtual hosts leaves VHDS virtual hosts intact
TEST_F(VhdsTest, RdsUpdatesVirtualHosts) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
virtual_hosts:
- name: vhost_rds1
  domains: ["vhost.rds.first"]
  routes:
  - match: { prefix: "/rdsone" }
    route: { cluster: my_service }
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
  )EOF");
  const auto updated_route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
virtual_hosts:
- name: vhost_rds1
  domains: ["vhost.rds.first"]
  routes:
  - match: { prefix: "/rdsone" }
    route: { cluster: my_service }
- name: vhost_rds2
  domains: ["vhost.rds.second"]
  routes:
  - match: { prefix: "/rdstwo" }
    route: { cluster: my_other_service }
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
  )EOF");
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  VhdsSubscription subscription(config_update_info, factory_context_, context_, provider_);
  EXPECT_EQ(1UL, config_update_info->protobufConfigurationCast().virtual_hosts_size());
  EXPECT_EQ("vhost_rds1", config_update_info->protobufConfigurationCast().virtual_hosts(0).name());

  auto vhost = buildVirtualHost("vhost_vhds1", "vhost.first");
  const auto& added_resources = buildAddedResources({vhost});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_TRUE(factory_context_.cluster_manager_.subscription_factory_.callbacks_
                  ->onConfigUpdate(decoded_resources.refvec_, removed_resources, "1")
                  .ok());
  EXPECT_EQ(2UL, config_update_info->protobufConfigurationCast().virtual_hosts_size());

  config_update_info->onRdsUpdate(updated_route_config, "2");

  EXPECT_EQ(3UL, config_update_info->protobufConfigurationCast().virtual_hosts_size());
  auto actual_vhost_0 = config_update_info->protobufConfigurationCast().virtual_hosts(0);
  auto actual_vhost_1 = config_update_info->protobufConfigurationCast().virtual_hosts(1);
  auto actual_vhost_2 = config_update_info->protobufConfigurationCast().virtual_hosts(2);
  EXPECT_TRUE("vhost_rds1" == actual_vhost_0.name() || "vhost_rds1" == actual_vhost_1.name() ||
              "vhost_rds1" == actual_vhost_2.name());
  EXPECT_TRUE("vhost_rds2" == actual_vhost_0.name() || "vhost_rds2" == actual_vhost_1.name() ||
              "vhost_rds2" == actual_vhost_2.name());
  EXPECT_TRUE("vhost_vhds1" == actual_vhost_0.name() || "vhost_vhds1" == actual_vhost_1.name() ||
              "vhost_vhds1" == actual_vhost_2.name());
}

} // namespace
} // namespace Router
} // namespace Envoy
