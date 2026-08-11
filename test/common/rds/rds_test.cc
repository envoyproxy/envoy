#include <chrono>
#include <memory>
#include <string>

#include "envoy/admin/v3/config_dump_shared.pb.h"
#include "envoy/admin/v3/config_dump_shared.pb.validate.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.validate.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/config/opaque_resource_decoder_impl.h"
#include "source/common/init/manager_impl.h"
#include "source/common/rds/common/route_config_provider_manager_impl.h"
#include "source/common/rds/rds_route_config_provider_impl.h"
#include "source/common/rds/rds_route_config_subscription.h"
#include "source/common/rds/route_config_provider_manager.h"
#include "source/common/rds/route_config_update_receiver_impl.h"
#include "source/common/rds/static_route_config_provider_impl.h"

#include "test/mocks/init/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {
namespace Rds {
namespace {

class RdsTestBase : public testing::Test {
public:
  RdsTestBase() {
    ON_CALL(server_factory_context_, scope()).WillByDefault(ReturnRef(*scope_.rootScope()));
    ON_CALL(server_factory_context_, messageValidationContext())
        .WillByDefault(ReturnRef(validation_context_));
  }

  ~RdsTestBase() override { server_factory_context_.thread_local_.shutdownThread(); }

  Event::SimulatedTimeSystem& timeSystem() { return time_system_; }

  Event::SimulatedTimeSystem time_system_;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
  Init::ManagerImpl init_manager_{"test route config"};
};

class TestConfig : public Config {
public:
  TestConfig() = default;
  TestConfig(const envoy::config::route::v3::RouteConfiguration& rc,
             Server::Configuration::ServerFactoryContext&, bool)
      : rc_(rc) {}
  const std::string* route(const std::string& name) const {
    for (const auto& virtual_host_config : rc_.virtual_hosts()) {
      if (virtual_host_config.name() == name) {
        return &virtual_host_config.name();
      }
    }
    return nullptr;
  }

private:
  envoy::config::route::v3::RouteConfiguration rc_;
};

class RdsConfigUpdateReceiverTest : public RdsTestBase {
public:
  void setup() {
    config_update_ = std::make_unique<RouteConfigUpdateReceiverImpl>(config_traits_, proto_traits_,
                                                                     server_factory_context_);
  }

  const std::string* route(const std::string& path) {
    return std::static_pointer_cast<const TestConfig>(config_update_->parsedConfiguration())
        ->route(path);
  }

  Common::ProtoTraitsImpl<envoy::config::route::v3::RouteConfiguration, 1> proto_traits_;
  Common::ConfigTraitsImpl<envoy::config::route::v3::RouteConfiguration, TestConfig, TestConfig>
      config_traits_;
  RouteConfigUpdatePtr config_update_;
};

TEST_F(RdsConfigUpdateReceiverTest, OnRdsUpdate) {
  setup();

  EXPECT_TRUE(config_update_->parsedConfiguration());
  EXPECT_EQ(nullptr, route("foo"));
  EXPECT_FALSE(config_update_->configInfo().has_value());

  const std::string response1_json = R"EOF(
{
  "name": "foo_route_config",
  "virtual_hosts": null
}
)EOF";
  auto response1 =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(response1_json);

  SystemTime time1(std::chrono::milliseconds(1234567891234));
  timeSystem().setSystemTime(time1);

  EXPECT_OK(config_update_->onRdsUpdate(response1, "1"));
  EXPECT_EQ(nullptr, route("foo"));
  EXPECT_TRUE(config_update_->configInfo().has_value());
  EXPECT_EQ("1", config_update_->configInfo().value().version_);
  EXPECT_EQ(time1, config_update_->lastUpdated());

  EXPECT_OK(config_update_->onRdsUpdate(response1, "2"));
  EXPECT_EQ(nullptr, route("foo"));
  EXPECT_EQ("1", config_update_->configInfo().value().version_);

  std::shared_ptr<const Config> config = config_update_->parsedConfiguration();
  EXPECT_EQ(2, config.use_count());

  const std::string response2_json = R"EOF(
{
  "name": "foo_route_config",
  "virtual_hosts": [
    {
      "name": "foo",
      "domains": [
        "*"
      ],
    }
  ]
}
  )EOF";

  auto response2 =
      TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(response2_json);

  SystemTime time2(std::chrono::milliseconds(1234567891235));
  timeSystem().setSystemTime(time2);

