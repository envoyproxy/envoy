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
    // Thread local storage assumes a single (main) thread with no workers.
    ON_CALL(factory_context_.admin_, concurrency()).WillByDefault(Return(0));
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
    filter_config_provider_manager_ = std::make_unique<HttpFilterConfigProviderManagerImpl>();
  }
  ~FilterConfigDiscoveryImplTest() override { factory_context_.thread_local_.shutdownThread(); }

  DynamicFilterConfigProviderPtr<Http::FilterFactoryCb>
  createProvider(std::string name, bool warm, bool default_configuration,
                 bool last_filter_config = true) {

    EXPECT_CALL(init_manager_, add(_));
    envoy::config::core::v3::ExtensionConfigSource config_source;

    std::string inject_default_configuration;
    if (default_configuration) {
      inject_default_configuration = R"EOF(
default_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  suppress_envoy_headers: true
)EOF";
    }
    TestUtility::loadFromYaml(absl::Substitute(R"EOF(
config_source: { ads: {} }
$0
type_urls:
- envoy.extensions.filters.http.router.v3.Router
)EOF",
                                               inject_default_configuration),
                              config_source);
    if (!warm) {
      config_source.set_apply_default_config_without_warming(true);
      TestUtility::loadFromYaml(R"EOF(
"@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
)EOF",
                                *config_source.mutable_default_config());
    }

    return filter_config_provider_manager_->createDynamicFilterConfigProvider(
        config_source, name, factory_context_, "xds.", last_filter_config, "http");
  }

  void setup(bool warm = true, bool default_configuration = false, bool last_filter_config = true) {
    provider_ = createProvider("foo", warm, default_configuration, last_filter_config);
    callbacks_ = factory_context_.cluster_manager_.subscription_factory_.callbacks_;
    EXPECT_CALL(*factory_context_.cluster_manager_.subscription_factory_.subscription_, start(_));
    if (!warm) {
      EXPECT_CALL(init_watcher_, ready());
    }
    init_manager_.initialize(init_watcher_);
  }

  std::unique_ptr<FilterConfigProviderManager<Http::FilterFactoryCb>>
      filter_config_provider_manager_;
  DynamicFilterConfigProviderPtr<Http::FilterFactoryCb> provider_;
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

  // Bool parameter allows generating two different filter configs.
  auto do_xds_response = [this](bool b) {
    const std::string response_yaml = fmt::format(R"EOF(
version_info: "1"
resources:
- "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
  name: foo
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
    suppress_envoy_headers: {}
)EOF",
                                                  b);
    const auto response =
        TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response_yaml);
    const auto decoded_resources =
        TestUtility::decodeResources<envoy::config::core::v3::TypedExtensionConfig>(response);
    callbacks_->onConfigUpdate(decoded_resources.refvec_, {}, response.version_info());
  };

  EXPECT_CALL(init_watcher_, ready());
  do_xds_response(true);
  EXPECT_NE(absl::nullopt, provider_->config());
  EXPECT_EQ(1UL, scope_.counter("xds.extension_config_discovery.foo.config_reload").value());
  EXPECT_EQ(0UL, scope_.counter("xds.extension_config_discovery.foo.config_fail").value());

  // Ensure that we honor resource removals.
  Protobuf::RepeatedPtrField<std::string> remove;
  *remove.Add() = "foo";
  callbacks_->onConfigUpdate({}, remove, "1");
  EXPECT_EQ(absl::nullopt, provider_->config());
  EXPECT_EQ(2UL, scope_.counter("xds.extension_config_discovery.foo.config_reload").value());
  EXPECT_EQ(0UL, scope_.counter("xds.extension_config_discovery.foo.config_fail").value());
}

TEST_F(FilterConfigDiscoveryImplTest, IncrementalWithDefault) {
  InSequence s;
  setup(true, true);

  // Bool parameter allows generating two different filter configs. The default configuration has
  // b=true.
  auto do_xds_response = [this](bool b) {
    const std::string response_yaml = fmt::format(R"EOF(
version_info: "1"
resources:
- "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
  name: foo
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
    suppress_envoy_headers: {}
)EOF",
                                                  b);

    const auto response =
        TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response_yaml);
    const auto decoded_resources =
        TestUtility::decodeResources<envoy::config::core::v3::TypedExtensionConfig>(response);
    callbacks_->onConfigUpdate(decoded_resources.refvec_, {}, response.version_info());
  };

  EXPECT_NE(absl::nullopt, provider_->config());

  EXPECT_CALL(init_watcher_, ready());
  do_xds_response(false);
  EXPECT_NE(absl::nullopt, provider_->config());
  EXPECT_EQ(1UL, scope_.counter("xds.extension_config_discovery.foo.config_reload").value());
  EXPECT_EQ(0UL, scope_.counter("xds.extension_config_discovery.foo.config_fail").value());

  // If we get a removal while a default is configured, we should revert back to the default
  // instead of clearing out the factory.
  Protobuf::RepeatedPtrField<std::string> remove;
  *remove.Add() = "foo";
  callbacks_->onConfigUpdate({}, remove, "1");
  EXPECT_NE(absl::nullopt, provider_->config());
  EXPECT_EQ(2UL, scope_.counter("xds.extension_config_discovery.foo.config_reload").value());
  EXPECT_EQ(0UL, scope_.counter("xds.extension_config_discovery.foo.config_fail").value());
}

TEST_F(FilterConfigDiscoveryImplTest, ApplyWithoutWarming) {
  InSequence s;
  setup(false);
  EXPECT_EQ("foo", provider_->name());
  EXPECT_NE(absl::nullopt, provider_->config());
  EXPECT_EQ(0UL, scope_.counter("xds.extension_config_discovery.foo.config_reload").value());
  EXPECT_EQ(0UL, scope_.counter("xds.extension_config_discovery.foo.config_fail").value());
}

TEST_F(FilterConfigDiscoveryImplTest, DualProviders) {
  InSequence s;
  setup();
  auto provider2 = createProvider("foo", true, false);
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
  auto provider2 = createProvider("foo", true, false);
  const std::string response_yaml = R"EOF(
  version_info: "1"
  resources:
  - "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
    name: foo
    typed_config:
      "@type": type.googleapis.com/test.integration.filters.AddBodyFilterConfig
      body_size: 10
  )EOF";
  const auto response =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response_yaml);
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::core::v3::TypedExtensionConfig>(response);
  EXPECT_CALL(init_watcher_, ready());
  EXPECT_THROW_WITH_MESSAGE(
      callbacks_->onConfigUpdate(decoded_resources.refvec_, response.version_info()),
      EnvoyException,
      "Error: filter config has type URL test.integration.filters.AddBodyFilterConfig but "
      "expect envoy.extensions.filters.http.router.v3.Router.");
  EXPECT_EQ(0UL, scope_.counter("xds.extension_config_discovery.foo.config_reload").value());
}

// Raise exception when filter is not the last filter in filter chain, but the filter is terminal
// filter.
TEST_F(FilterConfigDiscoveryImplTest, TerminalFilterInvalid) {
  InSequence s;
  setup(true, false, false);
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
  EXPECT_THROW_WITH_MESSAGE(
      callbacks_->onConfigUpdate(decoded_resources.refvec_, response.version_info()),
      EnvoyException,
      "Error: terminal filter named foo of type envoy.filters.http.router must be the last filter "
      "in a http filter chain.");
  EXPECT_EQ(0UL, scope_.counter("xds.extension_config_discovery.foo.config_reload").value());
}

} // namespace
} // namespace Filter
} // namespace Envoy
