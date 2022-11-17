#include <chrono>
#include <memory>
#include <string>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/config/utility.h"
#include "source/common/filter/config_discovery_impl.h"
#include "source/common/json/json_loader.h"
#include "source/common/network/filter_matcher.h"

#include "test/integration/filters/add_body_filter.pb.h"
#include "test/integration/filters/test_listener_filter.h"
#include "test/integration/filters/test_listener_filter.pb.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Filter {
namespace {

class FilterConfigDiscoveryTestBase {
public:
  FilterConfigDiscoveryTestBase() {
    // For server_factory_context
    ON_CALL(factory_context_.server_factory_context_, scope()).WillByDefault(ReturnRef(scope_));
    ON_CALL(factory_context_, messageValidationContext())
        .WillByDefault(ReturnRef(validation_context_));
    ON_CALL(factory_context_.server_factory_context_, messageValidationContext())
        .WillByDefault(ReturnRef(validation_context_));
    ON_CALL(validation_context_, dynamicValidationVisitor())
        .WillByDefault(ReturnRef(validation_visitor_));
    ON_CALL(factory_context_, initManager()).WillByDefault(ReturnRef(init_manager_));
    ON_CALL(init_manager_, add(_)).WillByDefault(Invoke([this](const Init::Target& target) {
      init_target_handle_ = target.createHandle("test");
    }));
    ON_CALL(init_manager_, initialize(_))
        .WillByDefault(Invoke(
            [this](const Init::Watcher& watcher) { init_target_handle_->initialize(watcher); }));
    // Thread local storage assumes a single (main) thread with no workers.
    ON_CALL(factory_context_.admin_, concurrency()).WillByDefault(Return(0));

    ON_CALL(factory_context_, listenerConfig()).WillByDefault(ReturnRef(listener_config_));
  }

  virtual ~FilterConfigDiscoveryTestBase() = default;
  Event::SimulatedTimeSystem& timeSystem() { return time_system_; }

  Event::SimulatedTimeSystem time_system_;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  NiceMock<Init::MockManager> init_manager_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  Init::ExpectableWatcherImpl init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
  NiceMock<Network::MockListenerConfig> listener_config_;
};

// Common base ECDS test class for HTTP filter, and TCP/UDP listener filter.
template <class FactoryCb, class FactoryCtx, class CfgProviderMgrImpl, class ProtoType>
class FilterConfigDiscoveryImplTest : public FilterConfigDiscoveryTestBase {
public:
  FilterConfigDiscoveryImplTest() {
    filter_config_provider_manager_ = std::make_unique<CfgProviderMgrImpl>();
  }
  ~FilterConfigDiscoveryImplTest() override { factory_context_.thread_local_.shutdownThread(); }

  // Create listener filter config provider callbacks.
  DynamicFilterConfigProviderPtr<FactoryCb> createProvider(std::string name, bool warm,
                                                           bool default_configuration,
                                                           bool last_filter_config = true) {
    EXPECT_CALL(init_manager_, add(_));
    envoy::config::core::v3::ExtensionConfigSource config_source;
    envoy::config::core::v3::AggregatedConfigSource ads;
    config_source.mutable_config_source()->mutable_ads()->MergeFrom(ads);
    config_source.add_type_urls(getTypeUrl());
    config_source.set_apply_default_config_without_warming(!warm);
    if (default_configuration || !warm) {
      ProtoType default_config;
      config_source.mutable_default_config()->PackFrom(default_config);
    }

    return filter_config_provider_manager_->createDynamicFilterConfigProvider(
        config_source, name, factory_context_.getServerFactoryContext(), factory_context_,
        last_filter_config, getFilterType(), getMatcher());
  }

  void setup(bool warm = true, bool default_configuration = false, bool last_filter_config = true) {
    provider_ = createProvider("foo", warm, default_configuration, last_filter_config);
    callbacks_ =
        factory_context_.server_factory_context_.cluster_manager_.subscription_factory_.callbacks_;
    EXPECT_CALL(*factory_context_.server_factory_context_.cluster_manager_.subscription_factory_
                     .subscription_,
                start(_));
    if (!warm) {
      EXPECT_CALL(init_watcher_, ready());
    }
    init_manager_.initialize(init_watcher_);
  }