  EXPECT_OK(config_update_->onRdsUpdate(response2, "2"));
  EXPECT_EQ("foo", *route("foo"));
  EXPECT_TRUE(config_update_->configInfo().has_value());
  EXPECT_EQ("2", config_update_->configInfo().value().version_);
  EXPECT_EQ(time2, config_update_->lastUpdated());

  EXPECT_EQ(1, config.use_count());
}

class RdsConfigProviderManagerTest : public RdsTestBase {
public:
  RdsConfigProviderManagerTest() : manager_(server_factory_context_.admin_) {}

  RouteConfigProviderSharedPtr createDynamic() {
    envoy::extensions::filters::network::thrift_proxy::v3::Trds rds;
    rds.mutable_config_source()->set_path("dummy");
    rds.set_route_config_name("test_route");
    return manager_.createRdsRouteConfigProvider(rds, server_factory_context_, "test_listener.",
                                                 outer_init_manager_);
  }

  RouteConfigProviderSharedPtr createStatic() {
    envoy::config::route::v3::RouteConfiguration route_config;
    TestUtility::loadFromYaml(R"EOF(
name: foo
virtual_hosts: null
)EOF",
                              route_config);
    return manager_.createStaticRouteConfigProvider(route_config, server_factory_context_,
                                                    init_manager_);
  }

  template <class RouteConfiguration = envoy::config::route::v3::RouteConfiguration>
  void setConfigToDynamicProvider(const std::string& response_json) {
    auto response =
        TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response_json);
    const auto decoded_resources = TestUtility::decodeResources<RouteConfiguration>(response);
    THROW_IF_NOT_OK(
        server_factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
            decoded_resources.refvec_, response.version_info()));
  }

  NiceMock<Init::MockManager> outer_init_manager_;
  Common::RouteConfigProviderManagerImpl<
      envoy::extensions::filters::network::thrift_proxy::v3::Trds,
      envoy::config::route::v3::RouteConfiguration, 1, TestConfig, TestConfig>
      manager_;
};

TEST_F(RdsConfigProviderManagerTest, ProviderAddErase) {
  Matchers::UniversalStringMatcher universal_name_matcher;
  auto config_tracker_callback =
      server_factory_context_.admin_.config_tracker_.config_tracker_callbacks_["trds_routes"];

  timeSystem().setSystemTime(std::chrono::milliseconds(1234567891234));

  auto message_ptr = config_tracker_callback(universal_name_matcher);
  const auto& dump1 =
      TestUtility::downcastAndValidate<const envoy::admin::v3::RoutesConfigDump&>(*message_ptr);
  EXPECT_EQ(0, dump1.dynamic_route_configs().size());
  EXPECT_EQ(0, dump1.static_route_configs().size());

  RouteConfigProviderSharedPtr static_provider = createStatic();
  RouteConfigProviderSharedPtr dynamic_provider = createDynamic();
  setConfigToDynamicProvider<>(R"EOF(
version_info: "1"
resources:
  - "@type": type.googleapis.com/envoy.config.route.v3.RouteConfiguration
    name: test_route
    virtual_hosts: null
)EOF");

  message_ptr = config_tracker_callback(universal_name_matcher);
  const auto& dump2 =
      TestUtility::downcastAndValidate<const envoy::admin::v3::RoutesConfigDump&>(*message_ptr);
  EXPECT_EQ(1, dump2.dynamic_route_configs().size());
  EXPECT_EQ(1, dump2.static_route_configs().size());

  envoy::admin::v3::RoutesConfigDump expected_dump;
  TestUtility::loadFromYaml(R"EOF(
static_route_configs:
  - route_config:
      "@type": type.googleapis.com/envoy.config.route.v3.RouteConfiguration
      name: foo
    last_updated:
      seconds: 1234567891
      nanos: 234000000
dynamic_route_configs:
  - version_info: "1"
    route_config:
      "@type": type.googleapis.com/envoy.config.route.v3.RouteConfiguration
      name: test_route
    last_updated:
      seconds: 1234567891
      nanos: 234000000
)EOF",
                            expected_dump);
  EXPECT_EQ(expected_dump.DebugString(), dump2.DebugString());

  EXPECT_EQ(1UL, scope_.counter("test_listener.trds.test_route.config_reload").value());

  static_provider.reset();
  message_ptr = config_tracker_callback(universal_name_matcher);
  const auto& dump3 =
      TestUtility::downcastAndValidate<const envoy::admin::v3::RoutesConfigDump&>(*message_ptr);
  EXPECT_EQ(1, dump3.dynamic_route_configs().size());
  EXPECT_EQ(0, dump3.static_route_configs().size());

  dynamic_provider.reset();
  message_ptr = config_tracker_callback(universal_name_matcher);
  const auto& dump4 =
      TestUtility::downcastAndValidate<const envoy::admin::v3::RoutesConfigDump&>(*message_ptr);
  EXPECT_EQ(0, dump4.dynamic_route_configs().size());
  EXPECT_EQ(0, dump4.static_route_configs().size());
}

