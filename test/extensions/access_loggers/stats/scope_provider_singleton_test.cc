#include "source/common/singleton/manager_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/extensions/access_loggers/stats/scope_provider_singleton.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/thread_factory_for_test.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace StatsAccessLog {
namespace {

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

class ScopeProviderSingletonTest : public testing::Test {
public:
  ScopeProviderSingletonTest() {
    ON_CALL(factory_context_.server_context_, singletonManager())
        .WillByDefault(ReturnRef(singleton_manager_));
    ON_CALL(factory_context_, serverFactoryContext())
        .WillByDefault(ReturnRef(factory_context_.server_context_));
  }

  NiceMock<Stats::MockStore> mock_store_;
  NiceMock<Server::Configuration::MockGenericFactoryContext> factory_context_;
  Singleton::ManagerImpl singleton_manager_;
  std::vector<std::unique_ptr<Stats::StatNameDynamicStorage>> name_storages_;
};

TEST_F(ScopeProviderSingletonTest, GetScopeCachesAndReturnsSameScope) {
  // First request
  envoy::type::v3::Scope scope_config1;
  scope_config1.set_sharing_name("shared_name");
  scope_config1.mutable_max_counters()->set_value(10);
  scope_config1.mutable_max_gauges()->set_value(20);
  scope_config1.mutable_max_histograms()->set_value(30);

  EXPECT_CALL(factory_context_.server_context_, serverScope())
      .WillRepeatedly(ReturnRef(mock_store_.mockScope()));
  EXPECT_CALL(factory_context_, scope()).WillRepeatedly(ReturnRef(mock_store_.mockScope()));
  EXPECT_CALL(factory_context_, statsScope()).WillRepeatedly(ReturnRef(mock_store_.mockScope()));

  std::shared_ptr<Stats::MockScope> newly_created_scope;

  EXPECT_CALL(mock_store_.mockScope(), createScope_(_))
      .WillOnce(Invoke([&](const std::string& name) {
        auto scope_name_storage =
            std::make_unique<Stats::StatNameDynamicStorage>(name, mock_store_.symbolTable());
        newly_created_scope = std::shared_ptr<Stats::MockScope>(
            new NiceMock<Stats::MockScope>(scope_name_storage->statName(), mock_store_));
        return newly_created_scope;
      }));

  auto returned_scope1 = Stats::ScopeProviderSingleton::getScope(factory_context_, scope_config1);
  EXPECT_EQ(returned_scope1, newly_created_scope);
  EXPECT_NE(returned_scope1, nullptr);

  envoy::type::v3::Scope scope_config2;
  scope_config2.set_sharing_name("shared_name");
  scope_config2.mutable_max_counters()->set_value(10);
  scope_config2.mutable_max_gauges()->set_value(20);
  scope_config2.mutable_max_histograms()->set_value(30);

  // Expect no new createScope_ calls here
  auto returned_scope2 = Stats::ScopeProviderSingleton::getScope(factory_context_, scope_config2);
  EXPECT_EQ(returned_scope2, returned_scope1);
  EXPECT_EQ(returned_scope2, newly_created_scope);

  // Third request with different config should create new scope
  envoy::type::v3::Scope scope_config3;
  scope_config3.set_sharing_name("different_limits");
  scope_config3.mutable_max_counters()->set_value(15);
  scope_config3.mutable_max_gauges()->set_value(20);
  scope_config3.mutable_max_histograms()->set_value(30);

  std::shared_ptr<Stats::MockScope> newly_created_scope3;
  EXPECT_CALL(mock_store_.mockScope(), createScope_(_))
      .WillOnce(Invoke([&](const std::string& name) {
        auto scope_name_storage =
            std::make_unique<Stats::StatNameDynamicStorage>(name, mock_store_.symbolTable());
        newly_created_scope3 = std::shared_ptr<Stats::MockScope>(
            new NiceMock<Stats::MockScope>(scope_name_storage->statName(), mock_store_));
        return newly_created_scope3;
      }));

  auto returned_scope3 = Stats::ScopeProviderSingleton::getScope(factory_context_, scope_config3);
  EXPECT_EQ(returned_scope3, newly_created_scope3);
  EXPECT_NE(returned_scope3, returned_scope1);
}

TEST_F(ScopeProviderSingletonTest, CleansUpExpiredScopesOnNextGetScope) {
  envoy::type::v3::Scope scope_config;
  scope_config.set_sharing_name("cleanup_test_name");
  scope_config.mutable_max_counters()->set_value(10);
  scope_config.mutable_max_gauges()->set_value(20);
  scope_config.mutable_max_histograms()->set_value(30);

  EXPECT_CALL(factory_context_.server_context_, serverScope())
      .WillRepeatedly(ReturnRef(mock_store_.mockScope()));
  EXPECT_CALL(factory_context_, scope()).WillRepeatedly(ReturnRef(mock_store_.mockScope()));
  EXPECT_CALL(factory_context_, statsScope()).WillRepeatedly(ReturnRef(mock_store_.mockScope()));

  // First request should trigger createScope_
  EXPECT_CALL(mock_store_.mockScope(), createScope_(_))
      .Times(1)
      .WillOnce(Invoke([&](const std::string& name) {
        auto scope_name_storage =
            std::make_unique<Stats::StatNameDynamicStorage>(name, mock_store_.symbolTable());
        return std::shared_ptr<Stats::Scope>(
            new NiceMock<Stats::MockScope>(scope_name_storage->statName(), mock_store_));
      }));

  auto returned_scope = Stats::ScopeProviderSingleton::getScope(factory_context_, scope_config);
  EXPECT_NE(returned_scope, nullptr);

  // While returned_scope is alive, requesting again should not trigger createScope_
  EXPECT_CALL(mock_store_.mockScope(), createScope_(_)).Times(0);
  auto returned_scope2 = Stats::ScopeProviderSingleton::getScope(factory_context_, scope_config);
  EXPECT_EQ(returned_scope, returned_scope2);

  testing::Mock::VerifyAndClearExpectations(&mock_store_.mockScope());

  // Destroy the scope holders. The weak reference will expire, and next getScope should clean it
  // up.
  returned_scope.reset();
  returned_scope2.reset();

  // Next request should trigger a new createScope_ because it was deleted
  EXPECT_CALL(mock_store_.mockScope(), createScope_(_))
      .Times(1)
      .WillOnce(Invoke([&](const std::string& name) {
        auto scope_name_storage =
            std::make_unique<Stats::StatNameDynamicStorage>(name, mock_store_.symbolTable());
        return std::shared_ptr<Stats::Scope>(
            new NiceMock<Stats::MockScope>(scope_name_storage->statName(), mock_store_));
      }));

  auto returned_scope3 = Stats::ScopeProviderSingleton::getScope(factory_context_, scope_config);
  EXPECT_NE(returned_scope3, nullptr);
}

TEST_F(ScopeProviderSingletonTest, GetScopeWithSharingDisabledDoesNotCache) {
  envoy::type::v3::Scope scope_config;
  // sharing_name is empty by default, which means sharing is disabled.
  scope_config.mutable_max_counters()->set_value(10);

  EXPECT_CALL(factory_context_.server_context_, serverScope())
      .WillRepeatedly(ReturnRef(mock_store_.mockScope()));
  EXPECT_CALL(factory_context_, statsScope()).WillRepeatedly(ReturnRef(mock_store_.mockScope()));

  // First request should create scope
  EXPECT_CALL(mock_store_.mockScope(), createScope_(_))
      .Times(1)
      .WillOnce(Invoke([&](const std::string& name) {
        auto scope_name_storage =
            std::make_unique<Stats::StatNameDynamicStorage>(name, mock_store_.symbolTable());
        return std::shared_ptr<Stats::Scope>(
            new NiceMock<Stats::MockScope>(scope_name_storage->statName(), mock_store_));
      }));

  auto returned_scope1 = Stats::ScopeProviderSingleton::getScope(factory_context_, scope_config);
  EXPECT_NE(returned_scope1, nullptr);

  // Second request should also create scope since sharing is disabled
  EXPECT_CALL(mock_store_.mockScope(), createScope_(_))
      .Times(1)
      .WillOnce(Invoke([&](const std::string& name) {
        auto scope_name_storage =
            std::make_unique<Stats::StatNameDynamicStorage>(name, mock_store_.symbolTable());
        return std::shared_ptr<Stats::Scope>(
            new NiceMock<Stats::MockScope>(scope_name_storage->statName(), mock_store_));
      }));

  auto returned_scope2 = Stats::ScopeProviderSingleton::getScope(factory_context_, scope_config);
  EXPECT_NE(returned_scope2, nullptr);
  EXPECT_NE(returned_scope1, returned_scope2);
}

TEST_F(ScopeProviderSingletonTest, SupportsGetSharedAndCopying) {
  envoy::type::v3::Scope scope_config;
  scope_config.set_sharing_name("copy_test_name");

  EXPECT_CALL(factory_context_.server_context_, serverScope())
      .WillRepeatedly(ReturnRef(mock_store_.mockScope()));
  EXPECT_CALL(factory_context_, scope()).WillRepeatedly(ReturnRef(mock_store_.mockScope()));
  EXPECT_CALL(factory_context_, statsScope()).WillRepeatedly(ReturnRef(mock_store_.mockScope()));
  EXPECT_CALL(factory_context_.server_context_, mainThreadDispatcher())
      .WillRepeatedly(ReturnRef(factory_context_.server_context_.dispatcher_));

  EXPECT_CALL(mock_store_.mockScope(), createScope_(_))
      .WillOnce(Invoke([&](const std::string& name) {
        auto scope_name_storage =
            std::make_unique<Stats::StatNameDynamicStorage>(name, mock_store_.symbolTable());
        return std::shared_ptr<Stats::Scope>(
            new NiceMock<Stats::MockScope>(scope_name_storage->statName(), mock_store_));
      }));

  auto wrapped_scope = Stats::ScopeProviderSingleton::getScope(factory_context_, scope_config);
  EXPECT_NE(wrapped_scope, nullptr);

  // Users can get a shared reference via the interface's `getShared()` (which uses
  // enable_shared_from_this).
  Stats::ScopeSharedPtr shared_copy = wrapped_scope->getShared();
  EXPECT_EQ(wrapped_scope.get(), shared_copy.get());

  // Resetting wrapped_scope should NOT trigger cleanup because shared_copy still holds it.
  EXPECT_CALL(factory_context_.server_context_.dispatcher_, post(_)).Times(0);
  wrapped_scope.reset();
  testing::Mock::VerifyAndClearExpectations(&factory_context_.server_context_.dispatcher_);

  // Resetting shared_copy should trigger cleanup because it's the last reference.
  EXPECT_CALL(factory_context_.server_context_.dispatcher_, post(_));
  shared_copy.reset();
}

} // namespace
} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
