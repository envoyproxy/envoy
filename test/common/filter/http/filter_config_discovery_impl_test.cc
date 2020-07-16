#include <chrono>
#include <memory>
#include <string>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "common/config/utility.h"
#include "common/filter/http/filter_config_discovery_impl.h"
#include "common/json/json_loader.h"

#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
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
using testing::ReturnRef;

namespace Envoy {
namespace Filter {
namespace Http {
namespace {

class FilterConfigDiscoveryTestBase : public testing::Test {
public:
  FilterConfigDiscoveryTestBase() {
    // For server_factory_context
    ON_CALL(factory_context_, scope()).WillByDefault(ReturnRef(scope_));
    ON_CALL(factory_context_, messageValidationContext())
        .WillByDefault(ReturnRef(validation_context_));
    EXPECT_CALL(validation_context_, dynamicValidationVisitor())
        .WillRepeatedly(ReturnRef(validation_visitor_));
    EXPECT_CALL(factory_context_, initManager()).WillRepeatedly(ReturnRef(init_manager_));
    ON_CALL(init_manager_, add(_)).WillByDefault(Invoke([this](const Init::Target& target) {
      init_target_handle_ = target.createHandle("test");
    }));
    ON_CALL(init_manager_, initialize(_))
        .WillByDefault(Invoke(
            [this](const Init::Watcher& watcher) { init_target_handle_->initialize(watcher); }));
  }

  Event::SimulatedTimeSystem& timeSystem() { return time_system_; }

  Event::SimulatedTimeSystem time_system_;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  NiceMock<Init::MockManager> init_manager_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  Init::ExpectableWatcherImpl init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
};

// Test base class with a single provider.
class FilterConfigDiscoveryImplTest : public FilterConfigDiscoveryTestBase {
public:
  FilterConfigDiscoveryImplTest() {
    filter_config_provider_manager_ = std::make_unique<FilterConfigProviderManagerImpl>();
  }
  ~FilterConfigDiscoveryImplTest() override { factory_context_.thread_local_.shutdownThread(); }

  FilterConfigProviderPtr createProvider(std::string name, bool warm) {
    EXPECT_CALL(init_manager_, add(_));
    envoy::config::core::v3::ConfigSource config_source;
    TestUtility::loadFromYaml("ads: {}", config_source);
    return filter_config_provider_manager_->createDynamicFilterConfigProvider(
        config_source, name, {"envoy.extensions.filters.http.router.v3.Router"}, factory_context_,
        "xds.", !warm);
  }

  void setup(bool warm = true) {
    provider_ = createProvider("foo", warm);
    callbacks_ = factory_context_.cluster_manager_.subscription_factory_.callbacks_;
    EXPECT_CALL(*factory_context_.cluster_manager_.subscription_factory_.subscription_, start(_));
    if (!warm) {
      EXPECT_CALL(init_watcher_, ready());
    }
    init_manager_.initialize(init_watcher_);
  }

  std::unique_ptr<FilterConfigProviderManager> filter_config_provider_manager_;
  FilterConfigProviderPtr provider_;
  Config::SubscriptionCallbacks* callbacks_{};
};

TEST_F(FilterConfigDiscoveryImplTest, DestroyReady) {
  setup();
  EXPECT_CALL(init_watcher_, ready());
}

TEST_F(FilterConfigDiscoveryImplTest, Basic) {
  InSequence s;
  setup();
  EXPECT_EQ("foo", provider_->name());
  EXPECT_EQ(absl::nullopt, provider_->config());

  // Initial request.
  {
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

    EXPECT_CALL(init_watcher_, ready());
    callbacks_->onConfigUpdate(decoded_resources.refvec_, response.version_info());
    EXPECT_NE(absl::nullopt, provider_->config());
    EXPECT_EQ(1UL, scope_.counter("xds.extension_config_discovery.foo.config_reload").value());
    EXPECT_EQ(0UL, scope_.counter("xds.extension_config_discovery.foo.config_fail").value());
  }

  // 2nd request with same response. Based on hash should not reload config.
  {
    const std::string response_yaml = R"EOF(
  version_info: "2"
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
    callbacks_->onConfigUpdate(decoded_resources.refvec_, response.version_info());
    EXPECT_EQ(1UL, scope_.counter("xds.extension_config_discovery.foo.config_reload").value());
    EXPECT_EQ(0UL, scope_.counter("xds.extension_config_discovery.foo.config_fail").value());
  }
}

TEST_F(FilterConfigDiscoveryImplTest, ConfigFailed) {
  InSequence s;
  setup();
  EXPECT_CALL(init_watcher_, ready());
  callbacks_->onConfigUpdateFailed(Config::ConfigUpdateFailureReason::FetchTimedout, {});
  EXPECT_EQ(0UL, scope_.counter("xds.extension_config_discovery.foo.config_reload").value());
  EXPECT_EQ(1UL, scope_.counter("xds.extension_config_discovery.foo.config_fail").value());
}

TEST_F(FilterConfigDiscoveryImplTest, TooManyResources) {
  InSequence s;
  setup();
  const std::string response_yaml = R"EOF(
  version_info: "1"
  resources:
  - "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
    name: foo
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  - "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
    name: foo
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  )EOF";
  const auto response =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response_yaml);
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::core::v3::TypedExtensionConfig>(response);
  EXPECT_CALL(init_watcher_, ready());
  EXPECT_THROW_WITH_MESSAGE(
      callbacks_->onConfigUpdate(decoded_resources.refvec_, response.version_info()),
      EnvoyException, "Unexpected number of resources in ExtensionConfigDS response: 2");
  EXPECT_EQ(0UL, scope_.counter("xds.extension_config_discovery.foo.config_reload").value());
}

TEST_F(FilterConfigDiscoveryImplTest, WrongName) {
  InSequence s;
  setup();
  const std::string response_yaml = R"EOF(
  version_info: "1"
  resources:
  - "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
    name: bar
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  )EOF";
  const auto response =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response_yaml);
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::core::v3::TypedExtensionConfig>(response);
  EXPECT_CALL(init_watcher_, ready());
  EXPECT_THROW_WITH_MESSAGE(
      callbacks_->onConfigUpdate(decoded_resources.refvec_, response.version_info()),
      EnvoyException, "Unexpected resource name in ExtensionConfigDS response: bar");
  EXPECT_EQ(0UL, scope_.counter("xds.extension_config_discovery.foo.config_reload").value());
}