TEST_F(RdsConfigProviderManagerTest, FailureInvalidResourceType) {
  RouteConfigProviderSharedPtr dynamic_provider = createDynamic();

  const std::string wrong_name = R"EOF(
version_info: "1"
resources:
  - "@type": type.googleapis.com/envoy.config.route.v3.RouteConfiguration
    name: not_test_route
    virtual_hosts: null
)EOF";
  EXPECT_THROW_WITH_MESSAGE(setConfigToDynamicProvider<>(wrong_name), EnvoyException,
                            "Unexpected TRDS configuration (expecting test_route): not_test_route");

  const std::string wrong_type = R"EOF(
version_info: "1"
resources:
  - "@type": type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.v3.RouteConfiguration
    name: test_route
    routes: null
)EOF";
  EXPECT_THROW_WITH_MESSAGE(
      setConfigToDynamicProvider<
          envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration>(wrong_type),
      EnvoyException,
      "Unexpected TRDS configuration type (expecting envoy.config.route.v3.RouteConfiguration): "
      "envoy.extensions.filters.network.thrift_proxy.v3.RouteConfiguration");
}

// A route configuration that owns a resource that needs to be warmed up before the route
// configuration can be published.
class WarmingTestConfig : public TestConfig {
public:
  WarmingTestConfig(const envoy::config::route::v3::RouteConfiguration& rc,
                    Server::Configuration::ServerFactoryContext& context,
                    Init::Manager& init_manager, bool ready_synchronously,
                    bool ready_on_destruction = false)
      : TestConfig(rc, context, false), ready_on_destruction_(ready_on_destruction),
        target_(fmt::format("warming target {}", rc.name()), [this, ready_synchronously]() {
          initializing_ = true;
          // The resource is already available, so it signals readiness from within the
          // initialization callback, i.e. before the init manager is done starting its
          // targets up.
          if (ready_synchronously) {
            target_.ready();
          }
        }) {
    init_manager.add(target_);
  }

  ~WarmingTestConfig() override {
    // Some resources give up and signal readiness when they are destroyed rather than leaving
    // whatever waits for them warming forever. The VHDS subscription of a route configuration is
    // one of them.
    if (ready_on_destruction_) {
      target_.ready();
    }
  }

  // Simulates the resource that is owned by this route configuration becoming ready.
  void ready() { target_.ready(); }

  // Whether the init manager that this route configuration was created with has started warming
  // this route configuration up.
  bool initializing_{false};

private:
  const bool ready_on_destruction_;
  Init::TargetImpl target_;
};

// Config traits that create route configurations which own a resource that needs to be warmed up,
// so that the warming of an update can be driven by the test.
class WarmingConfigTraits : public ConfigTraits {
public:
  ConfigConstSharedPtr createNullConfig() const override {
    return std::make_shared<const TestConfig>();
  }

  ConfigConstSharedPtr createConfig(const Protobuf::Message& rc,
                                    Server::Configuration::ServerFactoryContext& context,
                                    Init::Manager& init_manager, bool) const override {
    ASSERT(Envoy::Protobuf::DynamicCastMessage<envoy::config::route::v3::RouteConfiguration>(&rc));
    const auto& route_config = static_cast<const envoy::config::route::v3::RouteConfiguration&>(rc);
    if (!warming_) {
      return std::make_shared<const TestConfig>(route_config, context, false);
    }
    // The created configurations are kept alive so that a test can complete the warming of an
    // update that has already been superseded by a newer one.
    configs_.push_back(std::make_shared<WarmingTestConfig>(
        route_config, context, init_manager, ready_synchronously_, ready_on_destruction_));
    return configs_.back();
  }

  // Whether the created route configurations own a resource that needs to be warmed up.
  bool warming_{true};
  // Whether that resource is ready as soon as it is asked to initialize.
  bool ready_synchronously_{false};
  // Whether that resource signals readiness when it is destroyed.
  bool ready_on_destruction_{false};
  mutable std::vector<std::shared_ptr<WarmingTestConfig>> configs_;
};

