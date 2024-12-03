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
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/registry.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::ReturnRef;

namespace Envoy {
namespace Filter {
namespace {

class TestFilterFactory {
public:
  virtual ~TestFilterFactory() = default;

  bool created_{false};
};

class TestHttpFilterFactory : public TestFilterFactory,
                              public Server::Configuration::NamedHttpFilterConfigFactory,
                              public Server::Configuration::UpstreamHttpFilterConfigFactory {
public:
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                               Server::Configuration::FactoryContext&) override {
    created_ = true;
    return [](Http::FilterChainFactoryCallbacks&) -> void {};
  }
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                               Server::Configuration::UpstreamFactoryContext&) override {
    created_ = true;
    return [](Http::FilterChainFactoryCallbacks&) -> void {};
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }
  std::string name() const override { return "envoy.test.filter"; }
  bool isTerminalFilterByProto(const Protobuf::Message&,
                               Server::Configuration::ServerFactoryContext&) override {
    return true;
  }
};

class TestNetworkFilterFactory
    : public TestFilterFactory,
      public Server::Configuration::NamedNetworkFilterConfigFactory,
      public Server::Configuration::NamedUpstreamNetworkFilterConfigFactory {
public:
  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext&) override {
    created_ = true;
    return [](Network::FilterManager&) -> void {};
  }
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::UpstreamFactoryContext&) override {
    created_ = true;
    return [](Network::FilterManager&) -> void {};
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }
  std::string name() const override { return "envoy.test.filter"; }
  bool isTerminalFilterByProto(const Protobuf::Message&,
                               Server::Configuration::ServerFactoryContext&) override {
    return true;
  }
};

class TestListenerFilterFactory : public TestFilterFactory,
                                  public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  Network::ListenerFilterFactoryCb
  createListenerFilterFactoryFromProto(const Protobuf::Message&,
                                       const Network::ListenerFilterMatcherSharedPtr&,
                                       Server::Configuration::ListenerFactoryContext&) override {
    created_ = true;
    return [](Network::ListenerFilterManager&) -> void {};
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }
  std::string name() const override { return "envoy.test.filter"; }
};

class TestUdpListenerFilterFactory
    : public TestFilterFactory,
      public Server::Configuration::NamedUdpListenerFilterConfigFactory {
public:
  Network::UdpListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::ListenerFactoryContext&) override {
    created_ = true;
    return [](Network::UdpListenerFilterManager&, Network::UdpReadFilterCallbacks&) -> void {};
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }
  std::string name() const override { return "envoy.test.filter"; }
};

class TestUdpSessionFilterFactory
    : public TestFilterFactory,
      public Server::Configuration::NamedUdpSessionFilterConfigFactory {
public:
  Network::UdpSessionFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext&) override {
    created_ = true;
    return [](Network::UdpSessionFilterChainFactoryCallbacks&) -> void {};
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }
  std::string name() const override { return "envoy.test.filter"; }
};

class TestQuicListenerFilterFactory
    : public TestFilterFactory,
      public Server::Configuration::NamedQuicListenerFilterConfigFactory {
public:
  Network::QuicListenerFilterFactoryCb
  createListenerFilterFactoryFromProto(const Protobuf::Message&,
                                       const Network::ListenerFilterMatcherSharedPtr&,
                                       Server::Configuration::ListenerFactoryContext&) override {
    created_ = true;
    return [](Network::QuicListenerFilterManager&) -> void {};
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }
  std::string name() const override { return "envoy.test.filter"; }
};

class FilterConfigDiscoveryTestBase {
public:
  FilterConfigDiscoveryTestBase() {
    // For server_factory_context
    ON_CALL(server_factory_context_, scope()).WillByDefault(ReturnRef(scope_));
    ON_CALL(server_factory_context_, messageValidationContext())
        .WillByDefault(ReturnRef(validation_context_));
    ON_CALL(validation_context_, dynamicValidationVisitor())
        .WillByDefault(ReturnRef(validation_visitor_));
    ON_CALL(init_manager_, add(_)).WillByDefault(Invoke([this](const Init::Target& target) {
      init_target_handle_ = target.createHandle("test");
    }));
    ON_CALL(init_manager_, initialize(_))
        .WillByDefault(Invoke(
            [this](const Init::Watcher& watcher) { init_target_handle_->initialize(watcher); }));
  }
  virtual ~FilterConfigDiscoveryTestBase() {
    server_factory_context_.thread_local_.shutdownThread();
  }
  Event::SimulatedTimeSystem& timeSystem() { return time_system_; }

