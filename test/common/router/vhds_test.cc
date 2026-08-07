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
  RouteConfigUpdatePtr
  makeRouteConfigUpdate(const envoy::config::route::v3::RouteConfiguration& rc) {
    RouteConfigUpdatePtr config_update_info =
        std::make_unique<RouteConfigUpdateReceiverImpl>(proto_traits_, factory_context_);
    config_update_info->onRdsUpdate(rc, init_manager_, "1");
    return config_update_info;
  }

  ProtoTraitsImpl proto_traits_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  Init::ManagerImpl init_manager_{"test route config"};
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

  EXPECT_OK(VhdsSubscription::createVhdsSubscription(config_update_info, factory_context_, context_,
                                                     provider_)
                .status());
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

  EXPECT_THAT(VhdsSubscription::createVhdsSubscription(config_update_info, factory_context_,
                                                       context_, provider_)
                  .status(),
              Not(IsOk()));
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
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  EXPECT_THAT(VhdsSubscription::createVhdsSubscription(config_update_info, factory_context_,
                                                       context_, provider_)
                  .status(),
              Not(IsOk()));
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
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  EXPECT_OK(VhdsSubscription::createVhdsSubscription(config_update_info, factory_context_, context_,
                                                     provider_)
                .status());
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
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  auto result = VhdsSubscription::createVhdsSubscription(config_update_info, factory_context_,
                                                         context_, provider_);
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
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  auto result = VhdsSubscription::createVhdsSubscription(config_update_info, factory_context_,
                                                         context_, provider_);
  EXPECT_THAT(
      result,
      HasStatusMessage("vhds: ADS must use DELTA_GRPC api_type when used as VHDS config source."));
}

// verify addition/updating of virtual hosts
TEST_F(VhdsTest, VhdsAddsVirtualHosts) {
  const auto route_config =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
  RouteConfigUpdatePtr config_update_info = makeRouteConfigUpdate(route_config);

  VhdsSubscriptionPtr subscription = VhdsSubscription::createVhdsSubscription(
                                         config_update_info, factory_context_, context_, provider_)
                                         .value();
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

  VhdsSubscriptionPtr subscription = VhdsSubscription::createVhdsSubscription(
                                         config_update_info, factory_context_, context_, provider_)
                                         .value();
  EXPECT_EQ(1UL, config_update_info->protobufConfigurationCast().virtual_hosts_size());
  EXPECT_EQ("vhost_rds1", config_update_info->protobufConfigurationCast().virtual_hosts(0).name());

  auto vhost = buildVirtualHost("vhost_vhds1", "vhost.first");
  const auto& added_resources = buildAddedResources({vhost});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, removed_resources, "1"));
  EXPECT_EQ(2UL, config_update_info->protobufConfigurationCast().virtual_hosts_size());

  config_update_info->onRdsUpdate(updated_route_config, init_manager_, "2");

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

  VhdsSubscriptionPtr subscription = VhdsSubscription::createVhdsSubscription(
                                         config_update_info, factory_context_, context_, provider_)
                                         .value();
  const auto config_before_update = config_update_info->parsedConfiguration();

  const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> added_resources;
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
  EXPECT_OK(factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
      decoded_resources.refvec_, buildRemovedResources({"never_added_vhost"}), "2"));

  // No new route configuration was built.
  EXPECT_EQ(config_before_update, config_update_info->parsedConfiguration());
}

class MockRouteConfigUpdateReceiver : public RouteConfigUpdateReceiver {
public:
  // Rds::RouteConfigUpdateReceiver
  MOCK_METHOD(bool, onRdsUpdate,
              (const Protobuf::Message& rc, Init::Manager& init_manager,
               const std::string& version_info));
  MOCK_METHOD(uint64_t, configHash, (), (const));
  MOCK_METHOD(const std::optional<Rds::RouteConfigProvider::ConfigInfo>&, configInfo, (), (const));
  MOCK_METHOD(const Protobuf::Message&, protobufConfiguration, (), (const));
  MOCK_METHOD(Rds::ConfigConstSharedPtr, parsedConfiguration, (), (const));
  MOCK_METHOD(SystemTime, lastUpdated, (), (const));

  // Router::RouteConfigUpdateReceiver
  MOCK_METHOD(const envoy::config::route::v3::RouteConfiguration&, protobufConfigurationCast, (),
              (const));
  MOCK_METHOD(bool, onVhdsUpdate,
              (const VirtualHostRefVector& added_vhosts, std::set<std::string>&& added_resource_ids,
               const Protobuf::RepeatedPtrField<std::string>& removed_resources,
               Init::Manager& init_manager, const std::string& version_info));
  MOCK_METHOD(bool, vhdsConfigurationChanged, (), (const));
  MOCK_METHOD(const std::set<std::string>&, resourceIdsInLastVhdsUpdate, (), (const));
};

