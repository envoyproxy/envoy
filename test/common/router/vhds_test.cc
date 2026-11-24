#include <chrono>
#include <memory>
#include <string>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/server/filter_config.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/config/utility.h"
#include "source/common/init/manager_impl.h"
#include "source/common/init/target_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/router/rds_impl.h"
#include "source/common/router/route_config_update_receiver_impl.h"
#include "source/common/router/route_provider_manager.h"
#include "source/common/router/vhds.h"

#ifdef ENVOY_ADMIN_FUNCTIONALITY
#include "source/server/admin/admin.h"
#endif
#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/printers.h"
#include "test/test_common/registry.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
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

  struct SkipInitialWarmingReceiver {
    envoy::config::route::v3::RouteConfiguration route_config;
    std::unique_ptr<NiceMock<MockRouteConfigUpdateObserver>> observer;
    RouteConfigUpdatePtr receiver;
    Envoy::Config::MockSubscription* subscription{nullptr};
    Envoy::Config::SubscriptionCallbacks* callbacks{nullptr};
  };

  SkipInitialWarmingReceiver makeSkipInitialWarmingReceiver() {
    SkipInitialWarmingReceiver setup;
    setup.route_config =
        TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
    setup.route_config.mutable_vhds()->mutable_skip_initial_warming();
    setup.observer = std::make_unique<NiceMock<MockRouteConfigUpdateObserver>>();
    setup.receiver = makeReceiver();
    setup.receiver->setObserver(*setup.observer);
    ON_CALL(*setup.observer, onConfigWarmed()).WillByDefault([receiver = setup.receiver.get()]() {
      receiver->onVhdsConfigPublished();
    });

    EXPECT_OK(setup.receiver->onRdsUpdate(setup.route_config, "1"));
    EXPECT_FALSE(setup.receiver->configWarming());
    setup.subscription = factory_context_.cluster_manager_.subscription_factory_.subscription_;
    setup.callbacks = factory_context_.cluster_manager_.subscription_factory_.callbacks_;
    EXPECT_NE(nullptr, setup.subscription);
    EXPECT_NE(nullptr, setup.callbacks);
    return setup;
  }

  // Applies an RDS update, which is what creates the VHDS subscription of the route configuration.
  RouteConfigUpdatePtr
  makeRouteConfigUpdate(const envoy::config::route::v3::RouteConfiguration& rc) {
    RouteConfigUpdatePtr config_update_info = makeReceiver();
    EXPECT_OK(config_update_info->onRdsUpdate(rc, "1"));
    return config_update_info;
  }

  envoy::config::route::v3::RouteConfiguration
  routeConfiguration(const RouteConfigUpdateReceiver& receiver) {
    envoy::config::route::v3::RouteConfiguration route_config;
    if (receiver.configInfo().has_value()) {
      EXPECT_OK(MessageUtil::unpackTo(receiver.configInfo().value().config_, route_config));
    }
    return route_config;
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

  EXPECT_EQ(0UL, routeConfiguration(*config_update_info).virtual_hosts_size());

  auto vhost = buildVirtualHost("vhost1", "vhost.first");
  const auto& added_resources = buildAddedResources({vhost});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, removed_resources, "1"));

  EXPECT_EQ(1UL, routeConfiguration(*config_update_info).virtual_hosts_size());
  EXPECT_TRUE(
      messageDifferencer_.Equals(vhost, routeConfiguration(*config_update_info).virtual_hosts(0)));
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
  EXPECT_EQ(0UL, routeConfiguration(*config_update_info).virtual_hosts_size());

  auto vhost = buildVirtualHost("vhost_vhds1", "vhost.first");
  const auto& added_resources = buildAddedResources({vhost});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, removed_resources, "1"));
  EXPECT_EQ(2UL, routeConfiguration(*config_update_info).virtual_hosts_size());

  EXPECT_OK(config_update_info->onRdsUpdate(updated_route_config, "2"));

  EXPECT_EQ(3UL, routeConfiguration(*config_update_info).virtual_hosts_size());
  auto actual_vhost_0 = routeConfiguration(*config_update_info).virtual_hosts(0);
  auto actual_vhost_1 = routeConfiguration(*config_update_info).virtual_hosts(1);
  auto actual_vhost_2 = routeConfiguration(*config_update_info).virtual_hosts(2);
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
  const auto current_config = routeConfiguration(*config_update_info);
  for (const auto& vhost : current_config.virtual_hosts()) {
    vhost_names.push_back(vhost.name());
  }
  EXPECT_THAT(vhost_names, ::testing::UnorderedElementsAre("vhost_rds1", "vhost1"));
}

TEST_F(VhdsTest, AliasToDomainName) {
  EXPECT_EQ("my_route/example.com", VhdsSubscription::domainNameToAlias("my_route", "example.com"));
  EXPECT_EQ("example.com", VhdsSubscription::aliasToDomainName("my_route/example.com"));
  EXPECT_EQ("example.com", VhdsSubscription::aliasToDomainName("prefix/my_route/example.com"));
  EXPECT_EQ("example.com", VhdsSubscription::aliasToDomainName("example.com"));
  EXPECT_EQ("", VhdsSubscription::aliasToDomainName("my_route/"));
  EXPECT_EQ("", VhdsSubscription::aliasToDomainName(""));
}

// verify that when RDS and VHDS define virtual hosts with the same name, VHDS takes precedence
TEST_F(VhdsTest, VhdsOverridesRdsVirtualHostWithSameName) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
virtual_hosts:
- name: overlapping_vhost
  domains: ["vhost.rds.domain"]
  routes:
  - match: { prefix: "/rds" }
    route: { cluster: rds_service }
- name: vhost_rds_only
  domains: ["vhost.rds.only"]
  routes:
  - match: { prefix: "/rdsonly" }
    route: { cluster: rds_only_service }
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
  EXPECT_EQ(0UL, routeConfiguration(*config_update_info).virtual_hosts_size());

  // Add a VHDS virtual host with the same name as one of the RDS virtual hosts
  auto vhost = buildVirtualHost("overlapping_vhost", "vhost.vhds.domain");
  const auto& added_resources = buildAddedResources({vhost});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, removed_resources, "1"));

  // Should have 2 virtual hosts: vhost_rds_only from RDS + overlapping_vhost from VHDS
  const auto current_config = routeConfiguration(*config_update_info);
  EXPECT_EQ(2UL, current_config.virtual_hosts_size());

  // Verify the VHDS version of the overlapping vhost is used (with vhds domain, not rds domain)
  bool found_vhds_version = false;
  bool found_rds_only = false;
  for (const auto& vh : current_config.virtual_hosts()) {
    if (vh.name() == "overlapping_vhost") {
      // The VHDS version should have "vhost.vhds.domain", not "vhost.rds.domain"
      EXPECT_EQ("vhost.vhds.domain", vh.domains(0));
      found_vhds_version = true;
    } else if (vh.name() == "vhost_rds_only") {
      found_rds_only = true;
    }
  }
  EXPECT_TRUE(found_vhds_version) << "VHDS version of overlapping_vhost not found";
  EXPECT_TRUE(found_rds_only) << "vhost_rds_only not found";
}