TEST_F(FilterConfigDiscoveryImplTest, Incremental) {
  InSequence s;
  setup();
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
  Protobuf::RepeatedPtrField<std::string> remove;
  *remove.Add() = "bar";
  EXPECT_CALL(init_watcher_, ready());
  callbacks_->onConfigUpdate(decoded_resources.refvec_, remove, response.version_info());
  EXPECT_NE(absl::nullopt, provider_->config());
  EXPECT_EQ(1UL, scope_.counter("xds.extension_config_discovery.foo.config_reload").value());
  EXPECT_EQ(0UL, scope_.counter("xds.extension_config_discovery.foo.config_fail").value());
}

TEST_F(FilterConfigDiscoveryImplTest, ApplyWithoutWarming) {
  InSequence s;
  setup(false);
  EXPECT_EQ("foo", provider_->name());
  EXPECT_EQ(absl::nullopt, provider_->config());
  EXPECT_EQ(0UL, scope_.counter("xds.extension_config_discovery.foo.config_reload").value());
  EXPECT_EQ(0UL, scope_.counter("xds.extension_config_discovery.foo.config_fail").value());
}

TEST_F(FilterConfigDiscoveryImplTest, DualProviders) {
  InSequence s;
  setup();
  auto provider2 = createProvider("foo", true);
  EXPECT_EQ("foo", provider2->name());
  EXPECT_EQ(absl::nullopt, provider2->config());
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
  EXPECT_CALL(init_watcher_, ready());
  callbacks_->onConfigUpdate(decoded_resources.refvec_, response.version_info());
  EXPECT_NE(absl::nullopt, provider_->config());
  EXPECT_NE(absl::nullopt, provider2->config());
  EXPECT_EQ(1UL, scope_.counter("xds.extension_config_discovery.foo.config_reload").value());
}

TEST_F(FilterConfigDiscoveryImplTest, DualProvidersInvalid) {
  InSequence s;
  setup();
  auto provider2 = createProvider("foo", true);
  const std::string response_yaml = R"EOF(
  version_info: "1"
  resources:
  - "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
    name: foo
    typed_config:
      "@type": type.googleapis.com/envoy.config.filter.http.health_check.v2.HealthCheck
      pass_through_mode: false
  )EOF";
  const auto response =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response_yaml);
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::core::v3::TypedExtensionConfig>(response);
  EXPECT_CALL(init_watcher_, ready());
  EXPECT_THROW_WITH_MESSAGE(
      callbacks_->onConfigUpdate(decoded_resources.refvec_, response.version_info()),
      EnvoyException,
      "Error: filter config has type URL envoy.config.filter.http.health_check.v2.HealthCheck but "
      "expect envoy.extensions.filters.http.router.v3.Router.");
  EXPECT_EQ(0UL, scope_.counter("xds.extension_config_discovery.foo.config_reload").value());
}

} // namespace
} // namespace Http
} // namespace Filter
} // namespace Envoy
