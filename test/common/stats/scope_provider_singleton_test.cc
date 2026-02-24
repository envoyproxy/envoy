#include "source/common/config/decoded_resource_impl.h"
#include "source/common/stats/scope_provider_singleton.h"
#include "source/common/stats/symbol_table.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

class ScopeProviderSingletonTest : public testing::Test {
public:
  ScopeProviderSingletonTest()
      : root_scope_(Stats::StatNameManagedStorage("root", mock_store_.symbolTable()).statName(),
                    mock_store_) {}

  NiceMock<Stats::MockStore> mock_store_;
  NiceMock<Stats::MockScope> root_scope_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  envoy::config::core::v3::ConfigSource config_source_;
};

TEST_F(ScopeProviderSingletonTest, ScopeWrapperDelegation) {
  ON_CALL(root_scope_, createScope_(_))
      .WillByDefault(Return(std::make_shared<Stats::TestUtil::TestScope>(
          Stats::StatNameManagedStorage("dummy", mock_store_.symbolTable()).statName(),
          mock_store_)));

  EXPECT_CALL(factory_context_, scope()).WillRepeatedly(ReturnRef(root_scope_));

  auto provider = std::make_shared<ScopeProviderSingleton>(factory_context_, config_source_);

  EXPECT_CALL(factory_context_.xds_manager_,
              subscribeToSingletonResource("test_scope", _, _, _, _, _, _))
      .WillOnce(
          Invoke([](absl::string_view, OptRef<const envoy::config::core::v3::ConfigSource>,
                    absl::string_view, Stats::Scope&, Config::SubscriptionCallbacks&,
                    Config::OpaqueResourceDecoderSharedPtr,
                    const Config::SubscriptionOptions&) -> absl::StatusOr<Config::SubscriptionPtr> {
            return std::make_unique<NiceMock<Config::MockSubscription>>();
          }));

  auto subscription =
      std::make_shared<ScopeProviderSingleton::ScopeSubscription>(*provider, "test_scope");

  // createScope called for test_scope
  EXPECT_CALL(root_scope_, createScope_("test_scope"))
      .WillOnce(Return(std::make_shared<Stats::TestUtil::TestScope>(
          Stats::StatNameManagedStorage("test_scope", mock_store_.symbolTable()).statName(),
          mock_store_)));

  auto scope = factory_context_.scope().createScope("test_scope", false, {});
  auto init_target = std::make_unique<Init::TargetImpl>("test_target", []() {});

  ScopeProviderSingleton::ScopeWrapper wrapper(provider, subscription, scope,
                                               std::move(init_target));

  // Test counterFromStatName
  StatNameManagedStorage counter_name("counter", factory_context_.scope().symbolTable());
  Counter& counter = wrapper.getScope()->counterFromStatName(counter_name.statName());
  EXPECT_EQ(counter.name(), "test_scope.counter");

  // Test gaugeFromStatName
  StatNameManagedStorage gauge_name("gauge", factory_context_.scope().symbolTable());
  Gauge& gauge =
      wrapper.getScope()->gaugeFromStatName(gauge_name.statName(), Gauge::ImportMode::Accumulate);
  EXPECT_EQ(gauge.name(), "test_scope.gauge");

  // Test histogramFromStatName
  StatNameManagedStorage histogram_name("histogram", factory_context_.scope().symbolTable());
  Histogram& histogram = wrapper.getScope()->histogramFromStatName(histogram_name.statName(),
                                                                   Histogram::Unit::Unspecified);
  EXPECT_EQ(histogram.name(), "test_scope.histogram");

  // Test textReadoutFromStatName
  StatNameManagedStorage text_readout_name("text_readout", factory_context_.scope().symbolTable());
  TextReadout& text_readout =
      wrapper.getScope()->textReadoutFromStatName(text_readout_name.statName());
  EXPECT_EQ(text_readout.name(), "test_scope.text_readout");
}