// verify VHDS precedence is maintained after an RDS update
TEST_F(VhdsTest, VhdsOverridesRdsVirtualHostAfterRdsUpdate) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
virtual_hosts:
- name: overlapping_vhost
  domains: ["vhost.rds.domain"]
  routes:
  - match: { prefix: "/rds" }
    route: { cluster: rds_service }
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
  )EOF");
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  // Add a VHDS virtual host with the same name
  auto vhost = buildVirtualHost("overlapping_vhost", "vhost.vhds.domain");
  const auto& added_resources = buildAddedResources({vhost});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, removed_resources, "1"));

  // Now trigger an RDS update that still has the same-named vhost
  const auto updated_route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
virtual_hosts:
- name: overlapping_vhost
  domains: ["vhost.rds.updated"]
  routes:
  - match: { prefix: "/rds-updated" }
    route: { cluster: rds_updated_service }
- name: vhost_rds_new
  domains: ["vhost.rds.new"]
  routes:
  - match: { prefix: "/rdsnew" }
    route: { cluster: rds_new_service }
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
  )EOF");
  EXPECT_OK(config_update_info->onRdsUpdate(updated_route_config, "2"));

  // Should have 2: vhost_rds_new from RDS + overlapping_vhost from VHDS
  const auto current_config = routeConfiguration(*config_update_info);
  EXPECT_EQ(2UL, current_config.virtual_hosts_size());

  // Verify VHDS version still takes precedence after RDS update
  bool found_vhds_version = false;
  for (const auto& vh : current_config.virtual_hosts()) {
    if (vh.name() == "overlapping_vhost") {
      EXPECT_EQ("vhost.vhds.domain", vh.domains(0));
      found_vhds_version = true;
    }
  }
  EXPECT_TRUE(found_vhds_version) << "VHDS version of overlapping_vhost not found after RDS update";
}

// verify that removing a VHDS virtual host that was overriding an RDS one causes the RDS vhost to
// reappear
TEST_F(VhdsTest, RemovingVhdsVirtualHostRestoresRdsVirtualHost) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
virtual_hosts:
- name: overlapping_vhost
  domains: ["vhost.rds.domain"]
  routes:
  - match: { prefix: "/rds" }
    route: { cluster: rds_service }
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
  EXPECT_EQ(0UL, routeConfiguration(*config_update_info).virtual_hosts_size());

  // Add a VHDS virtual host with the same name as the RDS one
  auto vhost = buildVirtualHost("overlapping_vhost", "vhost.vhds.domain");
  const auto& added_resources = buildAddedResources({vhost});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, removed_resources, "1"));

  // VHDS version should override
  EXPECT_EQ(1UL, routeConfiguration(*config_update_info).virtual_hosts_size());
  EXPECT_EQ("vhost.vhds.domain",
            routeConfiguration(*config_update_info).virtual_hosts(0).domains(0));

  // Now remove the VHDS virtual host
  const auto no_added_resources = buildAddedResources({});
  const auto no_decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(no_added_resources);
  Protobuf::RepeatedPtrField<std::string> resources_to_remove;
  *resources_to_remove.Add() = "overlapping_vhost";
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      no_decoded_resources.refvec_, resources_to_remove, "2"));

  // The RDS virtual host should reappear
  EXPECT_EQ(1UL, routeConfiguration(*config_update_info).virtual_hosts_size());
  EXPECT_EQ("overlapping_vhost", routeConfiguration(*config_update_info).virtual_hosts(0).name());
  EXPECT_EQ("vhost.rds.domain",
            routeConfiguration(*config_update_info).virtual_hosts(0).domains(0));
}

// verify that with the runtime guard disabled, RDS and VHDS virtual hosts with the same name are
// both kept
TEST_F(VhdsTest, VhdsDoesNotOverrideRdsVirtualHostWithRuntimeGuardDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.vhds_overrides_rds_virtual_hosts_with_same_name", "false"}});
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
virtual_hosts:
- name: overlapping_vhost
  domains: ["vhost.rds.domain"]
  routes:
  - match: { prefix: "/rds" }
    route: { cluster: rds_service }
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
  )EOF");
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  auto vhost = buildVirtualHost("overlapping_vhost", "vhost.vhds.domain");
  const auto& added_resources = buildAddedResources({vhost});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, removed_resources, "1"));

  // Both the RDS and the VHDS versions of overlapping_vhost are kept, RDS first.
  const auto current_config = routeConfiguration(*config_update_info);
  const auto& virtual_hosts = current_config.virtual_hosts();
  ASSERT_EQ(2, virtual_hosts.size());
  EXPECT_EQ("overlapping_vhost", virtual_hosts[0].name());
  EXPECT_EQ("vhost.rds.domain", virtual_hosts[0].domains(0));
  EXPECT_EQ("overlapping_vhost", virtual_hosts[1].name());
  EXPECT_EQ("vhost.vhds.domain", virtual_hosts[1].domains(0));
}

TEST_F(VhdsTest, SkipInitialWarmingMarksInitTargetReadyImmediately) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
  skip_initial_warming: {}
)EOF");

  testing::NiceMock<MockVhdsConfigUpdateReceiver> receiver;
  auto subscription_or = VhdsSubscription::createVhdsSubscription(
      route_config, factory_context_, "vhds_test.", /*from_rds=*/false, receiver, init_manager_);
  ASSERT_OK(subscription_or.status());
  VhdsSubscriptionPtr vhds_subscription = std::move(subscription_or.value());

  Envoy::Config::MockSubscription* mock_subscription =
      factory_context_.cluster_manager_.subscription_factory_.subscription_;
  Envoy::Config::SubscriptionCallbacks* vhds_callbacks =
      factory_context_.cluster_manager_.subscription_factory_.callbacks_;
  ASSERT_NE(nullptr, mock_subscription);
  ASSERT_NE(nullptr, vhds_callbacks);

  {
    testing::InSequence s;
    EXPECT_CALL(*mock_subscription, accept(absl::flat_hash_set<std::string>{"my_route/*"}));
    EXPECT_CALL(*mock_subscription, start(absl::flat_hash_set<std::string>{}));
    init_watcher_.expectReady();
  }
  init_manager_.initialize(init_watcher_);
  ::testing::Mock::VerifyAndClearExpectations(&init_watcher_);

  // Later VHDS updates and failures must not re-fire the already-ready init watcher.
  init_watcher_.expectReady().Times(0);

  auto vhost = buildVirtualHost("vhost1", "vhost.first");
  const auto added_resources = buildAddedResources({vhost});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  EXPECT_CALL(receiver, onVhdsUpdate(::testing::SizeIs(1), ::testing::_, ::testing::_, "1"))
      .WillOnce(::testing::Return(true));
  EXPECT_OK(vhds_callbacks->onConfigUpdate(decoded_resources.refvec_, {}, "1"));

  vhds_callbacks->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout,
                                       nullptr);
}