  // Create a discovery response.
  envoy::service::discovery::v3::DiscoveryResponse createResponse(std::string version,
                                                                  std::string name) {
    envoy::service::discovery::v3::DiscoveryResponse response;
    response.set_version_info(version);
    envoy::config::core::v3::TypedExtensionConfig extension_config;
    extension_config.set_name(name);
    extension_config.mutable_typed_config()->set_type_url("type.googleapis.com/" + getTypeUrl());
    response.add_resources()->PackFrom(extension_config);
    return response;
  }

  // Add a filter configuration by send a extension discovery response. Then removes it.
  void incrementalTest() {
    const auto response = createResponse("1", "foo");
    const auto decoded_resources =
        TestUtility::decodeResources<envoy::config::core::v3::TypedExtensionConfig>(response);

    EXPECT_CALL(init_watcher_, ready());
    callbacks_->onConfigUpdate(decoded_resources.refvec_, response.version_info());
    EXPECT_NE(absl::nullopt, provider_->config());
    EXPECT_EQ(1UL, scope_.counter(getConfigReloadCounter()).value());
    EXPECT_EQ(0UL, scope_.counter(getConfigFailCounter()).value());

    // Ensure that we honor resource removals.
    Protobuf::RepeatedPtrField<std::string> remove;
    *remove.Add() = "foo";
    callbacks_->onConfigUpdate({}, remove, "1");
    EXPECT_EQ(2UL, scope_.counter(getConfigReloadCounter()).value());
    EXPECT_EQ(0UL, scope_.counter(getConfigFailCounter()).value());
  }

  const std::string getTypeUrl() const {
    ProtoType proto;
    return proto.GetDescriptor()->full_name();
  }

  virtual const std::string getFilterType() const PURE;
  virtual const Network::ListenerFilterMatcherSharedPtr getMatcher() const { return nullptr; }
  virtual const std::string getConfigReloadCounter() const PURE;
  virtual const std::string getConfigFailCounter() const PURE;