class ScopeProviderSingletonSubscriptionTest : public testing::Test {
public:
  ScopeProviderSingletonSubscriptionTest()
      : root_scope_(Stats::StatNameManagedStorage("root", mock_store_.symbolTable()).statName(),
                    mock_store_) {
    ON_CALL(root_scope_, createScope_(_))
        .WillByDefault(Return(std::make_shared<Stats::TestUtil::TestScope>(
            Stats::StatNameManagedStorage("dummy", mock_store_.symbolTable()).statName(),
            mock_store_)));
    EXPECT_CALL(factory_context_, scope()).WillRepeatedly(ReturnRef(root_scope_));

    provider_ = std::make_shared<ScopeProviderSingleton>(factory_context_, config_source_);

    EXPECT_CALL(factory_context_.xds_manager_,
                subscribeToSingletonResource("test_scope", _, _, _, _, _, _))
        .WillOnce(Invoke(
            [](absl::string_view, OptRef<const envoy::config::core::v3::ConfigSource>,
               absl::string_view, Stats::Scope&, Config::SubscriptionCallbacks&,
               Config::OpaqueResourceDecoderSharedPtr,
               const Config::SubscriptionOptions&) -> absl::StatusOr<Config::SubscriptionPtr> {
              return std::make_unique<NiceMock<Config::MockSubscription>>();
            }));

    subscription_ =
        std::make_shared<ScopeProviderSingleton::ScopeSubscription>(*provider_, "test_scope");
  }

  void onConfigUpdateWithLimit(uint32_t max_counters, uint32_t max_gauges, uint32_t max_histograms,
                               absl::string_view resource_name = "test_scope") {
    envoy::config::metrics::v3::Scope scope_config;
    scope_config.set_max_counters(max_counters);
    scope_config.set_max_gauges(max_gauges);
    scope_config.set_max_histograms(max_histograms);

    auto resource = std::make_unique<Config::DecodedResourceImpl>(
        std::make_unique<envoy::config::metrics::v3::Scope>(scope_config),
        std::string(resource_name), std::vector<std::string>{}, "version");
    std::vector<Config::DecodedResourceRef> resources{*resource};

    // We expect the scope to be created with these limits. Let's mock the createScope
    if (resource_name == "test_scope") {
      EXPECT_CALL(root_scope_, createScope_("test_scope"))
          .WillOnce(Invoke([&](const std::string& name) {
            auto scope_name_storage =
                std::make_unique<Stats::StatNameDynamicStorage>(name, mock_store_.symbolTable());
            auto new_scope = std::make_shared<NiceMock<Stats::MockScope>>(
                scope_name_storage->statName(), mock_store_);
            return new_scope;
          }));
    }

    EXPECT_OK(subscription_->onConfigUpdate(resources, "version"));
  }

  NiceMock<Stats::MockStore> mock_store_;
  NiceMock<Stats::MockScope> root_scope_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  envoy::config::core::v3::ConfigSource config_source_;
  std::shared_ptr<ScopeProviderSingleton> provider_;
  std::shared_ptr<ScopeProviderSingleton::ScopeSubscription> subscription_;
};

TEST_F(ScopeProviderSingletonSubscriptionTest, OnConfigUpdateSuccess) {
  envoy::config::metrics::v3::Scope scope_config;
  scope_config.set_max_counters(10);
  scope_config.set_max_gauges(20);
  scope_config.set_max_histograms(30);

  auto resource = std::make_unique<Config::DecodedResourceImpl>(
      std::make_unique<envoy::config::metrics::v3::Scope>(scope_config), "test_scope",
      std::vector<std::string>{}, "version");
  std::vector<Config::DecodedResourceRef> resources{*resource};

  EXPECT_CALL(root_scope_, createScope_("test_scope"))
      .WillOnce(Invoke([&](const std::string& name) {
        auto scope_name_storage =
            std::make_unique<Stats::StatNameDynamicStorage>(name, mock_store_.symbolTable());
        auto new_scope = std::make_shared<NiceMock<Stats::MockScope>>(
            scope_name_storage->statName(), mock_store_);
        return new_scope;
      }));

  EXPECT_OK(subscription_->onConfigUpdate(resources, "version"));

  // Second update with same hash should not do anything.
  EXPECT_OK(subscription_->onConfigUpdate(resources, "version"));

  auto new_scope = std::dynamic_pointer_cast<Stats::MockScope>(subscription_->getScope());
  ASSERT_NE(new_scope, nullptr);

  // Update with different hash should call createScope
  scope_config.set_max_counters(15);
  auto resource2 = std::make_unique<Config::DecodedResourceImpl>(
      std::make_unique<envoy::config::metrics::v3::Scope>(scope_config), "test_scope",
      std::vector<std::string>{}, "version2");
  std::vector<Config::DecodedResourceRef> resources2{*resource2};

  EXPECT_CALL(root_scope_, createScope_("test_scope"))
      .WillOnce(Invoke([&](const std::string& name) {
        auto scope_name_storage =
            std::make_unique<Stats::StatNameDynamicStorage>(name, mock_store_.symbolTable());
        auto new_scope = std::make_shared<NiceMock<Stats::MockScope>>(
            scope_name_storage->statName(), mock_store_);
        return new_scope;
      }));

  EXPECT_OK(subscription_->onConfigUpdate(resources2, "version2"));
}

