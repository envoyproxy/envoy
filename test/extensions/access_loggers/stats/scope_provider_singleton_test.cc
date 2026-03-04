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
  }

  NiceMock<Stats::MockStore> mock_store_;
  NiceMock<Server::Configuration::MockGenericFactoryContext> factory_context_;
  Singleton::ManagerImpl singleton_manager_;
  std::shared_ptr<Stats::ScopeProviderSingleton> provider_;
};

TEST_F(ScopeProviderSingletonTest, GetScopeCachesAndReturnsSameScope) {
  // First request
  envoy::type::v3::Scope scope_settings1;
  scope_settings1.set_name("my_dynamic_scope");
  scope_settings1.mutable_settings()->set_max_counters(10);
  scope_settings1.mutable_settings()->set_max_gauges(20);
  scope_settings1.mutable_settings()->set_max_histograms(30);

  EXPECT_CALL(factory_context_.server_context_, scope())
      .WillRepeatedly(ReturnRef(mock_store_.mockScope()));
  EXPECT_CALL(factory_context_, scope()).WillRepeatedly(ReturnRef(mock_store_.mockScope()));

  std::shared_ptr<Stats::MockScope> newly_created_scope;

  EXPECT_CALL(mock_store_.mockScope(), createScope_(_))
      .WillOnce(Invoke([&](const std::string& name) {
        auto scope_name_storage =
            std::make_unique<Stats::StatNameDynamicStorage>(name, mock_store_.symbolTable());
        newly_created_scope = std::make_shared<NiceMock<Stats::MockScope>>(
            scope_name_storage->statName(), mock_store_);
        return newly_created_scope;
      }));

  auto returned_scope1 = Stats::ScopeProviderSingleton::getScope(factory_context_, scope_settings1);
  provider_ =
      factory_context_.server_context_.singletonManager().getTyped<Stats::ScopeProviderSingleton>(
          "scope_provider_singleton_name");
  EXPECT_EQ(returned_scope1, newly_created_scope);
  EXPECT_NE(returned_scope1, nullptr);

  // Second request with exact same config should hit the cache
  envoy::type::v3::Scope scope_settings2;
  scope_settings2.set_name("my_dynamic_scope");
  scope_settings2.mutable_settings()->set_max_counters(10);
  scope_settings2.mutable_settings()->set_max_gauges(20);
  scope_settings2.mutable_settings()->set_max_histograms(30);

  // Expect no new createScope_ calls here
  auto returned_scope2 = Stats::ScopeProviderSingleton::getScope(factory_context_, scope_settings2);
  EXPECT_EQ(returned_scope2, returned_scope1);
  EXPECT_EQ(returned_scope2, newly_created_scope);

  // Third request with different config should create new scope
  envoy::type::v3::Scope scope_settings3;
  scope_settings3.set_name("my_other_dynamic_scope");
  scope_settings3.mutable_settings()->set_max_counters(15);
  scope_settings3.mutable_settings()->set_max_gauges(20);
  scope_settings3.mutable_settings()->set_max_histograms(30);

  std::shared_ptr<Stats::MockScope> newly_created_scope3;
  EXPECT_CALL(mock_store_.mockScope(), createScope_(_))
      .WillOnce(Invoke([&](const std::string& name) {
        auto scope_name_storage =
            std::make_unique<Stats::StatNameDynamicStorage>(name, mock_store_.symbolTable());
        newly_created_scope3 = std::make_shared<NiceMock<Stats::MockScope>>(
            scope_name_storage->statName(), mock_store_);
        return newly_created_scope3;
      }));

  auto returned_scope3 = Stats::ScopeProviderSingleton::getScope(factory_context_, scope_settings3);
  EXPECT_EQ(returned_scope3, newly_created_scope3);
  EXPECT_NE(returned_scope3, returned_scope1);
}

} // namespace
} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