  std::unique_ptr<FilterConfigProviderManager<FactoryCb, FactoryCtx>>
      filter_config_provider_manager_;
  DynamicFilterConfigProviderPtr<FactoryCb> provider_;
  Config::SubscriptionCallbacks* callbacks_{};
};

// HTTP filter test
class HttpFilterConfigDiscoveryImplTest
    : public FilterConfigDiscoveryImplTest<NamedHttpFilterFactoryCb,
                                           Server::Configuration::FactoryContext,
                                           HttpFilterConfigProviderManagerImpl,
                                           envoy::extensions::filters::http::router::v3::Router> {
public:
  const std::string getFilterType() const override { return "http"; }
  const std::string getConfigReloadCounter() const override {
    return "extension_config_discovery.http_filter.foo.config_reload";
  }
  const std::string getConfigFailCounter() const override {
    return "extension_config_discovery.http_filter.foo.config_fail";
  }
};

// TCP listener filter test
class TcpListenerFilterConfigDiscoveryImplTest
    : public FilterConfigDiscoveryImplTest<
          Network::ListenerFilterFactoryCb, Server::Configuration::ListenerFactoryContext,
          TcpListenerFilterConfigProviderManagerImpl,
          ::test::integration::filters::TestInspectorFilterConfig> {
public:
  const std::string getFilterType() const override { return "listener"; }
  const std::string getConfigReloadCounter() const override {
    return "extension_config_discovery.tcp_listener_filter.foo.config_reload";
  }
  const std::string getConfigFailCounter() const override {
    return "extension_config_discovery.tcp_listener_filter.foo.config_fail";
  }
};

// UDP listener filter test
class UdpListenerFilterConfigDiscoveryImplTest
    : public FilterConfigDiscoveryImplTest<
          Network::UdpListenerFilterFactoryCb, Server::Configuration::ListenerFactoryContext,
          UdpListenerFilterConfigProviderManagerImpl,
          test::integration::filters::TestUdpListenerFilterConfig> {
public:
  const std::string getFilterType() const override { return "listener"; }
  const std::string getConfigReloadCounter() const override {
    return "extension_config_discovery.udp_listener_filter.foo.config_reload";
  }
  const std::string getConfigFailCounter() const override {
    return "extension_config_discovery.udp_listener_filter.foo.config_fail";
  }
};

/***************************************************************************************
 *                  Parameterized test for                                             *
 *     HTTP filter,  TCP listener filter And UDP listener filter                       *
 *                                                                                     *
 ***************************************************************************************/
template <typename FilterConfigDiscoveryTestType>
class FilterConfigDiscoveryImplTestParameter : public testing::Test {};

// The test filter types.
using FilterConfigDiscoveryTestTypes =
    ::testing::Types<HttpFilterConfigDiscoveryImplTest, TcpListenerFilterConfigDiscoveryImplTest,
                     UdpListenerFilterConfigDiscoveryImplTest>;

TYPED_TEST_SUITE(FilterConfigDiscoveryImplTestParameter, FilterConfigDiscoveryTestTypes);

// TYPED_TEST will run the same test for each of the above filter type.
TYPED_TEST(FilterConfigDiscoveryImplTestParameter, DestroyReady) {
  TypeParam config_discovery_test;
  config_discovery_test.setup();
  EXPECT_CALL(config_discovery_test.init_watcher_, ready());
}

TYPED_TEST(FilterConfigDiscoveryImplTestParameter, Basic) {
  InSequence s;
  TypeParam config_discovery_test;
  config_discovery_test.setup();
  EXPECT_EQ("foo", config_discovery_test.provider_->name());
  EXPECT_EQ(absl::nullopt, config_discovery_test.provider_->config());

  // Initial request.
  {
    const auto response = config_discovery_test.createResponse("1", "foo");
    const auto decoded_resources =
        TestUtility::decodeResources<envoy::config::core::v3::TypedExtensionConfig>(response);

    EXPECT_CALL(config_discovery_test.init_watcher_, ready());
    config_discovery_test.callbacks_->onConfigUpdate(decoded_resources.refvec_,
                                                     response.version_info());
    EXPECT_NE(absl::nullopt, config_discovery_test.provider_->config());
    EXPECT_EQ(1UL,
              config_discovery_test.scope_.counter(config_discovery_test.getConfigReloadCounter())
                  .value());
    EXPECT_EQ(
        0UL,
        config_discovery_test.scope_.counter(config_discovery_test.getConfigFailCounter()).value());
  }

  // 2nd request with same response. Based on hash should not reload config.
  {
    const auto response = config_discovery_test.createResponse("2", "foo");
    const auto decoded_resources =
        TestUtility::decodeResources<envoy::config::core::v3::TypedExtensionConfig>(response);
    config_discovery_test.callbacks_->onConfigUpdate(decoded_resources.refvec_,
                                                     response.version_info());

    EXPECT_EQ(1UL,
              config_discovery_test.scope_.counter(config_discovery_test.getConfigReloadCounter())
                  .value());
    EXPECT_EQ(
        0UL,
        config_discovery_test.scope_.counter(config_discovery_test.getConfigFailCounter()).value());
  }
}

TYPED_TEST(FilterConfigDiscoveryImplTestParameter, ConfigFailed) {
  InSequence s;
  TypeParam config_discovery_test;
  config_discovery_test.setup();
  EXPECT_CALL(config_discovery_test.init_watcher_, ready());
  config_discovery_test.callbacks_->onConfigUpdateFailed(
      Config::ConfigUpdateFailureReason::FetchTimedout, {});
  EXPECT_EQ(
      0UL,
      config_discovery_test.scope_.counter(config_discovery_test.getConfigReloadCounter()).value());
  EXPECT_EQ(
      1UL,
      config_discovery_test.scope_.counter(config_discovery_test.getConfigFailCounter()).value());
}

TYPED_TEST(FilterConfigDiscoveryImplTestParameter, TooManyResources) {
  InSequence s;
  TypeParam config_discovery_test;
  config_discovery_test.setup();
  auto response = config_discovery_test.createResponse("1", "foo");
  envoy::config::core::v3::TypedExtensionConfig extension_config;
  extension_config.set_name("foo");
  extension_config.mutable_typed_config()->set_type_url("type.googleapis.com/" +
                                                        config_discovery_test.getTypeUrl());
  response.add_resources()->PackFrom(extension_config);
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::core::v3::TypedExtensionConfig>(response);
  EXPECT_CALL(config_discovery_test.init_watcher_, ready());
  EXPECT_THROW_WITH_MESSAGE(config_discovery_test.callbacks_->onConfigUpdate(
                                decoded_resources.refvec_, response.version_info()),
                            EnvoyException,
                            "Unexpected number of resources in ExtensionConfigDS response: 2");
  EXPECT_EQ(
      0UL,
      config_discovery_test.scope_.counter(config_discovery_test.getConfigReloadCounter()).value());
}

TYPED_TEST(FilterConfigDiscoveryImplTestParameter, WrongName) {
  InSequence s;
  TypeParam config_discovery_test;
  config_discovery_test.setup();
  const auto response = config_discovery_test.createResponse("1", "bar");
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::core::v3::TypedExtensionConfig>(response);
  EXPECT_CALL(config_discovery_test.init_watcher_, ready());
  EXPECT_THROW_WITH_MESSAGE(config_discovery_test.callbacks_->onConfigUpdate(
                                decoded_resources.refvec_, response.version_info()),
                            EnvoyException,
                            "Unexpected resource name in ExtensionConfigDS response: bar");
  EXPECT_EQ(
      0UL,
      config_discovery_test.scope_.counter(config_discovery_test.getConfigReloadCounter()).value());
}

// Without default config.
// First adding a filter configuration by send a extension discovery response, then removes it.
TYPED_TEST(FilterConfigDiscoveryImplTestParameter, IncrementalWithOutDefault) {
  InSequence s;
  TypeParam config_discovery_test;
  config_discovery_test.setup();
  config_discovery_test.incrementalTest();
  // Verify the provider config is empty.
  EXPECT_EQ(absl::nullopt, config_discovery_test.provider_->config());
}

// With default config.
// First adding a filter configuration by send a extension discovery response, then removes it.
TYPED_TEST(FilterConfigDiscoveryImplTestParameter, IncrementalWithDefault) {
  InSequence s;
  TypeParam config_discovery_test;
  config_discovery_test.setup(true, true);
  config_discovery_test.incrementalTest();
  // Verify the provider config is not empty since the default config is there.
  EXPECT_NE(absl::nullopt, config_discovery_test.provider_->config());
}

TYPED_TEST(FilterConfigDiscoveryImplTestParameter, ApplyWithoutWarming) {
  InSequence s;
  TypeParam config_discovery_test;
  config_discovery_test.setup(false);
  EXPECT_EQ("foo", config_discovery_test.provider_->name());
  EXPECT_NE(absl::nullopt, config_discovery_test.provider_->config());
  EXPECT_EQ(
      0UL,
      config_discovery_test.scope_.counter(config_discovery_test.getConfigReloadCounter()).value());
  EXPECT_EQ(
      0UL,
      config_discovery_test.scope_.counter(config_discovery_test.getConfigFailCounter()).value());
}

TYPED_TEST(FilterConfigDiscoveryImplTestParameter, DualProviders) {
  InSequence s;
  TypeParam config_discovery_test;
  config_discovery_test.setup();
  const auto provider2 = config_discovery_test.createProvider("foo", true, false);
  EXPECT_EQ("foo", provider2->name());
  EXPECT_EQ(absl::nullopt, provider2->config());
  const auto response = config_discovery_test.createResponse("1", "foo");
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::core::v3::TypedExtensionConfig>(response);
  EXPECT_CALL(config_discovery_test.init_watcher_, ready());
  config_discovery_test.callbacks_->onConfigUpdate(decoded_resources.refvec_,
                                                   response.version_info());
  EXPECT_NE(absl::nullopt, config_discovery_test.provider_->config());
  EXPECT_NE(absl::nullopt, provider2->config());
  EXPECT_EQ(
      1UL,
      config_discovery_test.scope_.counter(config_discovery_test.getConfigReloadCounter()).value());
}

TYPED_TEST(FilterConfigDiscoveryImplTestParameter, DualProvidersInvalid) {
  InSequence s;
  TypeParam config_discovery_test;
  config_discovery_test.setup();
  const auto provider2 = config_discovery_test.createProvider("foo", true, false);

  // Create a response with a random type AddBodyFilterConfig not matching with providers.
  auto add_body_filter_config = test::integration::filters::AddBodyFilterConfig();
  add_body_filter_config.set_body_size(10);
  envoy::config::core::v3::TypedExtensionConfig extension_config;
  extension_config.set_name("foo");
  extension_config.mutable_typed_config()->PackFrom(add_body_filter_config);
  envoy::service::discovery::v3::DiscoveryResponse response;
  response.set_version_info("1");
  response.add_resources()->PackFrom(extension_config);

  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::core::v3::TypedExtensionConfig>(response);
  EXPECT_CALL(config_discovery_test.init_watcher_, ready());
  EXPECT_THROW_WITH_MESSAGE(
      config_discovery_test.callbacks_->onConfigUpdate(decoded_resources.refvec_,
                                                       response.version_info()),
      EnvoyException,
      "Error: filter config has type URL test.integration.filters.AddBodyFilterConfig but "
      "expect " +
          config_discovery_test.getTypeUrl() + ".");
  EXPECT_EQ(
      0UL,
      config_discovery_test.scope_.counter(config_discovery_test.getConfigReloadCounter()).value());
}

// Throw Envoy exception when default config is wrong.
TYPED_TEST(FilterConfigDiscoveryImplTestParameter, WrongDefaultConfig) {
  InSequence s;
  TypeParam config_discovery_test;
  envoy::config::core::v3::ExtensionConfigSource config_source;
  // Set up the default config with a bogus type url.
  config_source.mutable_default_config()->set_type_url(
      "type.googleapis.com/test.integration.filters.Bogus");
  EXPECT_THROW_WITH_MESSAGE(
      config_discovery_test.filter_config_provider_manager_->createDynamicFilterConfigProvider(
          config_source, "foo", config_discovery_test.factory_context_.getServerFactoryContext(),
          config_discovery_test.factory_context_, true, config_discovery_test.getFilterType(),
          config_discovery_test.getMatcher()),
      EnvoyException,
      "Error: cannot find filter factory foo for default filter "
      "configuration with type URL "
      "type.googleapis.com/test.integration.filters.Bogus.");
}

// Raise exception when filter is not the last filter in filter chain, but the filter is terminal
// filter. This test is HTTP filter specific.
TYPED_TEST(FilterConfigDiscoveryImplTestParameter, TerminalFilterInvalid) {
  InSequence s;
  TypeParam config_discovery_test;
  if (config_discovery_test.getFilterType() != "http") {
    return;
  }

  config_discovery_test.setup(true, false, false);
  const std::string response_yaml = R"EOF(
  version_info: "1"
  resources:
  - "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
    name: foo
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  )EOF";
  const auto response =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response_yaml);
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::core::v3::TypedExtensionConfig>(response);
  EXPECT_CALL(config_discovery_test.init_watcher_, ready());
  EXPECT_THROW_WITH_MESSAGE(
      config_discovery_test.callbacks_->onConfigUpdate(decoded_resources.refvec_,
                                                       response.version_info()),
      EnvoyException,
      "Error: terminal filter named foo of type envoy.filters.http.router must be the last filter "
      "in a http filter chain.");
  EXPECT_EQ(
      0UL,
      config_discovery_test.scope_.counter("extension_config_discovery.foo.config_reload").value());
}

