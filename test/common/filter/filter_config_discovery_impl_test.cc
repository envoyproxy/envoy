#include <chrono>
#include <memory>
#include <string>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "common/config/utility.h"
#include "common/filter/filter_config_discovery_impl.h"
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
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
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

class FilterConfigDiscoveryImplTest : public FilterConfigDiscoveryTestBase {
public:
  FilterConfigDiscoveryImplTest() {
    filter_config_provider_manager_ = std::make_unique<HttpFilterConfigProviderManagerImpl>();
  }
  ~FilterConfigDiscoveryImplTest() override { factory_context_.thread_local_.shutdownThread(); }

  void setup() {
    envoy::config::core::v3::ConfigSource config_source;
    TestUtility::loadFromYaml("ads: {}", config_source);
    EXPECT_CALL(init_manager_, add(_));
    provider_ = filter_config_provider_manager_->createDynamicFilterConfigProvider(
        config_source, "foo", true, absl::nullopt, factory_context_, "xds.", false);
    callbacks_ = factory_context_.cluster_manager_.subscription_factory_.callbacks_;
    EXPECT_CALL(*factory_context_.cluster_manager_.subscription_factory_.subscription_, start(_));
    init_manager_.initialize(init_watcher_);
  }

  std::unique_ptr<HttpFilterConfigProviderManager> filter_config_provider_manager_;
  HttpFilterConfigProviderPtr provider_;
  Envoy::Config::SubscriptionCallbacks* callbacks_{};
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
  const std::string response1_yaml = R"EOF(
  version_info: "1"
  resources:
  - "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
    name: foo
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  )EOF";
  auto response1 =
      TestUtility::parseYaml<envoy::service::discovery::v3::DiscoveryResponse>(response1_yaml);
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::core::v3::TypedExtensionConfig>(response1);

  EXPECT_CALL(init_watcher_, ready());
  callbacks_->onConfigUpdate(decoded_resources.refvec_, response1.version_info());
  EXPECT_NE(absl::nullopt, provider_->config());
}

} // namespace
} // namespace Filter
} // namespace Envoy