TEST_F(VhdsTest, SkipInitialWarmingPublishesStaticVirtualHostsBeforeVhdsUpdate) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
virtual_hosts:
- name: vhost_static
  domains: ["static.vhost"]
  routes:
  - match: { prefix: "/" }
    route: { cluster: static_service }
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
  skip_initial_warming: {}
)EOF");

  NiceMock<MockRouteConfigUpdateObserver> observer;
  RouteConfigUpdatePtr config_update_info = makeReceiver();
  config_update_info->setObserver(observer);

  auto mock_subscription = std::make_unique<NiceMock<Envoy::Config::MockSubscription>>();
  Envoy::Config::MockSubscription* mock_subscription_ptr = mock_subscription.get();
  Envoy::Config::SubscriptionCallbacks* vhds_callbacks = nullptr;
  EXPECT_CALL(factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _))
      .WillOnce([&mock_subscription,
                 &vhds_callbacks](const envoy::config::core::v3::ConfigSource&, absl::string_view,
                                  Stats::Scope&, Envoy::Config::SubscriptionCallbacks& callbacks,
                                  Envoy::Config::OpaqueResourceDecoderSharedPtr,
                                  const Envoy::Config::SubscriptionOptions&) {
        vhds_callbacks = &callbacks;
        return absl::StatusOr<Envoy::Config::SubscriptionPtr>(std::move(mock_subscription));
      });
  EXPECT_CALL(*mock_subscription_ptr, accept(absl::flat_hash_set<std::string>{"my_route/*"}));
  EXPECT_CALL(*mock_subscription_ptr, start(absl::flat_hash_set<std::string>{}));
  EXPECT_CALL(observer, onConfigWarmed());

  EXPECT_OK(config_update_info->onRdsUpdate(route_config, "1"));
  ::testing::Mock::VerifyAndClearExpectations(&observer);
  ASSERT_NE(nullptr, vhds_callbacks);

  // Static virtual hosts are published and routable immediately while VHDS has not responded yet.
  EXPECT_FALSE(config_update_info->configWarming());
  auto initial_config =
      std::static_pointer_cast<const Config>(config_update_info->parsedConfiguration());
  ASSERT_NE(nullptr, initial_config);
  StreamInfo::StreamInfoImpl stream_info(Http::Protocol::Http11, factory_context_.timeSource(),
                                         nullptr, StreamInfo::FilterState::LifeSpan::FilterChain);
  RouteConstSharedPtr static_route = initial_config->route(
      Http::TestRequestHeaderMapImpl{
          {":authority", "static.vhost"}, {":path", "/"}, {"x-forwarded-proto", "http"}},
      stream_info, 0);
  ASSERT_NE(nullptr, static_route);
  ASSERT_NE(nullptr, static_route->routeEntry());
  EXPECT_EQ("static_service", static_route->routeEntry()->clusterName());
  EXPECT_EQ(nullptr, initial_config
                         ->route(Http::TestRequestHeaderMapImpl{{":authority", "vhost.first"},
                                                                {":path", "/"},
                                                                {"x-forwarded-proto", "http"}},
                                 stream_info, 0)
                         .route);

  // Subsequent VHDS update merges cleanly into the published route configuration.
  auto vhost = buildVirtualHost("vhost1", "vhost.first");
  const auto added_resources = buildAddedResources({vhost});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  EXPECT_CALL(observer, onConfigWarmed());
  EXPECT_OK(vhds_callbacks->onConfigUpdate(decoded_resources.refvec_, {}, "2"));
  ::testing::Mock::VerifyAndClearExpectations(&observer);

  auto merged_config =
      std::static_pointer_cast<const Config>(config_update_info->parsedConfiguration());
  ASSERT_NE(nullptr, merged_config);
  EXPECT_EQ("static_service",
            merged_config
                ->route(Http::TestRequestHeaderMapImpl{{":authority", "static.vhost"},
                                                       {":path", "/"},
                                                       {"x-forwarded-proto", "http"}},
                        stream_info, 0)
                ->routeEntry()
                ->clusterName());
  EXPECT_EQ("my_service", merged_config
                              ->route(Http::TestRequestHeaderMapImpl{{":authority", "vhost.first"},
                                                                     {":path", "/"},
                                                                     {"x-forwarded-proto", "http"}},
                                      stream_info, 0)
                              ->routeEntry()
                              ->clusterName());

  // onConfigUpdateFailed is also a safe no-op after skip_initial_warming already marked ready.
  EXPECT_CALL(observer, onConfigWarmed()).Times(0);
  vhds_callbacks->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout,
                                       nullptr);
}

TEST_F(VhdsTest, VhdsSubscriptionSurvivesRdsUpdateTogglingSkipInitialWarming) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  config_update_info->updateOnDemand("vhost1");
  const auto added_resources = buildAddedResources({buildVirtualHost("vhost1", "vhost1.com")});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, {}, "1"));
  ASSERT_FALSE(config_update_info->configWarming());
  EXPECT_TRUE(config_update_info->vhdsResourceIdAnswered("vhost1"));
  EXPECT_FALSE(routeConfiguration(*config_update_info).vhds().has_skip_initial_warming());

  // Toggling skip_initial_warming on an already-initialized route configuration with unchanged
  // config_source must keep the active VHDS subscription, answered resource IDs, and VHDS vhosts.
  EXPECT_CALL(factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _))
      .Times(0);

  auto route_config_with_skip = route_config;
  route_config_with_skip.mutable_vhds()->mutable_skip_initial_warming();
  EXPECT_OK(config_update_info->onRdsUpdate(route_config_with_skip, "2"));
  EXPECT_TRUE(routeConfiguration(*config_update_info).vhds().has_skip_initial_warming());
  EXPECT_TRUE(config_update_info->vhdsResourceIdAnswered("vhost1"));
  ASSERT_EQ(1, routeConfiguration(*config_update_info).virtual_hosts_size());
  EXPECT_EQ("vhost1", routeConfiguration(*config_update_info).virtual_hosts(0).name());

  // Rolling skip_initial_warming back via another RDS update must also preserve the subscription.
  EXPECT_OK(config_update_info->onRdsUpdate(route_config, "3"));
  EXPECT_FALSE(routeConfiguration(*config_update_info).vhds().has_skip_initial_warming());
  EXPECT_TRUE(config_update_info->vhdsResourceIdAnswered("vhost1"));
  ASSERT_EQ(1, routeConfiguration(*config_update_info).virtual_hosts_size());
  EXPECT_EQ("vhost1", routeConfiguration(*config_update_info).virtual_hosts(0).name());
}