// A resource that is owned by the route configuration built by a VHDS update and that needs to be
// warmed up before that route configuration can be published.
class WarmingResource {
public:
  WarmingResource(absl::string_view name, Init::Manager& init_manager)
      : target_(name, [this]() { initializing_ = true; }) {
    init_manager.add(target_);
  }

  // Simulates this resource becoming ready.
  void ready() { target_.ready(); }

  // Whether the init manager that this resource was registered with has started warming it up.
  bool initializing_{false};

private:
  Init::TargetImpl target_;
};

// A route config provider that records how often a route configuration was published through it
// and that can be told to fail publishing.
class TestRouteConfigProvider : public Rds::RouteConfigProvider {
public:
  Rds::ConfigConstSharedPtr config() const override { return nullptr; }
  const std::optional<ConfigInfo>& configInfo() const override { return config_info_; }
  SystemTime lastUpdated() const override { return {}; }
  absl::Status onConfigUpdate() override {
    ++config_updates_;
    return publish_status_;
  }

  uint64_t config_updates_{0};
  absl::Status publish_status_;

private:
  const std::optional<ConfigInfo> config_info_;
};

class VhdsWarmingTest : public VhdsTest {
public:
  // What the mocked receiver does with the next VHDS update.
  enum class NextUpdate {
    // Builds a route configuration that owns a resource that needs to be warmed up.
    Warming,
    // Builds a route configuration that has nothing to warm up.
    NothingToWarmUp,
    // Leaves the currently published route configuration in place.
    Noop,
  };

