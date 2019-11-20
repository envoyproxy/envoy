#include <chrono>
#include <memory>
#include <string>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/admin/v2alpha/config_dump.pb.validate.h"
#include "envoy/stats/scope.h"

#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/router/rds_impl.h"

#include "server/http/admin.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
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

  envoy::api::v2::route::VirtualHost buildVirtualHost(const std::string& name,
                                                      const std::string& domain) {
    return TestUtility::parseYaml<envoy::api::v2::route::VirtualHost>(fmt::format(R"EOF(
      name: {}
      domains: [{}]
      routes:
      - match: {{ prefix: "/" }}
        route: {{ cluster: "my_service" }}
    )EOF",
                                                                                  name, domain));
  }

  Protobuf::RepeatedPtrField<envoy::api::v2::Resource>
  buildAddedResources(const std::vector<envoy::api::v2::route::VirtualHost>& added_or_updated) {
    Protobuf::RepeatedPtrField<envoy::api::v2::Resource> to_ret;

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
  RouteConfigUpdatePtr makeRouteConfigUpdate(const envoy::api::v2::RouteConfiguration& rc) {
    RouteConfigUpdatePtr config_update_info = std::make_unique<RouteConfigUpdateReceiverImpl>(
        factory_context_.timeSource(), factory_context_.messageValidationVisitor());
    config_update_info->onRdsUpdate(rc, "1");
    return config_update_info;
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  Init::ExpectableWatcherImpl init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  const std::string context_ = "vhds_test";
  std::unordered_set<Envoy::Router::RouteConfigProvider*> providers_;
  Protobuf::util::MessageDifferencer messageDifferencer_;
  std::string default_vhds_config_;
  NiceMock<Envoy::Config::MockSubscriptionFactory> subscription_factory_;
};

// verify that api_type: DELTA_GRPC passes validation
TEST_F(VhdsTest, VhdsInstantiationShouldSucceedWithDELTA_GRPC) {
  const auto route_config =
      TestUtility::parseYaml<envoy::api::v2::RouteConfiguration>(default_vhds_config_);
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  EXPECT_NO_THROW(VhdsSubscription(config_update_info, factory_context_, context_, providers_));
}

// verify that api_type: GRPC fails validation
TEST_F(VhdsTest, VhdsInstantiationShouldFailWithoutDELTA_GRPC) {
  const auto route_config = TestUtility::parseYaml<envoy::api::v2::RouteConfiguration>(R"EOF(
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

  EXPECT_THROW(VhdsSubscription(config_update_info, factory_context_, context_, providers_),
               EnvoyException);
}

// verify addition/updating of virtual hosts
TEST_F(VhdsTest, VhdsAddsVirtualHosts) {
  const auto route_config =
      TestUtility::parseYaml<envoy::api::v2::RouteConfiguration>(default_vhds_config_);
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  VhdsSubscription subscription(config_update_info, factory_context_, context_, providers_);
  EXPECT_EQ(0UL, config_update_info->routeConfiguration().virtual_hosts_size());

  auto vhost = buildVirtualHost("vhost1", "vhost.first");
  const auto& added_resources = buildAddedResources({vhost});
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      added_resources, removed_resources, "1");

  EXPECT_EQ(1UL, config_update_info->routeConfiguration().virtual_hosts_size());
  EXPECT_TRUE(
      messageDifferencer_.Equals(vhost, config_update_info->routeConfiguration().virtual_hosts(0)));
}

// verify addition/updating of virtual hosts to already existing ones
TEST_F(VhdsTest, VhdsAddsToExistingVirtualHosts) {
  const auto route_config = TestUtility::parseYaml<envoy::api::v2::RouteConfiguration>(R"EOF(
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
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  VhdsSubscription subscription(config_update_info, factory_context_, context_, providers_);
  EXPECT_EQ(1UL, config_update_info->routeConfiguration().virtual_hosts_size());
  EXPECT_EQ("vhost_rds1", config_update_info->routeConfiguration().virtual_hosts(0).name());

  auto vhost = buildVirtualHost("vhost1", "vhost.first");
  const auto& added_resources = buildAddedResources({vhost});
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      added_resources, removed_resources, "1");

  EXPECT_EQ(2UL, config_update_info->routeConfiguration().virtual_hosts_size());
  auto actual_vhost_0 = config_update_info->routeConfiguration().virtual_hosts(0);
  auto actual_vhost_1 = config_update_info->routeConfiguration().virtual_hosts(1);
  EXPECT_TRUE("vhost_rds1" == actual_vhost_0.name() || "vhost_rds1" == actual_vhost_1.name());
  EXPECT_TRUE(messageDifferencer_.Equals(vhost, actual_vhost_0) ||
              messageDifferencer_.Equals(vhost, actual_vhost_1));
}

// verify removal of virtual hosts
TEST_F(VhdsTest, VhdsRemovesAnExistingVirtualHost) {
  const auto route_config = TestUtility::parseYaml<envoy::api::v2::RouteConfiguration>(R"EOF(
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
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);
  VhdsSubscription subscription(config_update_info, factory_context_, context_, providers_);
  EXPECT_EQ(1UL, config_update_info->routeConfiguration().virtual_hosts_size());
  EXPECT_EQ("vhost_rds1", config_update_info->routeConfiguration().virtual_hosts(0).name());

  const Protobuf::RepeatedPtrField<envoy::api::v2::Resource> added_resources;
  const auto removed_resources = buildRemovedResources({"vhost_rds1"});
  factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      added_resources, removed_resources, "1");

  EXPECT_EQ(0UL, config_update_info->routeConfiguration().virtual_hosts_size());
}

// verify vhds overwrites existing virtual hosts
TEST_F(VhdsTest, VhdsOverwritesAnExistingVirtualHost) {
  const auto route_config = TestUtility::parseYaml<envoy::api::v2::RouteConfiguration>(R"EOF(
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
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  VhdsSubscription subscription(config_update_info, factory_context_, context_, providers_);
  EXPECT_EQ(1UL, config_update_info->routeConfiguration().virtual_hosts_size());
  EXPECT_EQ("vhost_rds1", config_update_info->routeConfiguration().virtual_hosts(0).name());

  auto vhost = buildVirtualHost("vhost_rds1", "vhost.rds.first.mk2");
  const auto& added_resources = buildAddedResources({vhost});
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      added_resources, removed_resources, "1");

  EXPECT_EQ(1UL, config_update_info->routeConfiguration().virtual_hosts_size());
  EXPECT_TRUE(
      messageDifferencer_.Equals(vhost, config_update_info->routeConfiguration().virtual_hosts(0)));
}

// verify vhds validates VirtualHosts in added_resources
TEST_F(VhdsTest, VhdsValidatesAddedVirtualHosts) {
  const auto route_config =
      TestUtility::parseYaml<envoy::api::v2::RouteConfiguration>(default_vhds_config_);
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  VhdsSubscription subscription(config_update_info, factory_context_, context_, providers_);

  auto vhost = TestUtility::parseYaml<envoy::api::v2::route::VirtualHost>(R"EOF(
        name: invalid_vhost
        domains: []
        routes:
        - match: { prefix: "/" }
          route: { cluster: "my_service" }
)EOF");

  const auto& added_resources = buildAddedResources({vhost});
  const Protobuf::RepeatedPtrField<std::string> removed_resources;

  EXPECT_THROW(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
                   added_resources, removed_resources, "1"),
               ProtoValidationException);
}

} // namespace
} // namespace Router
} // namespace Envoy