  Event::SimulatedTimeSystem time_system_;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  NiceMock<Init::MockManager> init_manager_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  Init::ExpectableWatcherImpl init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  NiceMock<Stats::MockIsolatedStatsStore> store_;
  Stats::Scope& scope_{*store_.rootScope()};
};

// Common base ECDS test class for HTTP filter, Network filter and TCP/UDP listener filter.
template <class FactoryCb, class FactoryCtx, class CfgProviderMgrImpl, class FilterFactory,
          class FilterCategory, class MockFactoryCtx>
class FilterConfigDiscoveryImplTest : public FilterConfigDiscoveryTestBase {
public:
  FilterConfigDiscoveryImplTest() : inject_factory_(filter_factory_) {
    ON_CALL(factory_context_, serverFactoryContext())
        .WillByDefault(ReturnRef(server_factory_context_));
    ON_CALL(factory_context_, initManager()).WillByDefault(ReturnRef(init_manager_));
    filter_config_provider_manager_ = std::make_unique<CfgProviderMgrImpl>();
  }

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
      ProtobufWkt::StringValue default_config;
      config_source.mutable_default_config()->PackFrom(default_config);
    }

    return filter_config_provider_manager_->createDynamicFilterConfigProvider(
        config_source, name, server_factory_context_, factory_context_,
        server_factory_context_.cluster_manager_, last_filter_config, getFilterType(),
        getMatcher());
  }

  void setup(bool warm = true, bool default_configuration = false, bool last_filter_config = true) {
    provider_ = createProvider("foo", warm, default_configuration, last_filter_config);
    callbacks_ = server_factory_context_.cluster_manager_.subscription_factory_.callbacks_;
    EXPECT_CALL(*server_factory_context_.cluster_manager_.subscription_factory_.subscription_,
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
    ASSERT_TRUE(
        callbacks_->onConfigUpdate(decoded_resources.refvec_, response.version_info()).ok());
    EXPECT_NE(absl::nullopt, provider_->config());
    EXPECT_EQ(1UL, store_.counter(getConfigReloadCounter()).value());
    EXPECT_EQ(0UL, store_.counter(getConfigFailCounter()).value());

    // Ensure that we honor resource removals.
    Protobuf::RepeatedPtrField<std::string> remove;
    *remove.Add() = "foo";
    ASSERT_TRUE(callbacks_->onConfigUpdate({}, remove, "1").ok());
    EXPECT_EQ(2UL, store_.counter(getConfigReloadCounter()).value());
    EXPECT_EQ(0UL, store_.counter(getConfigFailCounter()).value());
  }

  const std::string getTypeUrl() const { return "google.protobuf.StringValue"; }

  virtual const std::string getFilterType() const PURE;
  virtual const Network::ListenerFilterMatcherSharedPtr getMatcher() const { return nullptr; }
  virtual const std::string getConfigReloadCounter() const PURE;
  virtual const std::string getConfigFailCounter() const PURE;

  NiceMock<MockFactoryCtx> factory_context_;
  FilterFactory filter_factory_;
  Registry::InjectFactory<FilterCategory> inject_factory_;
  std::unique_ptr<FilterConfigProviderManager<FactoryCb, FactoryCtx>>
      filter_config_provider_manager_;
  DynamicFilterConfigProviderPtr<FactoryCb> provider_;
  Config::SubscriptionCallbacks* callbacks_{};
};

