#include <chrono>
#include <memory>
#include <string>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"
#include "envoy/stats/scope.h"

#include "source/common/config/opaque_resource_decoder_impl.h"
#include "source/common/rds/rds_route_config_provider_impl.h"
#include "source/common/rds/rds_route_config_subscription.h"
#include "source/common/rds/route_config_provider_manager.h"
#include "source/common/rds/route_config_update_receiver_impl.h"
#include "source/common/rds/static_route_config_provider_impl.h"

#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {
namespace Rds {
namespace {

class RdsTestBase : public testing::Test {
public:
  RdsTestBase() {
    ON_CALL(server_factory_context_, scope()).WillByDefault(ReturnRef(scope_));
    ON_CALL(server_factory_context_, messageValidationContext())
        .WillByDefault(ReturnRef(validation_context_));
  }

  ~RdsTestBase() override { server_factory_context_.thread_local_.shutdownThread(); }

  Event::SimulatedTimeSystem& timeSystem() { return time_system_; }

  Event::SimulatedTimeSystem time_system_;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
};

class TestConfig : public Config {
public:
  TestConfig() = default;
  TestConfig(const envoy::config::route::v3::RouteConfiguration& rc) : rc_(rc) {}
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

class TestTraits : public ConfigTraits, public ProtoTraits {
public:
  TestTraits() {
    resource_type_ = Envoy::Config::getResourceName<envoy::config::route::v3::RouteConfiguration>();
  }

  const std::string& resourceType() const override { return resource_type_; }
  int resourceNameFieldNumber() const override { return resource_name_field_index_; }
  ConfigConstSharedPtr createNullConfig() const override {
    return std::make_shared<const TestConfig>();
  }
  ProtobufTypes::MessagePtr createEmptyProto() const override {
    return std::make_unique<envoy::config::route::v3::RouteConfiguration>();
  }
  ConfigConstSharedPtr createConfig(const Protobuf::Message& rc) const override {
    return std::make_shared<const TestConfig>(
        dynamic_cast<const envoy::config::route::v3::RouteConfiguration&>(rc));
  }

  std::string resource_type_;
  int resource_name_field_index_ = 1;
};

class RdsConfigUpdateReceiverTest : public RdsTestBase {
public:
  void setup() {
    config_update_ =
        std::make_unique<RouteConfigUpdateReceiverImpl>(traits_, traits_, server_factory_context_);
  }

  const std::string* route(const std::string& path) {
    return std::static_pointer_cast<const TestConfig>(config_update_->parsedConfiguration())
        ->route(path);
  }

  TestTraits traits_;
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

  EXPECT_TRUE(config_update_->onRdsUpdate(response1, "1"));
  EXPECT_EQ(nullptr, route("foo"));
  EXPECT_TRUE(config_update_->configInfo().has_value());
  EXPECT_EQ("1", config_update_->configInfo().value().version_);
  EXPECT_EQ(time1, config_update_->lastUpdated());

  EXPECT_FALSE(config_update_->onRdsUpdate(response1, "2"));
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

  EXPECT_TRUE(config_update_->onRdsUpdate(response2, "2"));
  EXPECT_EQ("foo", *route("foo"));
  EXPECT_TRUE(config_update_->configInfo().has_value());
  EXPECT_EQ("2", config_update_->configInfo().value().version_);
  EXPECT_EQ(time2, config_update_->lastUpdated());

  EXPECT_EQ(1, config.use_count());
}

class RdsConfigProviderManagerTest : public RdsTestBase {
public:
  RdsConfigProviderManagerTest() : manager_(server_factory_context_.admin_, "test", traits_) {}