// Route config provider that can be told to fail publishing a warmed up route configuration.
class TestRouteConfigProviderImpl : public RdsRouteConfigProviderImpl {
public:
  using RdsRouteConfigProviderImpl::RdsRouteConfigProviderImpl;

  absl::Status onConfigUpdate() override {
    RETURN_IF_NOT_OK(publish_status_);
    return RdsRouteConfigProviderImpl::onConfigUpdate();
  }

  absl::Status publish_status_;
};

class RdsWarmingTest : public RdsTestBase {
public:
  RdsWarmingTest()
      : provider_manager_(server_factory_context_.admin_, "trds_warming_routes", proto_traits_) {}

  void createProvider() {
    envoy::extensions::filters::network::thrift_proxy::v3::Trds rds;
    rds.mutable_config_source()->set_path("dummy");
    rds.set_route_config_name("test_route");
    provider_ = provider_manager_.addDynamicProvider(
        rds, rds.route_config_name(), outer_init_manager_,
        [this, &rds](uint64_t manager_identifier)
            -> absl::StatusOr<std::pair<RouteConfigProviderSharedPtr, const Init::Target*>> {
          auto config_update = std::make_unique<RouteConfigUpdateReceiverImpl>(
              config_traits_, proto_traits_, server_factory_context_);
          auto resource_decoder = std::make_shared<Envoy::Config::OpaqueResourceDecoderImpl<
              envoy::config::route::v3::RouteConfiguration>>(
              server_factory_context_.messageValidationContext().dynamicValidationVisitor(),
              "name");
          auto subscription = THROW_OR_RETURN_VALUE(
              RdsRouteConfigSubscription::create(
                  std::move(config_update), std::move(resource_decoder), rds.config_source(),
                  rds.route_config_name(), manager_identifier, server_factory_context_,
                  "test_listener.trds.", "TRDS", provider_manager_),
              std::unique_ptr<RdsRouteConfigSubscription>);
          auto provider = std::make_shared<TestRouteConfigProviderImpl>(std::move(subscription),
                                                                        server_factory_context_);
          return std::make_pair(provider, &provider->subscription().initTarget());
        });
    // Starts the subscription.
    outer_init_manager_.initialize(init_watcher_);
  }

  absl::Status pushUpdate(const std::string& response_json) {
    auto response =
        TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response_json);
    const auto decoded_resources =
        TestUtility::decodeResources<envoy::config::route::v3::RouteConfiguration>(response);
    return server_factory_context_.cluster_manager_.subscription_factory_.callbacks_
        ->onConfigUpdate(decoded_resources.refvec_, response.version_info());
  }

  // The virtual host of the route configuration that is visible to the workers, if any.
  const std::string* publishedRoute(const std::string& name) {
    return std::static_pointer_cast<const TestConfig>(provider_->config())->route(name);
  }

  TestRouteConfigProviderImpl& provider() {
    return *std::static_pointer_cast<TestRouteConfigProviderImpl>(provider_);
  }

  RouteConfigUpdateReceiver& receiver() { return *provider().subscription().routeConfigUpdate(); }

  uint64_t configReloads() {
    return scope_.counter("test_listener.trds.test_route.config_reload").value();
  }

  static std::string routeConfig(absl::string_view version, absl::string_view vhost) {
    return fmt::format(R"EOF(
version_info: "{}"
resources:
  - "@type": type.googleapis.com/envoy.config.route.v3.RouteConfiguration
    name: test_route
    virtual_hosts:
      - name: {}
        domains: ["*"]
)EOF",
                       version, vhost);
  }

  Common::ProtoTraitsImpl<envoy::config::route::v3::RouteConfiguration, 1> proto_traits_;
  WarmingConfigTraits config_traits_;
  Envoy::Rds::RouteConfigProviderManager provider_manager_;
  Init::ManagerImpl outer_init_manager_{"test outer init manager"};
  Init::ExpectableWatcherImpl init_watcher_;
  RouteConfigProviderSharedPtr provider_;
};

