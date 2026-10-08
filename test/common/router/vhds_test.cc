#include <chrono>
#include <memory>
#include <string>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/config/utility.h"
#include "source/common/init/manager_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/router/rds_impl.h"
#include "source/common/router/route_config_update_receiver_impl.h"
#include "source/common/router/route_provider_manager.h"

#ifdef ENVOY_ADMIN_FUNCTIONALITY
#include "source/server/admin/admin.h"
#endif
#include "test/mocks/config/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/printers.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

using ::Envoy::StatusHelpers::HasStatusMessage;
using ::Envoy::StatusHelpers::IsOk;
using ::testing::Not;

class MockRouteConfigUpdateObserver : public Rds::RouteConfigUpdateObserver {
public:
  MOCK_METHOD(void, onConfigWarmed, ());
};

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
      std::ignore = resource->mutable_resource()->PackFrom(vhost);
    }

    return to_ret;
  }

  Protobuf::RepeatedPtrField<std::string>
  buildRemovedResources(const std::vector<std::string>& removed) {
    return Protobuf::RepeatedPtrField<std::string>{removed.begin(), removed.end()};
  }
  RouteConfigUpdatePtr makeReceiver() {
    return std::make_unique<RouteConfigUpdateReceiverImpl>(proto_traits_, factory_context_,
                                                           context_, /*from_rds=*/false);
  }

  // Applies an RDS update, which is what creates the VHDS subscription of the route configuration.
  RouteConfigUpdatePtr
  makeRouteConfigUpdate(const envoy::config::route::v3::RouteConfiguration& rc) {
    RouteConfigUpdatePtr config_update_info = makeReceiver();
    EXPECT_OK(config_update_info->onRdsUpdate(rc, "1"));
    return config_update_info;
  }

  ProtoTraitsImpl proto_traits_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  Init::ManagerImpl init_manager_{"test route config"};
  Init::ExpectableWatcherImpl init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  const std::string context_ = "vhds_test";
  Protobuf::util::MessageDifferencer messageDifferencer_;
  std::string default_vhds_config_;
  NiceMock<Envoy::Config::MockSubscriptionFactory> subscription_factory_;
};

// verify that api_type: DELTA_GRPC passes validation
TEST_F(VhdsTest, VhdsInstantiationShouldSucceedWithDELTA_GRPC) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
  // Creating the receiver's VHDS subscription is part of applying the RDS update.
  makeRouteConfigUpdate(route_config);
}

// A receiver that is destroyed while an update is still waiting for its initial VHDS fetch must not
// notify its observer. The observer owns the receiver, so it is itself being destroyed by then, and
// dropping the VHDS subscription signals the init target that the update warms up with.
TEST_F(VhdsTest, DestroyingTheReceiverWhileWarmingDoesNotNotifyTheObserver) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);

  NiceMock<MockRouteConfigUpdateObserver> observer;
  RouteConfigUpdatePtr config_update_info = makeReceiver();
  config_update_info->setObserver(observer);

  // The route configuration configures VHDS, so it isn't published until the initial VHDS fetch has
  // landed, i.e. it is still warming up here.
  EXPECT_OK(config_update_info->onRdsUpdate(route_config, "1"));
  EXPECT_TRUE(config_update_info->configWarming());

  // Ensure that when the receiver and VHDS subscription are destroyed, the observer is not
  // notified.
  EXPECT_CALL(observer, onConfigWarmed()).Times(0);
  config_update_info.reset();
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
  RouteConfigUpdatePtr config_update_info = makeReceiver();

  EXPECT_THAT(config_update_info->onRdsUpdate(route_config, "1"), Not(IsOk()));
}

// Verify that VHDS over GRPC fails when ADS is using DELTA_GRPC.
TEST_F(VhdsTest, VhdsInstantiationShouldFailWithGrpcAndAdsDeltaGrpc) {
  factory_context_.bootstrap().mutable_dynamic_resources()->mutable_ads_config()->set_api_type(
      envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);
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
  RouteConfigUpdatePtr config_update_info = makeReceiver();

  EXPECT_THAT(config_update_info->onRdsUpdate(route_config, "1"), Not(IsOk()));
}

// verify that ADS with DELTA_GRPC in bootstrap passes validation
TEST_F(VhdsTest, VhdsInstantiationShouldSucceedWithAdsAndDeltaGrpc) {
  // Configure bootstrap with ADS using DELTA_GRPC
  auto& bootstrap = factory_context_.bootstrap();
  auto* dynamic_resources = bootstrap.mutable_dynamic_resources();
  auto* ads_config = dynamic_resources->mutable_ads_config();
  ads_config->set_api_type(envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);

  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
vhds:
  config_source:
    ads: {}
  )EOF");
  // Creating the receiver's VHDS subscription is part of applying the RDS update.
  makeRouteConfigUpdate(route_config);
}