  RouteConfigProviderSharedPtr createDynamic() {
    envoy::config::core::v3::ConfigSource config_source;
    config_source.set_path("test_path");
    return manager_.addDynamicProvider(
        config_source, "test_route", outer_init_manager_,
        [&config_source, this](uint64_t manager_identifier) {
          auto config_update = std::make_unique<RouteConfigUpdateReceiverImpl>(
              traits_, traits_, server_factory_context_);
          auto resource_decoder = std::make_unique<Envoy::Config::OpaqueResourceDecoderImpl<
              envoy::config::route::v3::RouteConfiguration>>(
              server_factory_context_.messageValidationContext().dynamicValidationVisitor(),
              "name");
          auto subscription = std::make_shared<RdsRouteConfigSubscription>(
              std::move(config_update), std::move(resource_decoder), config_source,
              route_config_name_, manager_identifier, server_factory_context_, "test_stat",
              rds_type_, manager_);
          auto provider = std::make_shared<RdsRouteConfigProviderImpl>(std::move(subscription),
                                                                       server_factory_context_);
          return std::make_pair(provider, &provider->subscription().initTarget());
        });
  }

  RouteConfigProviderSharedPtr createStatic() {
    return manager_.addStaticProvider([this]() {
      envoy::config::route::v3::RouteConfiguration route_config;
      return std::make_unique<StaticRouteConfigProviderImpl>(route_config, traits_,
                                                             server_factory_context_, manager_);
    });
  }

  void setConfigToDynamicProvider() {
    const std::string response_json = R"EOF(
{
  "version_info": "1",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.config.route.v3.RouteConfiguration",
      "name": "test_route",
      "virtual_hosts": null
    }
  ]
}
)EOF";
    auto response =
        TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response_json);
    const auto decoded_resources =
        TestUtility::decodeResources<envoy::config::route::v3::RouteConfiguration>(response);
    server_factory_context_.cluster_manager_.subscription_factory_.callbacks_->onConfigUpdate(
        decoded_resources.refvec_, response.version_info());
  }

  NiceMock<Init::MockManager> outer_init_manager_;
  TestTraits traits_;
  RouteConfigProviderManager manager_;
  const std::string rds_type_ = "TestRDS";
  const std::string route_config_name_ = "test_route";
};

TEST_F(RdsConfigProviderManagerTest, ProviderErase) {
  Matchers::UniversalStringMatcher universal_name_matcher;

  auto dump = manager_.dumpRouteConfigs(universal_name_matcher);
  EXPECT_EQ(0, dump->dynamic_route_configs().size());
  EXPECT_EQ(0, dump->static_route_configs().size());

  RouteConfigProviderSharedPtr static_provider = createStatic();
  RouteConfigProviderSharedPtr dynamic_provider = createDynamic();
  setConfigToDynamicProvider();

  dump = manager_.dumpRouteConfigs(universal_name_matcher);
  EXPECT_EQ(1, dump->dynamic_route_configs().size());
  EXPECT_EQ(1, dump->static_route_configs().size());

  static_provider.reset();
  dump = manager_.dumpRouteConfigs(universal_name_matcher);
  EXPECT_EQ(1, dump->dynamic_route_configs().size());
  EXPECT_EQ(0, dump->static_route_configs().size());

  dynamic_provider.reset();
  dump = manager_.dumpRouteConfigs(universal_name_matcher);
  EXPECT_EQ(0, dump->dynamic_route_configs().size());
  EXPECT_EQ(0, dump->static_route_configs().size());
}

TEST_F(RdsConfigProviderManagerTest, FailureInvalidResourceType) {
  RouteConfigProviderSharedPtr dynamic_provider = createDynamic();

  traits_.resource_name_field_index_ = 0;
  EXPECT_THROW_WITH_MESSAGE(setConfigToDynamicProvider(), EnvoyException,
                            "Unexpected " + rds_type_ + " configuration (expecting " +
                                route_config_name_ + "): ");

  traits_.resource_type_ = "EXPECTED_resource_type";
  EXPECT_THROW_WITH_MESSAGE(setConfigToDynamicProvider(), EnvoyException,
                            "Unexpected " + rds_type_ + " configuration type (expecting " +
                                traits_.resource_type_ +
                                "): envoy.config.route.v3.RouteConfiguration");
}

} // namespace
} // namespace Rds
} // namespace Envoy