// A route configuration that is still warming up isn't published, and the subscription doesn't
// signal readiness until it is.
TEST_F(RdsWarmingTest, UpdateIsPublishedOnlyAfterItIsWarmedUp) {
  createProvider();

  init_watcher_.expectReady().Times(0);
  EXPECT_TRUE(pushUpdate(routeConfig("1", "foo")).ok());
  ASSERT_EQ(1, config_traits_.configs_.size());
  EXPECT_TRUE(config_traits_.configs_[0]->initializing_);
  // Not visible to the workers yet.
  EXPECT_EQ(nullptr, publishedRoute("foo"));
  EXPECT_EQ(0, configReloads());
  ::testing::Mock::VerifyAndClearExpectations(&init_watcher_);

  init_watcher_.expectReady();
  config_traits_.configs_[0]->ready();
  EXPECT_NE(nullptr, publishedRoute("foo"));
  EXPECT_EQ(1, configReloads());
}

// A route configuration that has nothing to warm up is published synchronously, i.e. from within
// onConfigUpdate().
TEST_F(RdsWarmingTest, UpdateWithNothingToWarmUpIsPublishedSynchronously) {
  config_traits_.warming_ = false;
  createProvider();

  init_watcher_.expectReady();
  EXPECT_TRUE(pushUpdate(routeConfig("1", "foo")).ok());
  EXPECT_NE(nullptr, publishedRoute("foo"));
  EXPECT_EQ(1, configReloads());
}

// A route configuration whose resources are ready as soon as they are asked to initialize is
// published synchronously, from within the init manager that is starting them up.
TEST_F(RdsWarmingTest, UpdateWhoseResourcesAreImmediatelyReadyIsPublishedSynchronously) {
  config_traits_.ready_synchronously_ = true;
  createProvider();

  init_watcher_.expectReady();
  EXPECT_TRUE(pushUpdate(routeConfig("1", "foo")).ok());
  ASSERT_EQ(1, config_traits_.configs_.size());
  EXPECT_TRUE(config_traits_.configs_[0]->initializing_);
  EXPECT_NE(nullptr, publishedRoute("foo"));
  EXPECT_EQ(1, configReloads());

  // A second such update is published as well, i.e. the init manager of the first one is replaced
  // rather than reused.
  EXPECT_TRUE(pushUpdate(routeConfig("2", "bar")).ok());
  ASSERT_EQ(2, config_traits_.configs_.size());
  EXPECT_NE(nullptr, publishedRoute("bar"));
  EXPECT_EQ(2, configReloads());
}

// Nothing about an update that is still warming up is visible until it is published.
TEST_F(RdsWarmingTest, WarmingUpdateIsNotVisibleBeforeItIsPublished) {
  createProvider();

  init_watcher_.expectReady().Times(0);
  EXPECT_TRUE(pushUpdate(routeConfig("1", "foo")).ok());
  ASSERT_EQ(1, config_traits_.configs_.size());
  EXPECT_TRUE(receiver().configWarming());
  // The published route configuration is still the null config, so there is no version yet.
  EXPECT_FALSE(receiver().configInfo().has_value());
  EXPECT_EQ(0, receiver().configHash());
  ::testing::Mock::VerifyAndClearExpectations(&init_watcher_);

  init_watcher_.expectReady();
  config_traits_.configs_[0]->ready();
  EXPECT_FALSE(receiver().configWarming());
  ASSERT_TRUE(receiver().configInfo().has_value());
  EXPECT_EQ("1", receiver().configInfo().value().version_);
  EXPECT_NE(0, receiver().configHash());
}

// An update that arrives while a previous update is still warming up supersedes it. The superseded
// update is never published, even if it finishes warming up.
TEST_F(RdsWarmingTest, UpdateWhileWarmingSupersedesThePreviousUpdate) {
  createProvider();

  init_watcher_.expectReady().Times(0);
  EXPECT_TRUE(pushUpdate(routeConfig("1", "foo")).ok());
  EXPECT_TRUE(pushUpdate(routeConfig("2", "bar")).ok());
  ASSERT_EQ(2, config_traits_.configs_.size());

  // The abandoned update publishes nothing and doesn't signal readiness.
  config_traits_.configs_[0]->ready();
  EXPECT_EQ(nullptr, publishedRoute("foo"));
  EXPECT_EQ(nullptr, publishedRoute("bar"));
  EXPECT_EQ(0, configReloads());
  ::testing::Mock::VerifyAndClearExpectations(&init_watcher_);

  init_watcher_.expectReady();
  config_traits_.configs_[1]->ready();
  EXPECT_EQ(nullptr, publishedRoute("foo"));
  EXPECT_NE(nullptr, publishedRoute("bar"));
  EXPECT_EQ(1, configReloads());
}