// HTTP filter test
class HttpFilterConfigDiscoveryImplTest
    : public FilterConfigDiscoveryImplTest<
          HttpFilterFactoryCb, Server::Configuration::FactoryContext,
          HttpFilterConfigProviderManagerImpl, TestHttpFilterFactory,
          Server::Configuration::NamedHttpFilterConfigFactory,
          Server::Configuration::MockFactoryContext> {
public:
  const std::string getFilterType() const override { return "http"; }
  const std::string getConfigReloadCounter() const override {
    return "extension_config_discovery.http_filter.foo.config_reload";
  }
  const std::string getConfigFailCounter() const override {
    return "extension_config_discovery.http_filter.foo.config_fail";
  }
};

// Upstream HTTP filter test
class HttpUpstreamFilterConfigDiscoveryImplTest
    : public FilterConfigDiscoveryImplTest<
          HttpFilterFactoryCb, Server::Configuration::UpstreamFactoryContext,
          UpstreamHttpFilterConfigProviderManagerImpl, TestHttpFilterFactory,
          Server::Configuration::UpstreamHttpFilterConfigFactory,
          Server::Configuration::MockUpstreamFactoryContext> {
public:
  const std::string getFilterType() const override { return "http"; }
  const std::string getConfigReloadCounter() const override {
    return "extension_config_discovery.upstream_http_filter.foo.config_reload";
  }
  const std::string getConfigFailCounter() const override {
    return "extension_config_discovery.upstream_http_filter.foo.config_fail";
  }
};

// Network filter test
class NetworkFilterConfigDiscoveryImplTest
    : public FilterConfigDiscoveryImplTest<
          Network::FilterFactoryCb, Server::Configuration::FactoryContext,
          NetworkFilterConfigProviderManagerImpl, TestNetworkFilterFactory,
          Server::Configuration::NamedNetworkFilterConfigFactory,
          Server::Configuration::MockFactoryContext> {
public:
  const std::string getFilterType() const override { return "network"; }
  const std::string getConfigReloadCounter() const override {
    return "extension_config_discovery.network_filter.foo.config_reload";
  }
  const std::string getConfigFailCounter() const override {
    return "extension_config_discovery.network_filter.foo.config_fail";
  }
};

// Upstream network filter test
class NetworkUpstreamFilterConfigDiscoveryImplTest
    : public FilterConfigDiscoveryImplTest<
          Network::FilterFactoryCb, Server::Configuration::UpstreamFactoryContext,
          UpstreamNetworkFilterConfigProviderManagerImpl, TestNetworkFilterFactory,
          Server::Configuration::NamedUpstreamNetworkFilterConfigFactory,
          Server::Configuration::MockUpstreamFactoryContext> {
public:
  const std::string getFilterType() const override { return "upstream_network"; }
  const std::string getConfigReloadCounter() const override {
    return "extension_config_discovery.upstream_network_filter.foo.config_reload";
  }
  const std::string getConfigFailCounter() const override {
    return "extension_config_discovery.upstream_network_filter.foo.config_fail";
  }
};

