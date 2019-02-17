#include <memory>

#include "common/config/config_provider_impl.h"
#include "common/protobuf/utility.h"

#include "test/common/config/dummy_config.pb.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

class DummyConfigProviderManager;

class StaticDummyConfigProvider : public ImmutableConfigProviderImplBase {
public:
  StaticDummyConfigProvider(const test::common::config::DummyConfig& config_proto,
                            Server::Configuration::FactoryContext& factory_context,
                            DummyConfigProviderManager& config_provider_manager);

  ~StaticDummyConfigProvider() override = default;

  // Envoy::Config::ConfigProvider
  const Protobuf::Message* getConfigProto() const override { return &config_proto_; }

  // Envoy::Config::ConfigProvider
  std::string getConfigVersion() const override { return ""; }

  // Envoy::Config::ConfigProvider
  ConfigConstSharedPtr getConfig() const override { return config_; }

private:
  ConfigConstSharedPtr config_;
  test::common::config::DummyConfig config_proto_;
};

class DummyConfigSubscription
    : public ConfigSubscriptionInstanceBase,
      Envoy::Config::SubscriptionCallbacks<test::common::config::DummyConfig> {
public:
  DummyConfigSubscription(const uint64_t manager_identifier,
                          Server::Configuration::FactoryContext& factory_context,
                          DummyConfigProviderManager& config_provider_manager);

  ~DummyConfigSubscription() override = default;

  // Envoy::Config::ConfigSubscriptionInstanceBase
  void start() override {}

  // Envoy::Config::SubscriptionCallbacks
  // TODO(fredlas) deduplicate
  void onConfigUpdate(const ResourceVector& resources, const std::string& version_info) override {
    const auto& config = resources[0];
    if (checkAndApplyConfig(config, "dummy_config", version_info)) {
      config_proto_ = config;
    }

    ConfigSubscriptionInstanceBase::onConfigUpdate();
  }
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>&,
                      const Protobuf::RepeatedPtrField<std::string>&, const std::string&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  // Envoy::Config::SubscriptionCallbacks
  void onConfigUpdateFailed(const EnvoyException*) override {}

  // Envoy::Config::SubscriptionCallbacks
  std::string resourceName(const ProtobufWkt::Any&) override { return ""; }

  const absl::optional<test::common::config::DummyConfig>& config_proto() const {
    return config_proto_;
  }

private:
  absl::optional<test::common::config::DummyConfig> config_proto_;
};

using DummyConfigSubscriptionSharedPtr = std::shared_ptr<DummyConfigSubscription>;

class DummyConfig : public ConfigProvider::Config {
public:
  DummyConfig(const test::common::config::DummyConfig&) {}
};

class DummyDynamicConfigProvider : public MutableConfigProviderImplBase {
public:
  DummyDynamicConfigProvider(DummyConfigSubscriptionSharedPtr&& subscription,
                             ConfigConstSharedPtr initial_config,
                             Server::Configuration::FactoryContext& factory_context)
      : MutableConfigProviderImplBase(std::move(subscription), factory_context),
        subscription_(static_cast<DummyConfigSubscription*>(
            MutableConfigProviderImplBase::subscription().get())) {
    initialize(initial_config);
  }

  ~DummyDynamicConfigProvider() override = default;

  DummyConfigSubscription& subscription() { return *subscription_; }

  // Envoy::Config::MutableConfigProviderImplBase
  ConfigProvider::ConfigConstSharedPtr
  onConfigProtoUpdate(const Protobuf::Message& config) override {
    return std::make_shared<DummyConfig>(
        static_cast<const test::common::config::DummyConfig&>(config));
  }

  // Envoy::Config::ConfigProvider
  const Protobuf::Message* getConfigProto() const override {
    if (!subscription_->config_proto().has_value()) {
      return nullptr;
    }
    return &subscription_->config_proto().value();
  }

  // Envoy::Config::ConfigProvider
  std::string getConfigVersion() const override { return ""; }

private:
  // Lifetime of this pointer is owned by the shared_ptr held by the base class.
  DummyConfigSubscription* subscription_;
};