TEST_F(VhdsTest, SecondRdsUpdateWhileWarmingAppliesSkipInitialWarmingImmediately) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
  NiceMock<MockRouteConfigUpdateObserver> observer;
  RouteConfigUpdatePtr config_update_info = makeReceiver();
  config_update_info->setObserver(observer);

  // First RDS update without skip_initial_warming leaves the route configuration warming.
  EXPECT_CALL(observer, onConfigWarmed()).Times(0);
  EXPECT_OK(config_update_info->onRdsUpdate(route_config, "1"));
  EXPECT_TRUE(config_update_info->configWarming());
  ::testing::Mock::VerifyAndClearExpectations(&observer);

  // A second RDS update arriving while still uninitialized creates a new VHDS subscription on the
  // new update's init manager and marks initialization ready immediately when skip_initial_warming
  // is set.
  auto mock_subscription = std::make_unique<NiceMock<Envoy::Config::MockSubscription>>();
  Envoy::Config::MockSubscription* mock_subscription_ptr = mock_subscription.get();
  EXPECT_CALL(factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _))
      .WillOnce([&mock_subscription](const envoy::config::core::v3::ConfigSource&,
                                     absl::string_view, Stats::Scope&,
                                     Envoy::Config::SubscriptionCallbacks&,
                                     Envoy::Config::OpaqueResourceDecoderSharedPtr,
                                     const Envoy::Config::SubscriptionOptions&) {
        return absl::StatusOr<Envoy::Config::SubscriptionPtr>(std::move(mock_subscription));
      });
  EXPECT_CALL(*mock_subscription_ptr, accept(absl::flat_hash_set<std::string>{"my_route/*"}));
  EXPECT_CALL(*mock_subscription_ptr, start(absl::flat_hash_set<std::string>{}));
  EXPECT_CALL(observer, onConfigWarmed());

  auto route_config_with_skip = route_config;
  route_config_with_skip.mutable_vhds()->mutable_skip_initial_warming();
  EXPECT_OK(config_update_info->onRdsUpdate(route_config_with_skip, "2"));
  EXPECT_FALSE(config_update_info->configWarming());
  EXPECT_TRUE(routeConfiguration(*config_update_info).vhds().has_skip_initial_warming());
}

TEST_F(VhdsTest, SkipInitialWarmingInitialWildcardWakesMatchedAndFallsThroughUnmatched) {
  auto setup = makeSkipInitialWarmingReceiver();

  EXPECT_CALL(*setup.subscription, requestOnDemandUpdate(_)).Times(0);
  testing::MockFunction<void(bool)> matched_cb_fn;
  auto matched_cb =
      std::make_shared<Http::RouteConfigUpdatedCallback>(matched_cb_fn.AsStdFunction());
  testing::MockFunction<void(bool)> missing_cb_fn;
  auto missing_cb =
      std::make_shared<Http::RouteConfigUpdatedCallback>(missing_cb_fn.AsStdFunction());

  EXPECT_CALL(matched_cb_fn, Call(_)).Times(0);
  EXPECT_CALL(missing_cb_fn, Call(_)).Times(0);
  NiceMock<Event::MockDispatcher> worker_dispatcher;
  setup.receiver->requestVirtualHostsUpdate("exact.example.com", worker_dispatcher, matched_cb);
  setup.receiver->requestVirtualHostsUpdate("missing.com", worker_dispatcher, missing_cb);
  ::testing::Mock::VerifyAndClearExpectations(setup.subscription);
  ::testing::Mock::VerifyAndClearExpectations(&matched_cb_fn);
  ::testing::Mock::VerifyAndClearExpectations(&missing_cb_fn);

  // Initial wildcard response matches exact.example.com (waking it immediately without on-demand)
  // while missing.com falls through to a single per-domain requestOnDemandUpdate().
  EXPECT_CALL(matched_cb_fn, Call(true));
  EXPECT_CALL(missing_cb_fn, Call(_)).Times(0);
  EXPECT_CALL(*setup.subscription,
              requestOnDemandUpdate(absl::flat_hash_set<std::string>{"my_route/missing.com"}));
  const auto initial_added =
      buildAddedResources({buildVirtualHost("vhost_exact", "exact.example.com")});
  const auto initial_decoded =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(initial_added);
  EXPECT_OK(setup.callbacks->onConfigUpdate(initial_decoded.refvec_, {}, "1"));
  ::testing::Mock::VerifyAndClearExpectations(setup.subscription);
  ::testing::Mock::VerifyAndClearExpectations(&matched_cb_fn);
  ::testing::Mock::VerifyAndClearExpectations(&missing_cb_fn);

  // Per-domain response wakes missing.com with true.
  auto missing_resources = buildAddedResources({buildVirtualHost("vhost_missing", "missing.com")});
  missing_resources.Mutable(0)->add_aliases("my_route/missing.com");
  const auto missing_decoded =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(missing_resources);
  EXPECT_CALL(missing_cb_fn, Call(true));
  EXPECT_OK(setup.callbacks->onConfigUpdate(missing_decoded.refvec_, {}, "2"));
}