// verify that ADS without ADS configured in bootstrap fails validation
TEST_F(VhdsTest, VhdsInstantiationShouldFailWithAdsButNoBootstrapConfig) {
  // Don't configure ADS in bootstrap (it's empty by default)

  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
vhds:
  config_source:
    ads: {}
  )EOF");
  RouteConfigUpdatePtr config_update_info = makeReceiver();

  auto result = config_update_info->onRdsUpdate(route_config, "1");
  EXPECT_THAT(result, HasStatusMessage(
                          "vhds: ADS config source specified but no ADS configured in bootstrap."));
}

// verify that ADS without DELTA_GRPC api_type in bootstrap fails validation
TEST_F(VhdsTest, VhdsInstantiationShouldFailWithAdsButWrongApiType) {
  // Configure bootstrap with ADS using GRPC (not DELTA_GRPC)
  auto& bootstrap = factory_context_.bootstrap();
  auto* dynamic_resources = bootstrap.mutable_dynamic_resources();
  auto* ads_config = dynamic_resources->mutable_ads_config();
  ads_config->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);

  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
vhds:
  config_source:
    ads: {}
  )EOF");
  RouteConfigUpdatePtr config_update_info = makeReceiver();

  auto result = config_update_info->onRdsUpdate(route_config, "1");
  EXPECT_THAT(
      result,
      HasStatusMessage("vhds: ADS must use DELTA_GRPC api_type when used as VHDS config source."));
}

// verify addition/updating of virtual hosts
TEST_F(VhdsTest, VhdsAddsVirtualHosts) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  EXPECT_EQ(0UL, config_update_info->protobufConfigurationCast().virtual_hosts_size());

  auto vhost = buildVirtualHost("vhost1", "vhost.first");
  const auto& added_resources = buildAddedResources({vhost});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, removed_resources, "1"));

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

  // The route configuration configures VHDS, so it isn't published until the initial VHDS fetch
  // has landed.
  EXPECT_EQ(0UL, config_update_info->protobufConfigurationCast().virtual_hosts_size());

  auto vhost = buildVirtualHost("vhost_vhds1", "vhost.first");
  const auto& added_resources = buildAddedResources({vhost});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, removed_resources, "1"));
  EXPECT_EQ(2UL, config_update_info->protobufConfigurationCast().virtual_hosts_size());

  EXPECT_OK(config_update_info->onRdsUpdate(updated_route_config, "2"));

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

// verify that a VHDS update that neither adds nor removes a virtual host leaves the currently
// published route configuration in place instead of rebuilding it
TEST_F(VhdsTest, VhdsUpdateWithoutChangesKeepsTheRouteConfig) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  // Land the initial VHDS fetch, which is what publishes the route configuration.
  const auto first_added_resources =
      buildAddedResources({buildVirtualHost("vhost1", "vhost1.com")});
  const auto first_decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(first_added_resources);
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      first_decoded_resources.refvec_, {}, "2"));
  const auto config_before_update = config_update_info->parsedConfiguration();
  ASSERT_NE(nullptr, config_before_update);

  const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources;
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, buildRemovedResources({"never_added_vhost"}), "2"));

  // No new route configuration was built.
  EXPECT_EQ(config_before_update, config_update_info->parsedConfiguration());
}

// verify that a VHDS update that neither adds nor removes a virtual host records that it carried
// no resource ids, so that the ids of the previous update aren't resolved a second time
TEST_F(VhdsTest, VhdsUpdateWithoutChangesClearsTheResourceIdsOfTheLastUpdate) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  // An update that actually adds a virtual host records its resource id.
  const auto added_resources = buildAddedResources({buildVirtualHost("vhost1", "vhost1.com")});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, {}, "2"));
  EXPECT_THAT(config_update_info->resourceIdsInLastVhdsUpdate(),
              ::testing::UnorderedElementsAre("vhost1"));

  // A following no-op update carried no resource ids, so none are left over from the one above.
  const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> no_added_resources;
  const auto no_decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(no_added_resources);
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      no_decoded_resources.refvec_, buildRemovedResources({"never_added_vhost"}), "3"));
  EXPECT_TRUE(config_update_info->resourceIdsInLastVhdsUpdate().empty());
}