class DummyConfigProviderManager : public ConfigProviderManagerImplBase {
public:
  DummyConfigProviderManager(Server::Admin& admin)
      : ConfigProviderManagerImplBase(admin, "dummy") {}

  ~DummyConfigProviderManager() override = default;

  // Envoy::Config::ConfigProviderManagerImplBase
  ProtobufTypes::MessagePtr dumpConfigs() const override {
    auto config_dump = std::make_unique<test::common::config::DummyConfigsDump>();
    for (const auto& element : configSubscriptions()) {
      auto subscription = element.second.lock();
      ASSERT(subscription);

      if (subscription->configInfo()) {
        auto* dynamic_config = config_dump->mutable_dynamic_dummy_configs()->Add();
        dynamic_config->set_version_info(subscription->configInfo().value().last_config_version_);
        dynamic_config->mutable_dummy_config()->MergeFrom(
            static_cast<DummyConfigSubscription*>(subscription.get())->config_proto().value());
        TimestampUtil::systemClockToTimestamp(subscription->lastUpdated(),
                                              *dynamic_config->mutable_last_updated());
      }
    }

    for (const auto* provider : immutableConfigProviders(ConfigProviderInstanceType::Static)) {
      ASSERT(provider->configProtoInfo<test::common::config::DummyConfig>());
      auto* static_config = config_dump->mutable_static_dummy_configs()->Add();
      static_config->mutable_dummy_config()->MergeFrom(
          provider->configProtoInfo<test::common::config::DummyConfig>().value().config_proto_);
      TimestampUtil::systemClockToTimestamp(provider->lastUpdated(),
                                            *static_config->mutable_last_updated());
    }

    return config_dump;
  }

  // Envoy::Config::ConfigProviderManager
  ConfigProviderPtr createXdsConfigProvider(const Protobuf::Message& config_source_proto,
                                            Server::Configuration::FactoryContext& factory_context,
                                            const std::string&) override {
    DummyConfigSubscriptionSharedPtr subscription = getSubscription<DummyConfigSubscription>(
        config_source_proto, factory_context.initManager(),
        [&factory_context](const uint64_t manager_identifier,
                           ConfigProviderManagerImplBase& config_provider_manager)
            -> ConfigSubscriptionInstanceBaseSharedPtr {
          return std::make_shared<DummyConfigSubscription>(
              manager_identifier, factory_context,
              static_cast<DummyConfigProviderManager&>(config_provider_manager));
        });

    ConfigProvider::ConfigConstSharedPtr initial_config;
    const MutableConfigProviderImplBase* provider =
        subscription->getAnyBoundMutableConfigProvider();
    if (provider) {
      initial_config = provider->getConfig();
    }
    return std::make_unique<DummyDynamicConfigProvider>(std::move(subscription), initial_config,
                                                        factory_context);
  }

  // Envoy::Config::ConfigProviderManager
  ConfigProviderPtr
  createStaticConfigProvider(const Protobuf::Message& config_proto,
                             Server::Configuration::FactoryContext& factory_context) override {
    return std::make_unique<StaticDummyConfigProvider>(
        dynamic_cast<const test::common::config::DummyConfig&>(config_proto), factory_context,
        *this);
  }
};

StaticDummyConfigProvider::StaticDummyConfigProvider(
    const test::common::config::DummyConfig& config_proto,
    Server::Configuration::FactoryContext& factory_context,
    DummyConfigProviderManager& config_provider_manager)
    : ImmutableConfigProviderImplBase(factory_context, config_provider_manager,
                                      ConfigProviderInstanceType::Static),
      config_(std::make_shared<DummyConfig>(config_proto)), config_proto_(config_proto) {}

DummyConfigSubscription::DummyConfigSubscription(
    const uint64_t manager_identifier, Server::Configuration::FactoryContext& factory_context,
    DummyConfigProviderManager& config_provider_manager)
    : ConfigSubscriptionInstanceBase(
          "DummyDS", manager_identifier, config_provider_manager, factory_context.timeSource(),
          factory_context.timeSource().systemTime(), factory_context.localInfo()) {}

class ConfigProviderImplTest : public testing::Test {
public:
  ConfigProviderImplTest() {
    EXPECT_CALL(factory_context_.admin_.config_tracker_, add_("dummy", _));
    provider_manager_ = std::make_unique<DummyConfigProviderManager>(factory_context_.admin_);
  }