// TCP listener filter test
class TcpListenerFilterConfigDiscoveryImplTest
    : public FilterConfigDiscoveryImplTest<
          Network::ListenerFilterFactoryCb, Server::Configuration::ListenerFactoryContext,
          TcpListenerFilterConfigProviderManagerImpl, TestListenerFilterFactory,
          Server::Configuration::NamedListenerFilterConfigFactory,
          Server::Configuration::MockFactoryContext> {
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
          UdpListenerFilterConfigProviderManagerImpl, TestUdpListenerFilterFactory,
          Server::Configuration::NamedUdpListenerFilterConfigFactory,
          Server::Configuration::MockFactoryContext> {
public:
  const std::string getFilterType() const override { return "listener"; }
  const std::string getConfigReloadCounter() const override {
    return "extension_config_discovery.udp_listener_filter.foo.config_reload";
  }
  const std::string getConfigFailCounter() const override {
    return "extension_config_discovery.udp_listener_filter.foo.config_fail";
  }
};

// UDP session filter test
class UdpSessionFilterConfigDiscoveryImplTest
    : public FilterConfigDiscoveryImplTest<
          Network::UdpSessionFilterFactoryCb, Server::Configuration::FactoryContext,
          UdpSessionFilterConfigProviderManagerImpl, TestUdpSessionFilterFactory,
          Server::Configuration::NamedUdpSessionFilterConfigFactory,
          Server::Configuration::MockFactoryContext> {
public:
  const std::string getFilterType() const override { return "udp_session"; }
  const std::string getConfigReloadCounter() const override {
    return "extension_config_discovery.udp_session_filter.foo.config_reload";
  }
  const std::string getConfigFailCounter() const override {
    return "extension_config_discovery.udp_session_filter.foo.config_fail";
  }
};

// QUIC listener filter test
class QuicListenerFilterConfigDiscoveryImplTest
    : public FilterConfigDiscoveryImplTest<
          Network::QuicListenerFilterFactoryCb, Server::Configuration::ListenerFactoryContext,
          QuicListenerFilterConfigProviderManagerImpl, TestQuicListenerFilterFactory,
          Server::Configuration::NamedQuicListenerFilterConfigFactory,
          Server::Configuration::MockFactoryContext> {
public:
  const std::string getFilterType() const override { return "listener"; }
  const std::string getConfigReloadCounter() const override {
    return "extension_config_discovery.quic_listener_filter.foo.config_reload";
  }
  const std::string getConfigFailCounter() const override {
    return "extension_config_discovery.quic_listener_filter.foo.config_fail";
  }
};

/***************************************************************************************
 *                  Parameterized test for                                             *
 *     HTTP filter, Network filter, TCP listener filter, UDP session filter and        *
 *     UDP listener filter                                                             *
 *                                                                                     *
 ***************************************************************************************/
template <typename FilterConfigDiscoveryTestType>
class FilterConfigDiscoveryImplTestParameter : public testing::Test {};

// The test filter types.
using FilterConfigDiscoveryTestTypes = ::testing::Types<
    HttpFilterConfigDiscoveryImplTest, HttpUpstreamFilterConfigDiscoveryImplTest,
    NetworkFilterConfigDiscoveryImplTest, NetworkUpstreamFilterConfigDiscoveryImplTest,
    TcpListenerFilterConfigDiscoveryImplTest, UdpListenerFilterConfigDiscoveryImplTest,
    UdpSessionFilterConfigDiscoveryImplTest, QuicListenerFilterConfigDiscoveryImplTest>;

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

    EXPECT_CALL(config_discovery_test.init_watcher_, ready())
        .WillOnce(Invoke([&config_discovery_test]() {
          EXPECT_TRUE(config_discovery_test.filter_factory_.created_);
        }));
    ASSERT_TRUE(config_discovery_test.callbacks_
                    ->onConfigUpdate(decoded_resources.refvec_, response.version_info())
                    .ok());
    EXPECT_NE(absl::nullopt, config_discovery_test.provider_->config());
    EXPECT_EQ(1UL,
              config_discovery_test.store_.counter(config_discovery_test.getConfigReloadCounter())
                  .value());
    EXPECT_EQ(
        0UL,
        config_discovery_test.store_.counter(config_discovery_test.getConfigFailCounter()).value());
  }

  // 2nd request with same response. Based on hash should not reload config.
  {
    const auto response = config_discovery_test.createResponse("2", "foo");
    const auto decoded_resources =
        TestUtility::decodeResources<envoy::config::core::v3::TypedExtensionConfig>(response);
    ASSERT_TRUE(config_discovery_test.callbacks_
                    ->onConfigUpdate(decoded_resources.refvec_, response.version_info())
                    .ok());

    EXPECT_EQ(1UL,
              config_discovery_test.store_.counter(config_discovery_test.getConfigReloadCounter())
                  .value());
    EXPECT_EQ(
        0UL,
        config_discovery_test.store_.counter(config_discovery_test.getConfigFailCounter()).value());
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
      config_discovery_test.store_.counter(config_discovery_test.getConfigReloadCounter()).value());
  EXPECT_EQ(
      1UL,
      config_discovery_test.store_.counter(config_discovery_test.getConfigFailCounter()).value());
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
  EXPECT_EQ(config_discovery_test.callbacks_
                ->onConfigUpdate(decoded_resources.refvec_, response.version_info())
                .message(),
            "Unexpected number of resources in ExtensionConfigDS response: 2");
  EXPECT_EQ(
      0UL,
      config_discovery_test.store_.counter(config_discovery_test.getConfigReloadCounter()).value());
}