TEST_F(ScopeProviderSingletonSubscriptionTest, OnConfigUpdateBadResourceSize) {
  std::vector<Config::DecodedResourceRef> empty_resources;
  EXPECT_THAT(
      subscription_->onConfigUpdate(empty_resources, "version").message(),
      testing::HasSubstr("only one resource should be added or removed at a time but got 0"));

  envoy::config::metrics::v3::Scope scope_config;
  auto resource = std::make_unique<Config::DecodedResourceImpl>(
      std::make_unique<envoy::config::metrics::v3::Scope>(scope_config), "test_scope",
      std::vector<std::string>{}, "version");
  std::vector<Config::DecodedResourceRef> resources{*resource, *resource};
  EXPECT_THAT(
      subscription_->onConfigUpdate(resources, "version").message(),
      testing::HasSubstr("only one resource should be added or removed at a time but got 2"));
}

TEST_F(ScopeProviderSingletonSubscriptionTest, OnConfigUpdateWrongResourceName) {
  envoy::config::metrics::v3::Scope scope_config;
  auto resource = std::make_unique<Config::DecodedResourceImpl>(
      std::make_unique<envoy::config::metrics::v3::Scope>(scope_config), "wrong_scope",
      std::vector<std::string>{}, "version");
  std::vector<Config::DecodedResourceRef> resources{*resource};

  EXPECT_THAT(subscription_->onConfigUpdate(resources, "version").message(),
              testing::HasSubstr("Unexpected resource name: wrong_scope"));
}

TEST_F(ScopeProviderSingletonSubscriptionTest, OnConfigUpdateAddAndRemove) {
  envoy::config::metrics::v3::Scope scope_config;
  auto resource = std::make_unique<Config::DecodedResourceImpl>(
      std::make_unique<envoy::config::metrics::v3::Scope>(scope_config), "test_scope",
      std::vector<std::string>{}, "version");
  std::vector<Config::DecodedResourceRef> resources{*resource};

  Protobuf::RepeatedPtrField<std::string> removed_resources;

  // Both added and removed (size = 1 + 1 = 2) -> error
  removed_resources.Add(std::string("test_scope"));
  EXPECT_THAT(subscription_->onConfigUpdate(resources, removed_resources, "version").message(),
              testing::HasSubstr(
                  "Update must contain exactly one add or remove, got 1 adds and 1 removes"));

  // Only added, but wrong name -> error
  auto resource_wrong = std::make_unique<Config::DecodedResourceImpl>(
      std::make_unique<envoy::config::metrics::v3::Scope>(scope_config), "wrong_scope",
      std::vector<std::string>{}, "version");
  std::vector<Config::DecodedResourceRef> resources_wrong{*resource_wrong};
  Protobuf::RepeatedPtrField<std::string> empty_removed;
  EXPECT_THAT(subscription_->onConfigUpdate(resources_wrong, empty_removed, "version").message(),
              testing::HasSubstr("Unexpected resource name: wrong_scope"));

  // Only removed, but wrong name -> error
  std::vector<Config::DecodedResourceRef> empty_added;
  Protobuf::RepeatedPtrField<std::string> removed_wrong;
  removed_wrong.Add(std::string("wrong_scope"));
  EXPECT_THAT(subscription_->onConfigUpdate(empty_added, removed_wrong, "version").message(),
              testing::HasSubstr("Unexpected removed resource name: wrong_scope"));
}