// verify that the resource ids of published VHDS updates accumulate, so that a repeated on-demand
// request for an already answered alias can be answered locally
TEST_F(VhdsTest, VhdsAnsweredResourceIdsAccumulateAcrossUpdates) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);
  EXPECT_FALSE(config_update_info->vhdsResourceIdAnswered("vhost1"));

  // The first update answers a requested id with a virtual host delivered under its name.
  config_update_info->updateOnDemand("vhost1");
  const auto added_resources = buildAddedResources({buildVirtualHost("vhost1", "vhost1.com")});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, {}, "2"));
  EXPECT_TRUE(config_update_info->vhdsResourceIdAnswered("vhost1"));
  EXPECT_FALSE(config_update_info->vhdsResourceIdAnswered("my_route/unknown.com"));

  // The second update is an empty resource, the way the server answers for an alias it couldn't
  // resolve. Its id accumulates next to the one of the first update instead of replacing it.
  config_update_info->updateOnDemand("my_route/unknown.com");
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> empty_resource;
  empty_resource.Add()->set_name("my_route/unknown.com");
  const auto decoded_empty_resource =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(empty_resource);
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_empty_resource.refvec_, {}, "3"));
  EXPECT_TRUE(config_update_info->vhdsResourceIdAnswered("my_route/unknown.com"));
  EXPECT_TRUE(config_update_info->vhdsResourceIdAnswered("vhost1"));

  // Removing the virtual host doesn't withdraw the answer: the published configuration now
  // answers that the virtual host doesn't exist, and the subscription to the id stays, so the
  // server pushes an update on its own if the virtual host comes back.
  const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> nothing_added;
  const auto decoded_nothing_added =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(nothing_added);
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_nothing_added.refvec_, buildRemovedResources({"vhost1"}), "4"));
  EXPECT_TRUE(config_update_info->vhdsResourceIdAnswered("vhost1"));
}

// verify that accumulated answers don't survive the VHDS subscription they came from: a changed
// VHDS configuration creates a new subscription which isn't subscribed to the previously answered
// aliases, so nothing guarantees pushes for them any more
TEST_F(VhdsTest, VhdsAnsweredResourceIdsAreDroppedWithTheSubscription) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  config_update_info->updateOnDemand("vhost1");
  const auto added_resources = buildAddedResources({buildVirtualHost("vhost1", "vhost1.com")});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, {}, "2"));
  EXPECT_TRUE(config_update_info->vhdsResourceIdAnswered("vhost1"));

  // An RDS update with a different VHDS config source replaces the subscription.
  const auto updated_route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: another_xds_cluster
  )EOF");
  EXPECT_OK(config_update_info->onRdsUpdate(updated_route_config, "2"));
  EXPECT_FALSE(config_update_info->vhdsResourceIdAnswered("vhost1"));

  // The request made on the old subscription is forgotten too: the same update pushed again on
  // the new subscription doesn't mark the id answered, because the new subscription holds no
  // interest in it until it is requested again.
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, {}, "3"));
  EXPECT_FALSE(config_update_info->vhdsResourceIdAnswered("vhost1"));
}

// verify that names and aliases the server volunteered without a request never enter the
// answered cache: Envoy holds no subscription interest in them, so nothing guarantees pushes
// for them after a stream reconnect, and answering them locally would serve stale data forever
TEST_F(VhdsTest, VhdsUnrequestedResourceIdsAreNotAnswered) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  // The server pushes a virtual host unsolicited, with an alias attached on its own.
  auto added_resources = buildAddedResources({buildVirtualHost("vhost1", "vhost1.com")});
  added_resources.Mutable(0)->add_aliases("my_route/vhost1.com");
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, {}, "2"));
  EXPECT_FALSE(config_update_info->vhdsResourceIdAnswered("vhost1"));
  EXPECT_FALSE(config_update_info->vhdsResourceIdAnswered("my_route/vhost1.com"));

  // Once the alias is requested and the server answers it, it is cached. The name the server
  // chose on its own still isn't.
  config_update_info->updateOnDemand("my_route/vhost1.com");
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, {}, "3"));
  EXPECT_TRUE(config_update_info->vhdsResourceIdAnswered("my_route/vhost1.com"));
  EXPECT_FALSE(config_update_info->vhdsResourceIdAnswered("vhost1"));
}

// verify that accumulated answers survive an RDS update that changes only the virtual hosts: the
// VHDS configuration is unchanged, so the subscription that holds the interest in the answered
// aliases is kept, and the published configuration keeps reflecting the server's answers
TEST_F(VhdsTest, VhdsAnsweredResourceIdsSurviveRdsUpdateThatKeepsTheSubscription) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  config_update_info->updateOnDemand("vhost1");
  const auto added_resources = buildAddedResources({buildVirtualHost("vhost1", "vhost1.com")});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, {}, "2"));
  EXPECT_TRUE(config_update_info->vhdsResourceIdAnswered("vhost1"));

  // An RDS update with the same VHDS config source but different virtual hosts keeps the
  // subscription.
  auto updated_route_config = route_config;
  auto* rds_vhost = updated_route_config.add_virtual_hosts();
  rds_vhost->set_name("vhost_rds1");
  rds_vhost->add_domains("vhost.rds.first");
  EXPECT_OK(config_update_info->onRdsUpdate(updated_route_config, "2"));
  EXPECT_TRUE(config_update_info->vhdsResourceIdAnswered("vhost1"));

  // The published configuration still carries the answered virtual host, merged with the new RDS
  // virtual host, so the locally served answer stays correct.
  std::vector<std::string> vhost_names;
  for (const auto& vhost : config_update_info->protobufConfigurationCast().virtual_hosts()) {
    vhost_names.push_back(vhost.name());
  }
  EXPECT_THAT(vhost_names, ::testing::UnorderedElementsAre("vhost_rds1", "vhost1"));
}

} // namespace
} // namespace Router
} // namespace Envoy