TEST_F(VhdsTest, SkipInitialWarmingEmptyInitialResponseTriggersSingleOnDemandPerDomain) {
  auto setup = makeSkipInitialWarmingReceiver();

  // Two concurrent requests for the same missing domain + one for another missing domain.
  EXPECT_CALL(*setup.subscription, requestOnDemandUpdate(_)).Times(0);
  testing::MockFunction<void(bool)> missing_cb1_fn;
  auto missing_cb1 =
      std::make_shared<Http::RouteConfigUpdatedCallback>(missing_cb1_fn.AsStdFunction());
  testing::MockFunction<void(bool)> missing_cb2_fn;
  auto missing_cb2 =
      std::make_shared<Http::RouteConfigUpdatedCallback>(missing_cb2_fn.AsStdFunction());
  testing::MockFunction<void(bool)> unresolved_cb_fn;
  auto unresolved_cb =
      std::make_shared<Http::RouteConfigUpdatedCallback>(unresolved_cb_fn.AsStdFunction());

  EXPECT_CALL(missing_cb1_fn, Call(_)).Times(0);
  EXPECT_CALL(missing_cb2_fn, Call(_)).Times(0);
  EXPECT_CALL(unresolved_cb_fn, Call(_)).Times(0);
  NiceMock<Event::MockDispatcher> worker_dispatcher;
  setup.receiver->requestVirtualHostsUpdate("missing.com", worker_dispatcher, missing_cb1);
  setup.receiver->requestVirtualHostsUpdate("missing.com", worker_dispatcher, missing_cb2);
  setup.receiver->requestVirtualHostsUpdate("unresolved.com", worker_dispatcher, unresolved_cb);
  ::testing::Mock::VerifyAndClearExpectations(setup.subscription);

  // Initial empty wildcard update must issue each missing alias once and keep callbacks queued.
  EXPECT_CALL(*setup.subscription,
              requestOnDemandUpdate(absl::flat_hash_set<std::string>{"my_route/missing.com"}));
  EXPECT_CALL(*setup.subscription,
              requestOnDemandUpdate(absl::flat_hash_set<std::string>{"my_route/unresolved.com"}));
  const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> empty_added;
  const auto empty_decoded =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(empty_added);
  EXPECT_OK(setup.callbacks->onConfigUpdate(empty_decoded.refvec_, {}, "1"));
  ::testing::Mock::VerifyAndClearExpectations(setup.subscription);
  ::testing::Mock::VerifyAndClearExpectations(&missing_cb1_fn);
  ::testing::Mock::VerifyAndClearExpectations(&missing_cb2_fn);
  ::testing::Mock::VerifyAndClearExpectations(&unresolved_cb_fn);

  // Per-domain response for missing.com wakes both queued callbacks with true.
  auto missing_resources = buildAddedResources({buildVirtualHost("vhost_missing", "missing.com")});
  missing_resources.Mutable(0)->add_aliases("my_route/missing.com");
  const auto missing_decoded =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(missing_resources);
  EXPECT_CALL(missing_cb1_fn, Call(true));
  EXPECT_CALL(missing_cb2_fn, Call(true));
  EXPECT_CALL(unresolved_cb_fn, Call(_)).Times(0);
  EXPECT_OK(setup.callbacks->onConfigUpdate(missing_decoded.refvec_, {}, "2"));
  ::testing::Mock::VerifyAndClearExpectations(&missing_cb1_fn);
  ::testing::Mock::VerifyAndClearExpectations(&missing_cb2_fn);
  ::testing::Mock::VerifyAndClearExpectations(&unresolved_cb_fn);

  // Negative per-domain response for unresolved.com wakes its callback with false.
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> empty_alias_resource;
  empty_alias_resource.Add()->set_name("my_route/unresolved.com");
  const auto empty_alias_decoded =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(empty_alias_resource);
  EXPECT_CALL(unresolved_cb_fn, Call(false));
  EXPECT_OK(setup.callbacks->onConfigUpdate(empty_alias_decoded.refvec_, {}, "3"));
}

TEST_F(VhdsTest, SkipInitialWarmingFailureWakesLatchedRequestsWithFalse) {
  // Case 1: initial fetch timeout while WaitingForResponse.
  {
    auto setup = makeSkipInitialWarmingReceiver();

    EXPECT_CALL(*setup.subscription, requestOnDemandUpdate(_)).Times(0);
    testing::MockFunction<void(bool)> timeout_cb_fn;
    auto timeout_cb =
        std::make_shared<Http::RouteConfigUpdatedCallback>(timeout_cb_fn.AsStdFunction());
    NiceMock<Event::MockDispatcher> worker_dispatcher;
    setup.receiver->requestVirtualHostsUpdate("timeout.com", worker_dispatcher, timeout_cb);
    ::testing::Mock::VerifyAndClearExpectations(setup.subscription);

    EXPECT_CALL(*setup.subscription, requestOnDemandUpdate(_)).Times(0);
    EXPECT_CALL(timeout_cb_fn, Call(false));
    setup.callbacks->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout,
                                          nullptr);
    ::testing::Mock::VerifyAndClearExpectations(setup.subscription);

    // Subsequent on-demand requests after initial failure go directly to requestOnDemandUpdate.
    EXPECT_CALL(*setup.subscription,
                requestOnDemandUpdate(absl::flat_hash_set<std::string>{"my_route/after.com"}));
    testing::MockFunction<void(bool)> after_cb_fn;
    auto after_cb = std::make_shared<Http::RouteConfigUpdatedCallback>(after_cb_fn.AsStdFunction());
    setup.receiver->requestVirtualHostsUpdate("after.com", worker_dispatcher, after_cb);
  }

  // Case 2: invalid initial VHDS response throws during config build and invokes
  // onConfigUpdateFailed(UpdateRejected).
  {
    auto setup = makeSkipInitialWarmingReceiver();

    EXPECT_CALL(*setup.subscription, requestOnDemandUpdate(_)).Times(0);
    testing::MockFunction<void(bool)> rejected_cb_fn;
    auto rejected_cb =
        std::make_shared<Http::RouteConfigUpdatedCallback>(rejected_cb_fn.AsStdFunction());
    NiceMock<Event::MockDispatcher> worker_dispatcher;
    setup.receiver->requestVirtualHostsUpdate("rejected.com", worker_dispatcher, rejected_cb);
    ::testing::Mock::VerifyAndClearExpectations(setup.subscription);

    const auto invalid_vhost = TestUtility::parseYaml<envoy::config::route::v3::VirtualHost>(R"EOF(
      name: invalid_vhost
      domains: ["invalid.example.com"]
      routes:
      - match:
          prefix: "/"
          headers:
            - name: test_header
              string_match:
                safe_regex:
                  regex: "(+invalid regex)"
        route: { cluster: my_service }
    )EOF");
    const auto added_invalid = buildAddedResources({invalid_vhost});
    const auto decoded_invalid =
        TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_invalid);
    EXPECT_THROW(std::ignore = setup.callbacks->onConfigUpdate(decoded_invalid.refvec_, {}, "1"),
                 EnvoyException);

    EXPECT_CALL(*setup.subscription, requestOnDemandUpdate(_)).Times(0);
    EXPECT_CALL(rejected_cb_fn, Call(false));
    EnvoyException exception("invalid weighted_clusters");
    setup.callbacks->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected,
                                          &exception);
  }
}