  Event::SimulatedTimeSystem& timeSystem() { return time_system_; }

protected:
  Event::SimulatedTimeSystem time_system_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  std::unique_ptr<DummyConfigProviderManager> provider_manager_;
};

test::common::config::DummyConfig parseDummyConfigFromYaml(const std::string& yaml) {
  test::common::config::DummyConfig config;
  MessageUtil::loadFromYaml(yaml, config);
  return config;
}

// Tests that dynamic config providers share ownership of the config
// subscriptions, config protos and data structures generated as a result of the
// configurations (i.e., the ConfigProvider::Config).
TEST_F(ConfigProviderImplTest, SharedOwnership) {
  factory_context_.init_manager_.initialize();

  envoy::api::v2::core::ApiConfigSource config_source_proto;
  config_source_proto.set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
  ConfigProviderPtr provider1 = provider_manager_->createXdsConfigProvider(
      config_source_proto, factory_context_, "dummy_prefix");

  // No config protos have been received via the subscription yet.
  EXPECT_FALSE(provider1->configProtoInfo<test::common::config::DummyConfig>().has_value());

  Protobuf::RepeatedPtrField<test::common::config::DummyConfig> dummy_configs;
  dummy_configs.Add()->MergeFrom(parseDummyConfigFromYaml("a: a dummy config"));

  DummyConfigSubscription& subscription =
      dynamic_cast<DummyDynamicConfigProvider&>(*provider1).subscription();
  subscription.onConfigUpdate(dummy_configs, "1");

  // Check that a newly created provider with the same config source will share
  // the subscription, config proto and resulting ConfigProvider::Config.
  ConfigProviderPtr provider2 = provider_manager_->createXdsConfigProvider(
      config_source_proto, factory_context_, "dummy_prefix");

  EXPECT_TRUE(provider2->configProtoInfo<test::common::config::DummyConfig>().has_value());
  EXPECT_EQ(&dynamic_cast<DummyDynamicConfigProvider&>(*provider1).subscription(),
            &dynamic_cast<DummyDynamicConfigProvider&>(*provider2).subscription());
  EXPECT_EQ(&provider1->configProtoInfo<test::common::config::DummyConfig>().value().config_proto_,
            &provider2->configProtoInfo<test::common::config::DummyConfig>().value().config_proto_);
  EXPECT_EQ(provider1->config<const DummyConfig>().get(),
            provider2->config<const DummyConfig>().get());

  // Change the config source and verify that a new subscription is used.
  config_source_proto.set_api_type(envoy::api::v2::core::ApiConfigSource::REST);
  ConfigProviderPtr provider3 = provider_manager_->createXdsConfigProvider(
      config_source_proto, factory_context_, "dummy_prefix");

  EXPECT_NE(&dynamic_cast<DummyDynamicConfigProvider&>(*provider1).subscription(),
            &dynamic_cast<DummyDynamicConfigProvider&>(*provider3).subscription());
  EXPECT_NE(provider1->config<const DummyConfig>().get(),
            provider3->config<const DummyConfig>().get());

  dynamic_cast<DummyDynamicConfigProvider&>(*provider3)
      .subscription()
      .onConfigUpdate(dummy_configs, "provider3");

  EXPECT_EQ(2UL, static_cast<test::common::config::DummyConfigsDump*>(
                     provider_manager_->dumpConfigs().get())
                     ->dynamic_dummy_configs()
                     .size());

  // Test that tear down of config providers leads to correctly updating
  // centralized state; this is validated using the config dump.
  provider1.reset();
  provider2.reset();

  auto dynamic_dummy_configs =
      static_cast<test::common::config::DummyConfigsDump*>(provider_manager_->dumpConfigs().get())
          ->dynamic_dummy_configs();
  EXPECT_EQ(1UL, dynamic_dummy_configs.size());

  EXPECT_EQ("provider3", dynamic_dummy_configs[0].version_info());

  provider3.reset();

  EXPECT_EQ(0UL, static_cast<test::common::config::DummyConfigsDump*>(
                     provider_manager_->dumpConfigs().get())
                     ->dynamic_dummy_configs()
                     .size());
}