TEST_F(ScopeProviderSingletonSubscriptionTest, OnConfigUpdateRemoveSuccess) {
  // First add a resource
  envoy::config::metrics::v3::Scope scope_config;
  auto resource = std::make_unique<Config::DecodedResourceImpl>(
      std::make_unique<envoy::config::metrics::v3::Scope>(scope_config), "test_scope",
      std::vector<std::string>{}, "version");
  std::vector<Config::DecodedResourceRef> resources{*resource};

  EXPECT_CALL(root_scope_, createScope_("test_scope"))
      .WillOnce(Invoke([&](const std::string& name) {
        auto scope_name_storage =
            std::make_unique<Stats::StatNameDynamicStorage>(name, mock_store_.symbolTable());
        auto new_scope = std::make_shared<NiceMock<Stats::MockScope>>(
            scope_name_storage->statName(), mock_store_);
        return new_scope;
      }));

  EXPECT_OK(subscription_->onConfigUpdate(resources, "version"));
  auto scope = std::dynamic_pointer_cast<Stats::MockScope>(subscription_->getScope());
  ASSERT_NE(scope, nullptr);

  // Now remove it
  std::vector<Config::DecodedResourceRef> empty_added;
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  removed_resources.Add(std::string("test_scope"));

  EXPECT_OK(subscription_->onConfigUpdate(empty_added, removed_resources, "version"));
}

TEST_F(ScopeProviderSingletonSubscriptionTest, ScopeWrapperNotifiesSetters) {
  auto scope = factory_context_.scope().createScope("test_scope", false, {});
  auto init_target = std::make_unique<Init::TargetImpl>("test_target", []() {});
  auto wrapper = std::make_unique<ScopeProviderSingleton::ScopeWrapper>(
      provider_, subscription_, scope, std::move(init_target));

  subscription_->addSetter(
      reinterpret_cast<intptr_t>(wrapper.get()),
      [w = wrapper.get()](ScopeSharedPtr new_scope) { w->setScope(new_scope); });

  // Update config, which should trigger the setter and update wrapper's scope
  envoy::config::metrics::v3::Scope scope_config;
  auto resource = std::make_unique<Config::DecodedResourceImpl>(
      std::make_unique<envoy::config::metrics::v3::Scope>(scope_config), "test_scope",
      std::vector<std::string>{}, "version");
  std::vector<Config::DecodedResourceRef> resources{*resource};

  EXPECT_CALL(root_scope_, createScope_("test_scope"))
      .WillOnce(Invoke([&](const std::string& name) {
        auto scope_name_storage =
            std::make_unique<Stats::StatNameDynamicStorage>(name, mock_store_.symbolTable());
        auto new_scope = std::make_shared<NiceMock<Stats::MockScope>>(
            scope_name_storage->statName(), mock_store_);
        return new_scope;
      }));

  EXPECT_OK(subscription_->onConfigUpdate(resources, "version"));

  EXPECT_NE(wrapper->getScope(), scope);
  EXPECT_EQ(wrapper->getScope(), subscription_->getScope());

  // Destroy wrapper, should remove setter
  // But wait, there's no easy way to inspect setters_ in subscription_.
  // The ~ScopeWrapper destructor removes it.
  wrapper.reset();
  // We can update config again, which would crash if the setter was not removed and it accesses
  // destructed wrapper.
  scope_config.set_max_counters(100);
  auto resource2 = std::make_unique<Config::DecodedResourceImpl>(
      std::make_unique<envoy::config::metrics::v3::Scope>(scope_config), "test_scope",
      std::vector<std::string>{}, "version2");
  std::vector<Config::DecodedResourceRef> resources2{*resource2};

  EXPECT_CALL(root_scope_, createScope_("test_scope"))
      .WillOnce(Invoke([&](const std::string& name) {
        auto scope_name_storage =
            std::make_unique<Stats::StatNameDynamicStorage>(name, mock_store_.symbolTable());
        auto new_scope = std::make_shared<NiceMock<Stats::MockScope>>(
            scope_name_storage->statName(), mock_store_);
        return new_scope;
      }));

  EXPECT_OK(subscription_->onConfigUpdate(resources2, "version2"));
}

} // namespace
} // namespace Stats
} // namespace Envoy