TEST_F(VhdsTest, SkipInitialWarmingIntermediateRdsUpdatesAndVhdsRemoval) {
  auto setup = makeSkipInitialWarmingReceiver();

  EXPECT_CALL(*setup.subscription, requestOnDemandUpdate(_)).Times(0);
  testing::MockFunction<void(bool)> rds_added_cb_fn;
  auto rds_added_cb =
      std::make_shared<Http::RouteConfigUpdatedCallback>(rds_added_cb_fn.AsStdFunction());
  testing::MockFunction<void(bool)> still_waiting_cb_fn;
  auto still_waiting_cb =
      std::make_shared<Http::RouteConfigUpdatedCallback>(still_waiting_cb_fn.AsStdFunction());
  testing::MockFunction<void(bool)> rds_missing_cb_fn;
  auto rds_missing_cb =
      std::make_shared<Http::RouteConfigUpdatedCallback>(rds_missing_cb_fn.AsStdFunction());

  NiceMock<Event::MockDispatcher> worker_dispatcher;
  setup.receiver->requestVirtualHostsUpdate("rds_added.com", worker_dispatcher, rds_added_cb);
  setup.receiver->requestVirtualHostsUpdate("still_waiting.com", worker_dispatcher,
                                            still_waiting_cb);

  // Intermediate RDS update keeping VHDS adds rds_added.com, waking only its callback while keeping
  // still_waiting.com latched.
  auto intermediate_rds = setup.route_config;
  *intermediate_rds.add_virtual_hosts() = buildVirtualHost("vhost_rds_added", "rds_added.com");
  EXPECT_CALL(rds_added_cb_fn, Call(true));
  EXPECT_CALL(still_waiting_cb_fn, Call(_)).Times(0);
  EXPECT_OK(setup.receiver->onRdsUpdate(intermediate_rds, "2"));
  ::testing::Mock::VerifyAndClearExpectations(&rds_added_cb_fn);
  ::testing::Mock::VerifyAndClearExpectations(&still_waiting_cb_fn);

  setup.receiver->requestVirtualHostsUpdate("rds_missing.com", worker_dispatcher, rds_missing_cb);

  // An RDS update that removes VHDS completely resolves remaining latched callbacks against the
  // updated RDS route configuration without stranding them.
  envoy::config::route::v3::RouteConfiguration rds_without_vhds;
  rds_without_vhds.set_name("my_route");
  *rds_without_vhds.add_virtual_hosts() =
      buildVirtualHost("vhost_still_waiting", "still_waiting.com");
  EXPECT_CALL(still_waiting_cb_fn, Call(true));
  EXPECT_CALL(rds_missing_cb_fn, Call(false));
  EXPECT_OK(setup.receiver->onRdsUpdate(rds_without_vhds, "3"));
}

TEST_F(VhdsTest, SkipInitialWarmingRemovedByRdsUpdateFlushesLatchedCallbacks) {
  auto setup = makeSkipInitialWarmingReceiver();

  EXPECT_CALL(*setup.subscription, requestOnDemandUpdate(_)).Times(0);
  testing::MockFunction<void(bool)> latched_cb_fn;
  auto latched_cb =
      std::make_shared<Http::RouteConfigUpdatedCallback>(latched_cb_fn.AsStdFunction());
  EXPECT_CALL(latched_cb_fn, Call(_)).Times(0);
  NiceMock<Event::MockDispatcher> worker_dispatcher;
  setup.receiver->requestVirtualHostsUpdate("flushed.com", worker_dispatcher, latched_cb);

  // Replace VHDS subscription with a new config_source without skip_initial_warming.
  auto second_subscription = std::make_unique<NiceMock<Envoy::Config::MockSubscription>>();
  Envoy::Config::MockSubscription* second_subscription_ptr = second_subscription.get();
  Envoy::Config::SubscriptionCallbacks* second_callbacks = nullptr;
  EXPECT_CALL(factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _))
      .WillOnce([&second_subscription,
                 &second_callbacks](const envoy::config::core::v3::ConfigSource&, absl::string_view,
                                    Stats::Scope&, Envoy::Config::SubscriptionCallbacks& callbacks,
                                    Envoy::Config::OpaqueResourceDecoderSharedPtr,
                                    const Envoy::Config::SubscriptionOptions&) {
        second_callbacks = &callbacks;
        return absl::StatusOr<Envoy::Config::SubscriptionPtr>(std::move(second_subscription));
      });
  {
    testing::InSequence s;
    EXPECT_CALL(*second_subscription_ptr, accept(absl::flat_hash_set<std::string>{"my_route/*"}));
    EXPECT_CALL(*second_subscription_ptr, start(absl::flat_hash_set<std::string>{}));
    EXPECT_CALL(*second_subscription_ptr,
                requestOnDemandUpdate(absl::flat_hash_set<std::string>{"my_route/flushed.com"}));
  }

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
  EXPECT_OK(setup.receiver->onRdsUpdate(updated_route_config, "2"));
  ASSERT_NE(nullptr, second_callbacks);
  ::testing::Mock::VerifyAndClearExpectations(&latched_cb_fn);

  auto flushed_resources = buildAddedResources({buildVirtualHost("vhost_flushed", "flushed.com")});
  flushed_resources.Mutable(0)->add_aliases("my_route/flushed.com");
  const auto flushed_decoded =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(flushed_resources);
  EXPECT_CALL(latched_cb_fn, Call(true));
  EXPECT_OK(second_callbacks->onConfigUpdate(flushed_decoded.refvec_, {}, "1"));
}