TYPED_TEST(FilterConfigDiscoveryImplTestParameter, WrongName) {
  InSequence s;
  TypeParam config_discovery_test;
  config_discovery_test.setup();
  const auto response = config_discovery_test.createResponse("1", "bar");
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::core::v3::TypedExtensionConfig>(response);
  EXPECT_CALL(config_discovery_test.init_watcher_, ready());
  EXPECT_EQ(config_discovery_test.callbacks_
                ->onConfigUpdate(decoded_resources.refvec_, response.version_info())
                .message(),
            "Unexpected resource name in ExtensionConfigDS response: bar");
  EXPECT_EQ(
      0UL,
      config_discovery_test.store_.counter(config_discovery_test.getConfigReloadCounter()).value());
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
      config_discovery_test.store_.counter(config_discovery_test.getConfigReloadCounter()).value());
  EXPECT_EQ(
      0UL,
      config_discovery_test.store_.counter(config_discovery_test.getConfigFailCounter()).value());
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
  ASSERT_TRUE(config_discovery_test.callbacks_
                  ->onConfigUpdate(decoded_resources.refvec_, response.version_info())
                  .ok());
  EXPECT_NE(absl::nullopt, config_discovery_test.provider_->config());
  EXPECT_NE(absl::nullopt, provider2->config());
  EXPECT_EQ(
      1UL,
      config_discovery_test.store_.counter(config_discovery_test.getConfigReloadCounter()).value());
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
  ASSERT_EQ(config_discovery_test.callbacks_
                ->onConfigUpdate(decoded_resources.refvec_, response.version_info())
                .message(),
            "Error: filter config has type URL test.integration.filters.AddBodyFilterConfig but "
            "expect " +
                config_discovery_test.getTypeUrl() + ".");
  EXPECT_EQ(
      0UL,
      config_discovery_test.store_.counter(config_discovery_test.getConfigReloadCounter()).value());
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
          config_source, "foo", config_discovery_test.server_factory_context_,
          config_discovery_test.factory_context_,
          config_discovery_test.server_factory_context_.cluster_manager_, true,
          config_discovery_test.getFilterType(), config_discovery_test.getMatcher()),
      EnvoyException,
      "Error: cannot find filter factory foo for default filter "
      "configuration with type URL "
      "type.googleapis.com/test.integration.filters.Bogus.");
}

// For filters which are not listener and upstream network, raise exception when filter is not the
// last filter in filter chain, but the filter is terminal. For listener and upstream network filter
// check that there is no exception raised.
TYPED_TEST(FilterConfigDiscoveryImplTestParameter, TerminalFilterInvalid) {
  InSequence s;
  TypeParam config_discovery_test;

  config_discovery_test.setup(true, false, false);
  const std::string response_yaml = R"EOF(
  version_info: "1"
  resources:
  - "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
    name: foo
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  )EOF";
  const auto response =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response_yaml);
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::core::v3::TypedExtensionConfig>(response);
  EXPECT_CALL(config_discovery_test.init_watcher_, ready());

  if (config_discovery_test.getFilterType() == "listener" ||
      config_discovery_test.getFilterType() == "upstream_network" ||
      config_discovery_test.getFilterType() == "udp_session") {
    ASSERT_TRUE(config_discovery_test.callbacks_
                    ->onConfigUpdate(decoded_resources.refvec_, response.version_info())
                    .ok());
    return;
  }

  EXPECT_THROW_WITH_MESSAGE(
      config_discovery_test.callbacks_
          ->onConfigUpdate(decoded_resources.refvec_, response.version_info())
          .IgnoreError(),
      EnvoyException,
      "Error: terminal filter named foo of type envoy.test.filter must be the last filter "
      "in a " +
          config_discovery_test.getFilterType() + " filter chain.");
  EXPECT_EQ(
      0UL,
      config_discovery_test.store_.counter("extension_config_discovery.foo.config_reload").value());
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