// TCP listener filter matcher test.
class TcpListenerFilterConfigMatcherTest : public testing::Test,
                                           public TcpListenerFilterConfigDiscoveryImplTest {
public:
  TcpListenerFilterConfigMatcherTest() : matcher_(nullptr) {}
  const Network::ListenerFilterMatcherSharedPtr getMatcher() const override { return matcher_; }
  void setMatcher(Network::ListenerFilterMatcherSharedPtr matcher) { matcher_ = matcher; }

  Network::ListenerFilterMatcherSharedPtr matcher_;
};

// Setup matcher as nullptr
TEST_F(TcpListenerFilterConfigMatcherTest, TcpListenerFilterNullMatcher) {
  // By default, getMatcher() returns nullptr.
  setup();
  // Verify the listener_filter_matcher_ stored in provider_ matches with the configuration.
  EXPECT_EQ(provider_->getListenerFilterMatcher(), nullptr);
  EXPECT_CALL(init_watcher_, ready());
}

// Setup matcher as any matcher.
TEST_F(TcpListenerFilterConfigMatcherTest, TcpListenerFilterAnyMatcher) {
  envoy::config::listener::v3::ListenerFilterChainMatchPredicate pred;
  pred.set_any_match(true);
  Network::ListenerFilterMatcherSharedPtr matcher =
      Network::ListenerFilterMatcherBuilder::buildListenerFilterMatcher(pred);
  setMatcher(matcher);
  setup();
  EXPECT_EQ(provider_->getListenerFilterMatcher(), matcher);
  EXPECT_CALL(init_watcher_, ready());
}

} // namespace
} // namespace Filter
} // namespace Envoy