  void createSubscription() {
    route_config_ =
        TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(default_vhds_config_);
    auto config_update_info = std::make_unique<NiceMock<MockRouteConfigUpdateReceiver>>();
    config_update_info_ = config_update_info.get();
    ON_CALL(*config_update_info_, protobufConfigurationCast())
        .WillByDefault(testing::ReturnRef(route_config_));
    ON_CALL(*config_update_info_,
            onVhdsUpdate(testing::_, testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(testing::Invoke(
            [this](const RouteConfigUpdateReceiver::VirtualHostRefVector&, std::set<std::string>&&,
                   const Protobuf::RepeatedPtrField<std::string>&, Init::Manager& init_manager,
                   const std::string& version_info) {
              if (next_update_ == NextUpdate::Noop) {
                return false;
              }
              if (next_update_ == NextUpdate::Warming) {
                warming_resources_.push_back(std::make_unique<WarmingResource>(
                    fmt::format("vhds warming resource {}", version_info), init_manager));
              }
              return true;
            }));
    receiver_ = std::move(config_update_info);

    subscription_ = VhdsSubscription::createVhdsSubscription(receiver_, factory_context_, context_,
                                                             &route_config_provider_)
                        .value();
    subscription_->registerInitTargetWithInitManager(init_manager_);
    // Starts the subscription.
    init_manager_.initialize(init_watcher_);
  }

  absl::Status pushUpdate(const std::vector<std::string>& added,
                          const std::vector<std::string>& removed,
                          const std::string& version_info) {
    std::vector<envoy::config::route::v3::VirtualHost> vhosts;
    vhosts.reserve(added.size());
    for (const auto& name : added) {
      vhosts.push_back(buildVirtualHost(name, name + ".domain"));
    }
    const auto added_resources = buildAddedResources(vhosts);
    const auto decoded_resources =
        TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(added_resources);
    return factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
        decoded_resources.refvec_, buildRemovedResources(removed), version_info);
  }

  uint64_t configReloads() {
    return factory_context_.store_.counter(context_ + "vhds.my_route.config_reload").value();
  }

  NextUpdate next_update_{NextUpdate::Warming};
  std::vector<std::unique_ptr<WarmingResource>> warming_resources_;
  envoy::config::route::v3::RouteConfiguration route_config_;
  TestRouteConfigProvider route_config_provider_;
  NiceMock<MockRouteConfigUpdateReceiver>* config_update_info_{nullptr};
  RouteConfigUpdatePtr receiver_;
  VhdsSubscriptionPtr subscription_;
};

// A route configuration that is still warming up isn't published, and the subscription doesn't
// signal readiness until it is.
TEST_F(VhdsWarmingTest, UpdateIsPublishedOnlyAfterItIsWarmedUp) {
  init_watcher_.expectReady().Times(0);
  createSubscription();

  EXPECT_OK(pushUpdate({"vhost1"}, {}, "1"));
  ASSERT_EQ(1UL, warming_resources_.size());
  EXPECT_TRUE(warming_resources_[0]->initializing_);
  // Not published yet.
  EXPECT_EQ(0UL, route_config_provider_.config_updates_);
  EXPECT_EQ(0UL, configReloads());
  ::testing::Mock::VerifyAndClearExpectations(&init_watcher_);

  init_watcher_.expectReady();
  warming_resources_[0]->ready();
  EXPECT_EQ(1UL, route_config_provider_.config_updates_);
  EXPECT_EQ(1UL, configReloads());
}

// A route configuration that has nothing to warm up is published synchronously, i.e. from within
// onConfigUpdate().
TEST_F(VhdsWarmingTest, UpdateWithNothingToWarmUpIsPublishedSynchronously) {
  next_update_ = NextUpdate::NothingToWarmUp;
  init_watcher_.expectReady();
  createSubscription();

  EXPECT_OK(pushUpdate({"vhost1"}, {}, "1"));
  EXPECT_EQ(1UL, route_config_provider_.config_updates_);
  EXPECT_EQ(1UL, configReloads());
  EXPECT_TRUE(warming_resources_.empty());
}

// A VHDS update that arrives while a previous update is still warming up supersedes it. The
// superseded update is never published, even if it finishes warming up.
TEST_F(VhdsWarmingTest, UpdateWhileWarmingSupersedesThePreviousUpdate) {
  init_watcher_.expectReady().Times(0);
  createSubscription();

  EXPECT_OK(pushUpdate({"vhost1"}, {}, "1"));
  EXPECT_OK(pushUpdate({"vhost2"}, {}, "2"));
  ASSERT_EQ(2UL, warming_resources_.size());

  // The abandoned update publishes nothing and doesn't signal readiness.
  warming_resources_[0]->ready();
  EXPECT_EQ(0UL, route_config_provider_.config_updates_);
  EXPECT_EQ(0UL, configReloads());
  ::testing::Mock::VerifyAndClearExpectations(&init_watcher_);

  init_watcher_.expectReady();
  warming_resources_[1]->ready();
  EXPECT_EQ(1UL, route_config_provider_.config_updates_);
  EXPECT_EQ(1UL, configReloads());
}

// A VHDS update that turns out to be a no-op leaves an update that is still warming up alone, and
// doesn't signal readiness on its behalf.
TEST_F(VhdsWarmingTest, NoopUpdateWhileWarmingLeavesTheWarmingUpdateAlone) {
  init_watcher_.expectReady().Times(0);
  createSubscription();

  EXPECT_OK(pushUpdate({"vhost1"}, {}, "1"));
  next_update_ = NextUpdate::Noop;
  EXPECT_OK(pushUpdate({}, {}, "2"));
  ASSERT_EQ(1UL, warming_resources_.size());
  ::testing::Mock::VerifyAndClearExpectations(&init_watcher_);

  init_watcher_.expectReady();
  warming_resources_[0]->ready();
  EXPECT_EQ(1UL, route_config_provider_.config_updates_);
}

// A VHDS update that turns out to be a no-op while nothing is warming up signals readiness right
// away without publishing anything.
TEST_F(VhdsWarmingTest, NoopUpdateSignalsReadinessWithoutPublishing) {
  next_update_ = NextUpdate::Noop;
  init_watcher_.expectReady();
  createSubscription();

  EXPECT_OK(pushUpdate({}, {}, "1"));
  EXPECT_EQ(0UL, route_config_provider_.config_updates_);
  EXPECT_EQ(0UL, configReloads());
}

// A failure to publish a route configuration that was warmed up asynchronously can't be reported to
// the xDS layer anymore, and the subscription stays unready.
TEST_F(VhdsWarmingTest, FailureToPublishAWarmedUpUpdate) {
  init_watcher_.expectReady().Times(0);
  createSubscription();

  EXPECT_OK(pushUpdate({"vhost1"}, {}, "1"));
  ASSERT_EQ(1UL, warming_resources_.size());

  route_config_provider_.publish_status_ = absl::InvalidArgumentError("publishing failed");
  warming_resources_[0]->ready();
  EXPECT_EQ(1UL, route_config_provider_.config_updates_);

  // The subscription only signals readiness when it is destroyed.
  ::testing::Mock::VerifyAndClearExpectations(&init_watcher_);
  init_watcher_.expectReady();
}

// A failure to publish a route configuration that had nothing to warm up is reported back to the
// xDS layer as a rejection of the update.
TEST_F(VhdsWarmingTest, FailureToPublishAnUpdateWithNothingToWarmUp) {
  next_update_ = NextUpdate::NothingToWarmUp;
  init_watcher_.expectReady().Times(0);
  createSubscription();

  route_config_provider_.publish_status_ = absl::InvalidArgumentError("publishing failed");
  EXPECT_EQ("publishing failed", pushUpdate({"vhost1"}, {}, "1").message());

  // The subscription only signals readiness when it is destroyed. In production the xDS layer
  // rejects the update and calls onConfigUpdateFailed(), which signals readiness.
  ::testing::Mock::VerifyAndClearExpectations(&init_watcher_);
  init_watcher_.expectReady();
}

} // namespace
} // namespace Router
} // namespace Envoy
