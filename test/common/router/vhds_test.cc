#include <chrono>
#include <memory>
#include <string>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/admin/v2alpha/config_dump.pb.validate.h"
#include "envoy/stats/scope.h"

#include "common/config/filter_json.h"
#include "common/config/utility.h"
#include "common/http/message_impl.h"
#include "common/json/json_loader.h"
#include "common/protobuf/protobuf.h"
#include "common/router/rds_impl.h"

#include "server/http/admin.h"

#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Router {
namespace {

class VhdsTest : public testing::Test {
public:
  void SetUp() override {
    factory_function_ = {[](const envoy::api::v2::core::ConfigSource&, const LocalInfo::LocalInfo&,
                            Event::Dispatcher&, Upstream::ClusterManager&,
                            Envoy::Runtime::RandomGenerator&, Stats::Scope&, const std::string&,
                            const std::string&, absl::string_view,
                            Api::Api&) -> std::unique_ptr<Envoy::Config::Subscription> {
      return std::unique_ptr<Envoy::Config::MockSubscription>();
    }};
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

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  Init::ExpectableWatcherImpl init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  Envoy::Router::SubscriptionFactoryFunction factory_function_;
  const std::string context_ = "vhds_test";
  std::unordered_set<Envoy::Router::RouteConfigProvider*> providers_;
  Protobuf::util::MessageDifferencer messageDifferencer_;
};

// verify that api_type: DELTA_GRPC passes validation
TEST_F(VhdsTest, VhdsInstantiationShouldSucceedWithDELTA_GRPC) {
  const auto route_config = TestUtility::parseYaml<envoy::api::v2::RouteConfiguration>(R"EOF(
name: my_route
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
  )EOF");

  EXPECT_NO_THROW(
      VhdsSubscription(route_config, factory_context_, context_, providers_, factory_function_));
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

  EXPECT_THROW(
      VhdsSubscription(route_config, factory_context_, context_, providers_, factory_function_),
      EnvoyException);
}

// verify addition/updating of virtual hosts
TEST_F(VhdsTest, VhdsAddsVirtualHosts) {
  const auto route_config = TestUtility::parseYaml<envoy::api::v2::RouteConfiguration>(R"EOF(
name: my_route
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
  )EOF");

  VhdsSubscription subscription(route_config, factory_context_, context_, providers_,
                                factory_function_);
  EXPECT_EQ(0UL, subscription.routeConfiguration().virtual_hosts_size());

  auto vhost = buildVirtualHost("vhost1", "vhost.first");
  const auto& added_resources = buildAddedResources({vhost});
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  subscription.onConfigUpdate(added_resources, removed_resources, "1");

  EXPECT_EQ(1UL, subscription.routeConfiguration().virtual_hosts_size());
  EXPECT_TRUE(
      messageDifferencer_.Equals(vhost, subscription.routeConfiguration().virtual_hosts(0)));
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

  VhdsSubscription subscription(route_config, factory_context_, context_, providers_,
                                factory_function_);
  EXPECT_EQ(1UL, subscription.routeConfiguration().virtual_hosts_size());
  EXPECT_EQ("vhost_rds1", subscription.routeConfiguration().virtual_hosts(0).name());

  auto vhost = buildVirtualHost("vhost1", "vhost.first");
  const auto& added_resources = buildAddedResources({vhost});
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  subscription.onConfigUpdate(added_resources, removed_resources, "1");

  EXPECT_EQ(2UL, subscription.routeConfiguration().virtual_hosts_size());
  auto actual_vhost_0 = subscription.routeConfiguration().virtual_hosts(0);
  auto actual_vhost_1 = subscription.routeConfiguration().virtual_hosts(1);
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

  VhdsSubscription subscription(route_config, factory_context_, context_, providers_,
                                factory_function_);
  EXPECT_EQ(1UL, subscription.routeConfiguration().virtual_hosts_size());
  EXPECT_EQ("vhost_rds1", subscription.routeConfiguration().virtual_hosts(0).name());

  const Protobuf::RepeatedPtrField<envoy::api::v2::Resource> added_resources;
  const auto removed_resources = buildRemovedResources({"vhost_rds1"});
  subscription.onConfigUpdate(added_resources, removed_resources, "1");

  EXPECT_EQ(0UL, subscription.routeConfiguration().virtual_hosts_size());
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

  VhdsSubscription subscription(route_config, factory_context_, context_, providers_,
                                factory_function_);
  EXPECT_EQ(1UL, subscription.routeConfiguration().virtual_hosts_size());
  EXPECT_EQ("vhost_rds1", subscription.routeConfiguration().virtual_hosts(0).name());

  auto vhost = buildVirtualHost("vhost_rds1", "vhost.rds.first.mk2");
  const auto& added_resources = buildAddedResources({vhost});
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  subscription.onConfigUpdate(added_resources, removed_resources, "1");

  EXPECT_EQ(1UL, subscription.routeConfiguration().virtual_hosts_size());
  EXPECT_TRUE(
      messageDifferencer_.Equals(vhost, subscription.routeConfiguration().virtual_hosts(0)));
}

} // namespace
} // namespace Router
} // namespace Envoy