TEST_F(VhdsTest, SkipInitialWarmingResolvesInFlightWorkerRequestsPostedBeforeWildcardPublish) {
  std::vector<Event::PostCb> buffered_posts;
  bool buffer_posts = false;
  ON_CALL(factory_context_.dispatcher_, post(_))
      .WillByDefault([&buffered_posts, &buffer_posts](Event::PostCb cb) {
        if (buffer_posts) {
          buffered_posts.push_back(std::move(cb));
        } else {
          cb();
        }
      });

  std::function<void()> pending_thread_local_complete;
  ON_CALL(factory_context_.thread_local_, runOnAllThreads(_, _))
      .WillByDefault([&pending_thread_local_complete](std::function<void()> cb,
                                                      std::function<void()> main_callback) {
        cb();
        pending_thread_local_complete = std::move(main_callback);
      });

  // Case 1: Initial wildcard publish delivers both an exact vhost and a catch-all '*' fallback.
  // In-flight worker posts evaluated against the pre-VHDS TLS snapshot (queued before publish or
  // posted while TLS updates are still propagating across workers) resolve locally with zero
  // updateOnDemand() calls.
  {
    auto setup = makeSkipInitialWarmingReceiver();
    NiceMock<Event::MockDispatcher> worker_dispatcher;

    testing::MockFunction<void(bool)> pre_publish_exact_fn;
    auto pre_publish_exact_cb =
        std::make_shared<Http::RouteConfigUpdatedCallback>(pre_publish_exact_fn.AsStdFunction());
    testing::MockFunction<void(bool)> pre_publish_wildcard_fn;
    auto pre_publish_wildcard_cb =
        std::make_shared<Http::RouteConfigUpdatedCallback>(pre_publish_wildcard_fn.AsStdFunction());

    buffer_posts = true;
    setup.receiver->requestVirtualHostsUpdate("pre-exact.example.com", worker_dispatcher,
                                              pre_publish_exact_cb);
    setup.receiver->requestVirtualHostsUpdate("pre-wildcard.example.com", worker_dispatcher,
                                              pre_publish_wildcard_cb);
    buffer_posts = false;
    ASSERT_EQ(2, buffered_posts.size());

    EXPECT_CALL(*setup.subscription, requestOnDemandUpdate(_)).Times(0);
    EXPECT_CALL(pre_publish_exact_fn, Call(_)).Times(0);
    EXPECT_CALL(pre_publish_wildcard_fn, Call(_)).Times(0);
    const auto added_resources =
        buildAddedResources({buildVirtualHost("vhost_exact", "pre-exact.example.com"),
                             buildVirtualHost("vhost_wildcard", "\"*\"")});
    const auto decoded_resources =
        TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
    EXPECT_OK(setup.callbacks->onConfigUpdate(decoded_resources.refvec_, {}, "1"));
    ASSERT_TRUE(pending_thread_local_complete != nullptr);
    ::testing::Mock::VerifyAndClearExpectations(&pre_publish_exact_fn);
    ::testing::Mock::VerifyAndClearExpectations(&pre_publish_wildcard_fn);

    EXPECT_CALL(pre_publish_exact_fn, Call(true));
    EXPECT_CALL(pre_publish_wildcard_fn, Call(true));
    for (auto& post : buffered_posts) {
      post();
    }
    buffered_posts.clear();

    testing::MockFunction<void(bool)> during_propagation_cb_fn;
    auto during_propagation_cb = std::make_shared<Http::RouteConfigUpdatedCallback>(
        during_propagation_cb_fn.AsStdFunction());
    EXPECT_CALL(during_propagation_cb_fn, Call(true));
    setup.receiver->requestVirtualHostsUpdate("during-prop.example.com", worker_dispatcher,
                                              during_propagation_cb);
    ::testing::Mock::VerifyAndClearExpectations(setup.subscription);

    pending_thread_local_complete();
    pending_thread_local_complete = nullptr;
  }

  // Case 2: Initial wildcard publish without catch-all '*' still forwards truly missing domains
  // to updateOnDemand(), and an invalidating RDS update cancels an in-flight PropagatingToWorkers
  // transition via the generation counter.
  {
    auto setup = makeSkipInitialWarmingReceiver();
    NiceMock<Event::MockDispatcher> worker_dispatcher;

    const auto exact_only_resources =
        buildAddedResources({buildVirtualHost("vhost_exact", "exact.example.com")});
    const auto exact_only_decoded =
        TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(exact_only_resources);
    EXPECT_OK(setup.callbacks->onConfigUpdate(exact_only_decoded.refvec_, {}, "1"));
    ASSERT_TRUE(pending_thread_local_complete != nullptr);
    auto stale_thread_local_complete = std::move(pending_thread_local_complete);
    pending_thread_local_complete = nullptr;

    testing::MockFunction<void(bool)> missing_cb_fn;
    auto missing_cb =
        std::make_shared<Http::RouteConfigUpdatedCallback>(missing_cb_fn.AsStdFunction());
    EXPECT_CALL(*setup.subscription,
                requestOnDemandUpdate(absl::flat_hash_set<std::string>{"my_route/missing.com"}));
    EXPECT_CALL(missing_cb_fn, Call(_)).Times(0);
    setup.receiver->requestVirtualHostsUpdate("missing.com", worker_dispatcher, missing_cb);
    ::testing::Mock::VerifyAndClearExpectations(setup.subscription);

    // Replace VHDS subscription with another skip_initial_warming config while
    // stale_thread_local_complete is still pending; running stale_thread_local_complete afterwards
    // must be ignored.
    auto replacement_subscription = std::make_unique<NiceMock<Envoy::Config::MockSubscription>>();
    Envoy::Config::MockSubscription* replacement_subscription_ptr = replacement_subscription.get();
    EXPECT_CALL(factory_context_.cluster_manager_.subscription_factory_,
                subscriptionFromConfigSource(_, _, _, _, _, _))
        .WillOnce([&replacement_subscription](const envoy::config::core::v3::ConfigSource&,
                                              absl::string_view, Stats::Scope&,
                                              Envoy::Config::SubscriptionCallbacks&,
                                              Envoy::Config::OpaqueResourceDecoderSharedPtr,
                                              const Envoy::Config::SubscriptionOptions&) {
          return absl::StatusOr<Envoy::Config::SubscriptionPtr>(
              std::move(replacement_subscription));
        });
    auto updated_route_config =
        TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(R"EOF(
name: my_route
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: another_xds_cluster
  skip_initial_warming: {}
)EOF");
    EXPECT_OK(setup.receiver->onRdsUpdate(updated_route_config, "2"));
    stale_thread_local_complete();

    EXPECT_CALL(*replacement_subscription_ptr, requestOnDemandUpdate(_)).Times(0);
    testing::MockFunction<void(bool)> new_latched_cb_fn;
    auto new_latched_cb =
        std::make_shared<Http::RouteConfigUpdatedCallback>(new_latched_cb_fn.AsStdFunction());
    setup.receiver->requestVirtualHostsUpdate("new-latched.com", worker_dispatcher, new_latched_cb);
  }
}

TEST_F(VhdsTest, SkipInitialWarmingDisabledOnSameConfigSourceFlushesLatchedCallbacks) {
  auto setup = makeSkipInitialWarmingReceiver();

  EXPECT_CALL(*setup.subscription, requestOnDemandUpdate(_)).Times(0);
  testing::MockFunction<void(bool)> rds_covered_cb_fn;
  auto rds_covered_cb =
      std::make_shared<Http::RouteConfigUpdatedCallback>(rds_covered_cb_fn.AsStdFunction());
  testing::MockFunction<void(bool)> needs_on_demand_cb_fn;
  auto needs_on_demand_cb =
      std::make_shared<Http::RouteConfigUpdatedCallback>(needs_on_demand_cb_fn.AsStdFunction());
  EXPECT_CALL(rds_covered_cb_fn, Call(_)).Times(0);
  EXPECT_CALL(needs_on_demand_cb_fn, Call(_)).Times(0);

  NiceMock<Event::MockDispatcher> worker_dispatcher;
  setup.receiver->requestVirtualHostsUpdate("rds-covered.com", worker_dispatcher, rds_covered_cb);
  setup.receiver->requestVirtualHostsUpdate("needs-on-demand.com", worker_dispatcher,
                                            needs_on_demand_cb);
  ::testing::Mock::VerifyAndClearExpectations(setup.subscription);
  ::testing::Mock::VerifyAndClearExpectations(&rds_covered_cb_fn);
  ::testing::Mock::VerifyAndClearExpectations(&needs_on_demand_cb_fn);

  // Roll back skip_initial_warming via RDS keeping the same vhds.config_source. The existing
  // subscription must survive and latched callbacks must flush immediately once RDS publishes.
  EXPECT_CALL(factory_context_.cluster_manager_.subscription_factory_,
              subscriptionFromConfigSource(_, _, _, _, _, _))
      .Times(0);
  EXPECT_CALL(rds_covered_cb_fn, Call(true));
  EXPECT_CALL(*setup.subscription, requestOnDemandUpdate(absl::flat_hash_set<std::string>{
                                       "my_route/needs-on-demand.com"}));
  EXPECT_CALL(needs_on_demand_cb_fn, Call(_)).Times(0);

  auto rollback_rds = setup.route_config;
  rollback_rds.mutable_vhds()->clear_skip_initial_warming();
  *rollback_rds.add_virtual_hosts() = buildVirtualHost("vhost_rds_covered", "rds-covered.com");
  EXPECT_OK(setup.receiver->onRdsUpdate(rollback_rds, "2"));
  ::testing::Mock::VerifyAndClearExpectations(setup.subscription);
  ::testing::Mock::VerifyAndClearExpectations(&rds_covered_cb_fn);
  ::testing::Mock::VerifyAndClearExpectations(&needs_on_demand_cb_fn);

  auto on_demand_resources =
      buildAddedResources({buildVirtualHost("vhost_on_demand", "needs-on-demand.com")});
  on_demand_resources.Mutable(0)->add_aliases("my_route/needs-on-demand.com");
  const auto on_demand_decoded =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(on_demand_resources);
  EXPECT_CALL(needs_on_demand_cb_fn, Call(true));
  EXPECT_OK(setup.callbacks->onConfigUpdate(on_demand_decoded.refvec_, {}, "1"));

  EXPECT_CALL(*setup.subscription,
              requestOnDemandUpdate(absl::flat_hash_set<std::string>{"my_route/after.com"}));
  testing::MockFunction<void(bool)> after_cb_fn;
  auto after_cb = std::make_shared<Http::RouteConfigUpdatedCallback>(after_cb_fn.AsStdFunction());
  setup.receiver->requestVirtualHostsUpdate("after.com", worker_dispatcher, after_cb);
}