// An update that turns out to be a no-op leaves an update that is still warming up alone.
TEST_F(RdsWarmingTest, NoopUpdateWhileWarmingLeavesTheWarmingUpdateAlone) {
  createProvider();

  init_watcher_.expectReady().Times(0);
  EXPECT_TRUE(pushUpdate(routeConfig("1", "foo")).ok());
  // Same route configuration, different version. No new route configuration is built.
  EXPECT_TRUE(pushUpdate(routeConfig("2", "foo")).ok());
  ASSERT_EQ(1, config_traits_.configs_.size());
  ::testing::Mock::VerifyAndClearExpectations(&init_watcher_);

  init_watcher_.expectReady();
  config_traits_.configs_[0]->ready();
  EXPECT_NE(nullptr, publishedRoute("foo"));
  EXPECT_EQ(1, configReloads());
}

// An empty update while an update is still warming up leaves the warming update alone and doesn't
// signal readiness on its behalf.
TEST_F(RdsWarmingTest, EmptyUpdateWhileWarmingLeavesTheWarmingUpdateAlone) {
  createProvider();

  init_watcher_.expectReady().Times(0);
  EXPECT_TRUE(pushUpdate(routeConfig("1", "foo")).ok());
  EXPECT_TRUE(pushUpdate(R"EOF(
version_info: "2"
resources: []
)EOF")
                  .ok());
  EXPECT_EQ(1UL, scope_.counter("test_listener.trds.test_route.update_empty").value());
  ASSERT_EQ(1, config_traits_.configs_.size());
  ::testing::Mock::VerifyAndClearExpectations(&init_watcher_);

  init_watcher_.expectReady();
  config_traits_.configs_[0]->ready();
  EXPECT_NE(nullptr, publishedRoute("foo"));
}

// Destroying everything while an update is still warming up must not publish that update, even
// though the resource it warms up signals readiness as it is destroyed. By then the subscription
// that would publish it is being destroyed itself.
TEST_F(RdsWarmingTest, DestructionWhileWarmingDoesNotPublishTheWarmingUpdate) {
  config_traits_.ready_on_destruction_ = true;
  createProvider();

  init_watcher_.expectReady().Times(0);
  EXPECT_TRUE(pushUpdate(routeConfig("1", "foo")).ok());
  ASSERT_EQ(1, config_traits_.configs_.size());
  EXPECT_TRUE(receiver().configWarming());
  EXPECT_EQ(0, configReloads());
  ::testing::Mock::VerifyAndClearExpectations(&init_watcher_);

  // Hand the only remaining reference to the route configuration over to the receiver, so that the
  // resource that is warming up is destroyed together with it.
  config_traits_.configs_.clear();

  // The subscription signals readiness as it is destroyed, so that nothing keeps warming with it.
  init_watcher_.expectReady();
  provider_.reset();
  EXPECT_EQ(0, configReloads());
}

// A failure to publish a route configuration that was warmed up asynchronously can't be reported to
// the xDS layer anymore, and the subscription stays unready.
TEST_F(RdsWarmingTest, FailureToPublishAWarmedUpUpdate) {
  createProvider();

  init_watcher_.expectReady().Times(0);
  EXPECT_TRUE(pushUpdate(routeConfig("1", "foo")).ok());
  ASSERT_EQ(1, config_traits_.configs_.size());

  provider().publish_status_ = absl::InvalidArgumentError("publishing failed");
  config_traits_.configs_[0]->ready();
  EXPECT_EQ(nullptr, publishedRoute("foo"));

  // The subscription only signals readiness when it is destroyed.
  ::testing::Mock::VerifyAndClearExpectations(&init_watcher_);
  init_watcher_.expectReady();
}

// A failure to publish a route configuration that had nothing to warm up is reported back to the
// xDS layer as a rejection of the update.
TEST_F(RdsWarmingTest, FailureToPublishAnUpdateWithNothingToWarmUp) {
  config_traits_.warming_ = false;
  createProvider();

  init_watcher_.expectReady().Times(0);
  provider().publish_status_ = absl::InvalidArgumentError("publishing failed");
  EXPECT_EQ("publishing failed", pushUpdate(routeConfig("1", "foo")).message());
  EXPECT_EQ(nullptr, publishedRoute("foo"));

  // The subscription only signals readiness when it is destroyed. In production the xDS layer
  // rejects the update and calls onConfigUpdateFailed(), which signals readiness.
  ::testing::Mock::VerifyAndClearExpectations(&init_watcher_);
  init_watcher_.expectReady();
}

} // namespace
} // namespace Rds
} // namespace Envoy
