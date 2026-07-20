#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/stats/allocator.h"
#include "source/common/stats/stats_matcher_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/thread_local_store.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"
#include "source/server/generic_factory_context.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {
namespace {

envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig
makeConfig(absl::string_view name) {
  envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config;
  config.set_name(name);
  config.set_dns_lookup_family(envoy::config::cluster::v3::Cluster::V4_ONLY);
  return config;
}

class DnsCacheManagerImplTest : public testing::Test {
public:
  DnsCacheManagerImplTest() : registered_dns_factory_(dns_resolver_factory_) {}

  void SetUp() override {
    // Route the server scope to the production-like store so stats behavior (including
    // StatsMatcher handling) matches the real deployment.
    ON_CALL(server_context_, serverScope()).WillByDefault(ReturnRef(*store_.rootScope()));
    EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
        .WillRepeatedly(Return(resolver_));
  }

  // store_ must outlive server_context_: the singleton manager (owned by the mock)
  // holds DnsCaches whose scopes unregister from the store during teardown.
  Stats::SymbolTableImpl symbol_table_;
  Stats::Allocator alloc_{symbol_table_};
  Stats::ThreadLocalStoreImpl store_{alloc_};
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  std::shared_ptr<Network::MockDnsResolver> resolver_{std::make_shared<Network::MockDnsResolver>()};
  NiceMock<Network::MockDnsResolverFactory> dns_resolver_factory_;
  Registry::InjectFactory<Network::DnsResolverFactory> registered_dns_factory_;
};

TEST_F(DnsCacheManagerImplTest, CacheMissAfterFirstCallerScopeDestroyed) {
  DnsCacheManagerSharedPtr manager;
  {
    // Simulates a filter-chain-owned scope; destroyed when the filter chain goes away.
    Stats::ScopeSharedPtr filter_chain_scope = store_.rootScope()->createScope("filter_chain_a.");
    Server::GenericFactoryContextImpl context_a(server_context_, *filter_chain_scope,
                                                ProtobufMessage::getNullValidationVisitor(),
                                                &server_context_.init_manager_);
    DnsCacheManagerFactoryImpl factory(server_context_);
    manager = factory.get();

    auto cache_or = manager->getCache(context_a.messageValidationVisitor(), makeConfig("cache_a"));
    THROW_IF_NOT_OK_REF(cache_or.status());
    EXPECT_NE(nullptr, cache_or.value());
  }

  // The second caller arrives after the first caller's scope is gone, with its own
  // live scope (e.g. a new listener's filter chain).
  Stats::ScopeSharedPtr filter_chain_scope_b = store_.rootScope()->createScope("filter_chain_b.");
  Server::GenericFactoryContextImpl context_b(server_context_, *filter_chain_scope_b,
                                              ProtobufMessage::getNullValidationVisitor(),
                                              &server_context_.init_manager_);

  // New cache name forces a cache miss; the DnsCacheImpl is created from the server
  // context only, so the destroyed caller scope plays no role.
  auto miss_or = manager->getCache(context_b.messageValidationVisitor(), makeConfig("cache_b"));
  THROW_IF_NOT_OK_REF(miss_or.status());
  EXPECT_NE(nullptr, miss_or.value());
}

// DNS cache stats live under the server scope, so a per-listener StatsMatcher cannot
// filter them.
TEST_F(DnsCacheManagerImplTest, ListenerStatsMatcherDoesNotFilterDnsCacheStats) {
  envoy::config::metrics::v3::StatsMatcher matcher_proto;
  matcher_proto.set_reject_all(true);
  auto listener_matcher =
      std::make_shared<Stats::StatsMatcherImpl>(matcher_proto, symbol_table_, server_context_);
  Stats::ScopeSharedPtr listener_scope =
      store_.rootScope()->createScope("listener.", false, {}, listener_matcher);

  // The matcher is active: stats under the listener scope are dropped.
  listener_scope->counterFromString("dropped");
  EXPECT_EQ(nullptr, TestUtility::findCounter(store_, "listener.dropped"));

  DnsCacheManagerFactoryImpl factory(server_context_);
  auto manager = factory.get();
  auto cache_or =
      manager->getCache(ProtobufMessage::getNullValidationVisitor(), makeConfig("cache_a"));
  THROW_IF_NOT_OK_REF(cache_or.status());

  EXPECT_NE(nullptr, TestUtility::findCounter(store_, "dns_cache.cache_a.dns_query_attempt"));
}

// DNS cache stats are governed by the store-level (bootstrap) StatsMatcher.
TEST_F(DnsCacheManagerImplTest, GlobalStatsMatcherFiltersDnsCacheStats) {
  envoy::config::metrics::v3::StatsMatcher matcher_proto;
  matcher_proto.mutable_exclusion_list()->add_patterns()->set_prefix("dns_cache.");
  store_.setStatsMatcher(
      std::make_unique<Stats::StatsMatcherImpl>(matcher_proto, symbol_table_, server_context_));

  DnsCacheManagerFactoryImpl factory(server_context_);
  auto manager = factory.get();
  auto cache_or =
      manager->getCache(ProtobufMessage::getNullValidationVisitor(), makeConfig("cache_a"));
  THROW_IF_NOT_OK_REF(cache_or.status());

  EXPECT_EQ(nullptr, TestUtility::findCounter(store_, "dns_cache.cache_a.dns_query_attempt"));

  // Stats outside the exclusion list are still created.
  store_.rootScope()->counterFromString("kept");
  EXPECT_NE(nullptr, TestUtility::findCounter(store_, "kept"));
}

} // namespace
} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