// Tests that the base ConfigProvider*s are handling registration with the
// /config_dump admin handler as well as generic bookkeeping such as timestamp
// updates.
TEST_F(ConfigProviderImplTest, ConfigDump) {
  // Empty dump first.
  auto message_ptr = factory_context_.admin_.config_tracker_.config_tracker_callbacks_["dummy"]();
  const auto& dummy_config_dump =
      static_cast<const test::common::config::DummyConfigsDump&>(*message_ptr);

  test::common::config::DummyConfigsDump expected_config_dump;
  MessageUtil::loadFromYaml(R"EOF(
static_dummy_configs:
dynamic_dummy_configs:
)EOF",
                            expected_config_dump);
  EXPECT_EQ(expected_config_dump.DebugString(), dummy_config_dump.DebugString());

  // Static config dump only.
  std::string config_yaml = "a: a static dummy config";
  timeSystem().setSystemTime(std::chrono::milliseconds(1234567891234));

  ConfigProviderPtr static_config = provider_manager_->createStaticConfigProvider(
      parseDummyConfigFromYaml(config_yaml), factory_context_);
  message_ptr = factory_context_.admin_.config_tracker_.config_tracker_callbacks_["dummy"]();
  const auto& dummy_config_dump2 =
      static_cast<const test::common::config::DummyConfigsDump&>(*message_ptr);
  MessageUtil::loadFromYaml(R"EOF(
static_dummy_configs:
  - dummy_config: { a: a static dummy config }
    last_updated: { seconds: 1234567891, nanos: 234000000 }
dynamic_dummy_configs:
)EOF",
                            expected_config_dump);
  EXPECT_EQ(expected_config_dump.DebugString(), dummy_config_dump2.DebugString());

  envoy::api::v2::core::ApiConfigSource config_source_proto;
  config_source_proto.set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
  ConfigProviderPtr dynamic_provider = provider_manager_->createXdsConfigProvider(
      config_source_proto, factory_context_, "dummy_prefix");

  // Static + dynamic config dump.
  Protobuf::RepeatedPtrField<test::common::config::DummyConfig> dummy_configs;
  dummy_configs.Add()->MergeFrom(parseDummyConfigFromYaml("a: a dynamic dummy config"));

  timeSystem().setSystemTime(std::chrono::milliseconds(1234567891567));
  DummyConfigSubscription& subscription =
      dynamic_cast<DummyDynamicConfigProvider&>(*dynamic_provider).subscription();
  subscription.onConfigUpdate(dummy_configs, "v1");

  message_ptr = factory_context_.admin_.config_tracker_.config_tracker_callbacks_["dummy"]();
  const auto& dummy_config_dump3 =
      static_cast<const test::common::config::DummyConfigsDump&>(*message_ptr);
  MessageUtil::loadFromYaml(R"EOF(
static_dummy_configs:
  - dummy_config: { a: a static dummy config }
    last_updated: { seconds: 1234567891, nanos: 234000000 }
dynamic_dummy_configs:
  - version_info: v1
    dummy_config: { a: a dynamic dummy config }
    last_updated: { seconds: 1234567891, nanos: 567000000 }
)EOF",
                            expected_config_dump);
  EXPECT_EQ(expected_config_dump.DebugString(), dummy_config_dump3.DebugString());
}

// Tests that dynamic config providers enforce that the context's localInfo is
// set, since it is used to obtain the node/cluster attributes required for
// subscriptions.
TEST_F(ConfigProviderImplTest, LocalInfoNotDefined) {
  factory_context_.local_info_.node_.set_cluster("");
  factory_context_.local_info_.node_.set_id("");

  envoy::api::v2::core::ApiConfigSource config_source_proto;
  config_source_proto.set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
  EXPECT_THROW_WITH_MESSAGE(
      provider_manager_->createXdsConfigProvider(config_source_proto, factory_context_,
                                                 "dummy_prefix"),
      EnvoyException,
      "DummyDS: node 'id' and 'cluster' are required. Set it either in 'node' config or "
      "via --service-node and --service-cluster options.");
}

} // namespace
} // namespace Config
} // namespace Envoy