TEST_F(VhdsTest, SkipInitialWarmingNoopVhdsUpdateWhileWarmingDefersCallbackResolution) {
  class WarmingRouteFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
  public:
    struct EmptyRouteSpecificFilterConfig : public RouteSpecificFilterConfig {};

    WarmingRouteFilterConfig() : EmptyHttpFilterConfig("vhds.test.warming_filter") {}

    absl::StatusOr<Http::FilterFactoryCb>
    createFilter(const std::string&, Server::Configuration::FactoryContext&) override {
      PANIC("not implemented");
    }
    ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override {
      return std::make_unique<Protobuf::Timestamp>();
    }
    ProtobufTypes::MessagePtr createEmptyConfigProto() override {
      return std::make_unique<Protobuf::Timestamp>();
    }
    std::set<std::string> configTypes() override { return {"google.protobuf.Timestamp"}; }
    absl::StatusOr<RouteSpecificFilterConfigConstSharedPtr> createHttpFilterRouteConfig(
        const Protobuf::Message&, Server::Configuration::ServerFactoryContext&,
        Server::Configuration::ExtraFactoryContext& extra_context) override {
      target_ = std::make_unique<Init::TargetImpl>("vhds_test_warming_filter", []() {});
      if (extra_context.init_manager.has_value()) {
        extra_context.init_manager->add(*target_);
      }
      return std::make_shared<EmptyRouteSpecificFilterConfig>();
    }

    void markReady() {
      if (target_ != nullptr) {
        target_->ready();
      }
    }

  private:
    std::unique_ptr<Init::TargetImpl> target_;
  };

  WarmingRouteFilterConfig warming_filter_factory;
  Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> registered_factory(
      warming_filter_factory);

  auto setup = makeSkipInitialWarmingReceiver();

  EXPECT_CALL(*setup.subscription, requestOnDemandUpdate(_)).Times(0);
  testing::MockFunction<void(bool)> warming_rds_cb_fn;
  auto warming_rds_cb =
      std::make_shared<Http::RouteConfigUpdatedCallback>(warming_rds_cb_fn.AsStdFunction());
  testing::MockFunction<void(bool)> still_missing_cb_fn;
  auto still_missing_cb =
      std::make_shared<Http::RouteConfigUpdatedCallback>(still_missing_cb_fn.AsStdFunction());
  EXPECT_CALL(warming_rds_cb_fn, Call(_)).Times(0);
  EXPECT_CALL(still_missing_cb_fn, Call(_)).Times(0);

  NiceMock<Event::MockDispatcher> worker_dispatcher;
  setup.receiver->requestVirtualHostsUpdate("warming-rds.com", worker_dispatcher, warming_rds_cb);
  setup.receiver->requestVirtualHostsUpdate("still-missing.com", worker_dispatcher,
                                            still_missing_cb);
  ::testing::Mock::VerifyAndClearExpectations(setup.subscription);
  ::testing::Mock::VerifyAndClearExpectations(&warming_rds_cb_fn);
  ::testing::Mock::VerifyAndClearExpectations(&still_missing_cb_fn);

  // Start an RDS update adding warming-rds.com that stays warming via the filter init target.
  auto warming_rds = setup.route_config;
  auto* vhost = warming_rds.add_virtual_hosts();
  *vhost = buildVirtualHost("vhost_warming_rds", "warming-rds.com");
  Protobuf::Timestamp filter_config;
  filter_config.set_seconds(1);
  ASSERT_TRUE((*warming_rds.mutable_typed_per_filter_config())["vhds.test.warming_filter"].PackFrom(
      filter_config));
  EXPECT_CALL(*setup.observer, onConfigWarmed()).Times(0);
  EXPECT_OK(setup.receiver->onRdsUpdate(warming_rds, "2"));
  ASSERT_TRUE(setup.receiver->configWarming());
  ::testing::Mock::VerifyAndClearExpectations(setup.observer.get());

  // An empty no-op initial VHDS update arriving while warming must transition to WarmingResponse
  // and defer latched callback resolution until the warming update publishes, rather than
  // prematurely calling requestOnDemandUpdate() against the stale pre-warming config.
  EXPECT_CALL(*setup.subscription, requestOnDemandUpdate(_)).Times(0);
  EXPECT_CALL(warming_rds_cb_fn, Call(_)).Times(0);
  EXPECT_CALL(still_missing_cb_fn, Call(_)).Times(0);
  const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> empty_added;
  const auto empty_decoded =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(empty_added);
  EXPECT_OK(setup.callbacks->onConfigUpdate(empty_decoded.refvec_, {}, "1"));
  ::testing::Mock::VerifyAndClearExpectations(setup.subscription);
  ::testing::Mock::VerifyAndClearExpectations(&warming_rds_cb_fn);
  ::testing::Mock::VerifyAndClearExpectations(&still_missing_cb_fn);

  // Completing warming publishes the new RDS config and resolves latched callbacks against it.
  EXPECT_CALL(*setup.observer, onConfigWarmed()).WillOnce([&]() {
    setup.receiver->onVhdsConfigPublished();
  });
  EXPECT_CALL(warming_rds_cb_fn, Call(true));
  EXPECT_CALL(*setup.subscription, requestOnDemandUpdate(absl::flat_hash_set<std::string>{
                                       "my_route/still-missing.com"}));
  EXPECT_CALL(still_missing_cb_fn, Call(_)).Times(0);
  warming_filter_factory.markReady();
  ASSERT_FALSE(setup.receiver->configWarming());
}

} // namespace
} // namespace Router
} // namespace Envoy
