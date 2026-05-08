#include "envoy/extensions/network/dns_resolver/hickory/v3/hickory_dns_resolver.pb.h"

#include "source/common/network/dns_resolver/dns_factory_util.h"
#include "source/extensions/network/dns_resolver/hickory/hickory_dns_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/test_runtime.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace {

// These tests exercise the C++ shell of the Hickory DNS resolver extension by directly
// testing the factory, resolver, pending resolution, and ABI callback logic using the
// statically linked Rust module.

class HickoryDnsImplTest : public testing::Test {
public:
  HickoryDnsImplTest()
      : api_(Api::createApiForTest(stats_store_)),
        dispatcher_(api_->allocateDispatcher("test_thread")) {}

  void
  initialize(const envoy::extensions::network::dns_resolver::hickory::v3::HickoryDnsResolverConfig&
                 config = {}) {
    envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
    typed_dns_resolver_config.mutable_typed_config()->PackFrom(config);
    typed_dns_resolver_config.set_name(std::string("envoy.network.dns_resolver.hickory"));

    Network::DnsResolverFactory& dns_resolver_factory =
        createDnsResolverFactoryFromTypedConfig(typed_dns_resolver_config);
    auto resolver_or =
        dns_resolver_factory.createDnsResolver(*dispatcher_, *api_, typed_dns_resolver_config);
    ASSERT_TRUE(resolver_or.ok()) << resolver_or.status().message();
    resolver_ = std::move(*resolver_or);
  }

  void checkStats(uint64_t resolve_total, uint64_t pending_resolutions, uint64_t not_found,
                  uint64_t get_addr_failure, uint64_t timeouts) {
    EXPECT_EQ(resolve_total, stats_store_.counter("dns.hickory.resolve_total").value());
    EXPECT_EQ(
        pending_resolutions,
        stats_store_.gauge("dns.hickory.pending_resolutions", Stats::Gauge::ImportMode::NeverImport)
            .value());
    EXPECT_EQ(not_found, stats_store_.counter("dns.hickory.not_found").value());
    EXPECT_EQ(get_addr_failure, stats_store_.counter("dns.hickory.get_addr_failure").value());
    EXPECT_EQ(timeouts, stats_store_.counter("dns.hickory.timeouts").value());
  }

  Stats::TestUtil::TestStore stats_store_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  DnsResolverSharedPtr resolver_;
  TestScopedRuntime scoped_runtime_;
};

TEST_F(HickoryDnsImplTest, FactoryRegistration) {
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  envoy::extensions::network::dns_resolver::hickory::v3::HickoryDnsResolverConfig config;
  typed_dns_resolver_config.mutable_typed_config()->PackFrom(config);
  typed_dns_resolver_config.set_name(std::string("envoy.network.dns_resolver.hickory"));

  Network::DnsResolverFactory& factory =
      createDnsResolverFactoryFromTypedConfig(typed_dns_resolver_config);
  EXPECT_EQ(factory.name(), "envoy.network.dns_resolver.hickory");
  EXPECT_NE(factory.createEmptyConfigProto(), nullptr);
}

TEST_F(HickoryDnsImplTest, CreateResolverDefaultConfig) {
  initialize();
  EXPECT_NE(resolver_, nullptr);
}

TEST_F(HickoryDnsImplTest, CreateResolverWithExplicitNameservers) {
  envoy::extensions::network::dns_resolver::hickory::v3::HickoryDnsResolverConfig config;
  auto* resolver_addr = config.add_resolvers();
  auto* sa = resolver_addr->mutable_socket_address();
  sa->set_address("8.8.8.8");
  sa->set_port_value(53);

  initialize(config);
  EXPECT_NE(resolver_, nullptr);
}

TEST_F(HickoryDnsImplTest, CreateResolverWithCustomOptions) {
  envoy::extensions::network::dns_resolver::hickory::v3::HickoryDnsResolverConfig config;
  config.mutable_cache_size()->set_value(2048);
  config.mutable_num_resolver_threads()->set_value(4);
  config.mutable_query_timeout()->set_seconds(10);
  config.mutable_query_tries()->set_value(5);
  config.set_enable_dnssec(true);

  initialize(config);
  EXPECT_NE(resolver_, nullptr);
}

TEST_F(HickoryDnsImplTest, ResolveLocalhost) {
  initialize();
  checkStats(0, 0, 0, 0, 0);

  bool callback_called = false;
  auto* query =
      resolver_->resolve("localhost", DnsLookupFamily::All,
                         [this, &callback_called](DnsResolver::ResolutionStatus status,
                                                  absl::string_view, std::list<DnsResponse>&&) {
                           callback_called = true;
                           EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
                           dispatcher_->exit();
                         });

  EXPECT_NE(query, nullptr);
  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  EXPECT_TRUE(callback_called);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_F(HickoryDnsImplTest, ResolveLocalhostV4Only) {
  initialize();

  bool callback_called = false;
  auto* query = resolver_->resolve("localhost", DnsLookupFamily::V4Only,
                                   [this, &callback_called](DnsResolver::ResolutionStatus status,
                                                            absl::string_view,
                                                            std::list<DnsResponse>&& response) {
                                     callback_called = true;
                                     EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
                                     for (const auto& resp : response) {
                                       EXPECT_NE(resp.addrInfo().address_->ip()->ipv4(), nullptr);
                                     }
                                     dispatcher_->exit();
                                   });

  EXPECT_NE(query, nullptr);
  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  EXPECT_TRUE(callback_called);
}

TEST_F(HickoryDnsImplTest, ResolveLocalhostV6Only) {
  initialize();

  bool callback_called = false;
  auto* query = resolver_->resolve("localhost", DnsLookupFamily::V6Only,
                                   [this, &callback_called](DnsResolver::ResolutionStatus status,
                                                            absl::string_view,
                                                            std::list<DnsResponse>&& response) {
                                     callback_called = true;
                                     EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
                                     for (const auto& resp : response) {
                                       EXPECT_NE(resp.addrInfo().address_->ip()->ipv6(), nullptr);
                                     }
                                     dispatcher_->exit();
                                   });

  EXPECT_NE(query, nullptr);
  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  EXPECT_TRUE(callback_called);
}

TEST_F(HickoryDnsImplTest, ResolveLocalhostAuto) {
  initialize();

  bool callback_called = false;
  auto* query =
      resolver_->resolve("localhost", DnsLookupFamily::Auto,
                         [this, &callback_called](DnsResolver::ResolutionStatus status,
                                                  absl::string_view, std::list<DnsResponse>&&) {
                           callback_called = true;
                           EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
                           dispatcher_->exit();
                         });

  EXPECT_NE(query, nullptr);
  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  EXPECT_TRUE(callback_called);
}

TEST_F(HickoryDnsImplTest, ResolveLocalhostV4Preferred) {
  initialize();

  bool callback_called = false;
  auto* query =
      resolver_->resolve("localhost", DnsLookupFamily::V4Preferred,
                         [this, &callback_called](DnsResolver::ResolutionStatus status,
                                                  absl::string_view, std::list<DnsResponse>&&) {
                           callback_called = true;
                           EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
                           dispatcher_->exit();
                         });

  EXPECT_NE(query, nullptr);
  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  EXPECT_TRUE(callback_called);
}

TEST_F(HickoryDnsImplTest, ResolveNonExistentDomain) {
  initialize();

  bool callback_called = false;
  DnsResolver::ResolutionStatus result_status;
  auto* query =
      resolver_->resolve("this-domain-does-not-exist-at-all.invalid", DnsLookupFamily::All,
                         [this, &callback_called,
                          &result_status](DnsResolver::ResolutionStatus status, absl::string_view,
                                          std::list<DnsResponse>&& response) {
                           callback_called = true;
                           result_status = status;
                           if (status == DnsResolver::ResolutionStatus::Failure) {
                             EXPECT_TRUE(response.empty());
                           }
                           dispatcher_->exit();
                         });

  EXPECT_NE(query, nullptr);
  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  EXPECT_TRUE(callback_called);
  // The query completed, so resolve_total should be incremented and pending_resolutions
  // should be back to zero.
  EXPECT_EQ(1, stats_store_.counter("dns.hickory.resolve_total").value());
  EXPECT_EQ(0, stats_store_
                   .gauge("dns.hickory.pending_resolutions", Stats::Gauge::ImportMode::NeverImport)
                   .value());
  // On failure, exactly one error category stat should be incremented.
  if (result_status == DnsResolver::ResolutionStatus::Failure) {
    const uint64_t total_errors = stats_store_.counter("dns.hickory.not_found").value() +
                                  stats_store_.counter("dns.hickory.get_addr_failure").value() +
                                  stats_store_.counter("dns.hickory.timeouts").value();
    EXPECT_EQ(1, total_errors);
  }
}

TEST_F(HickoryDnsImplTest, CancelQuery) {
  initialize();

  auto* query = resolver_->resolve(
      "localhost", DnsLookupFamily::All,
      [](DnsResolver::ResolutionStatus, absl::string_view, std::list<DnsResponse>&&) {
        // Should not be called after cancel.
      });

  EXPECT_NE(query, nullptr);
  query->cancel(ActiveDnsQuery::CancelReason::QueryAbandoned);

  // Resolve a second query to verify the resolver is still functional after cancel.
  bool second_callback_called = false;
  auto* query2 = resolver_->resolve(
      "localhost", DnsLookupFamily::All,
      [this, &second_callback_called](DnsResolver::ResolutionStatus status, absl::string_view,
                                      std::list<DnsResponse>&&) {
        second_callback_called = true;
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
        dispatcher_->exit();
      });

  EXPECT_NE(query2, nullptr);
  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  EXPECT_TRUE(second_callback_called);
}

TEST_F(HickoryDnsImplTest, ResetNetworking) {
  initialize();

  // resetNetworking should not crash. It clears the resolver cache.
  resolver_->resetNetworking();

  // Verify the resolver still works after reset.
  bool callback_called = false;
  resolver_->resolve("localhost", DnsLookupFamily::All,
                     [this, &callback_called](DnsResolver::ResolutionStatus status,
                                              absl::string_view, std::list<DnsResponse>&&) {
                       callback_called = true;
                       EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
                       dispatcher_->exit();
                     });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  EXPECT_TRUE(callback_called);
}

TEST_F(HickoryDnsImplTest, MultipleSimultaneousQueries) {
  initialize();

  int callbacks_received = 0;
  constexpr int num_queries = 5;

  for (int i = 0; i < num_queries; i++) {
    resolver_->resolve("localhost", DnsLookupFamily::All,
                       [this, &callbacks_received](DnsResolver::ResolutionStatus status,
                                                   absl::string_view, std::list<DnsResponse>&&) {
                         EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
                         callbacks_received++;
                         if (callbacks_received == num_queries) {
                           dispatcher_->exit();
                         }
                       });
  }

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  EXPECT_EQ(callbacks_received, num_queries);
  checkStats(num_queries /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_F(HickoryDnsImplTest, ActiveDnsQueryTracing) {
  initialize();

  auto* query = resolver_->resolve("localhost", DnsLookupFamily::All,
                                   [this](DnsResolver::ResolutionStatus, absl::string_view,
                                          std::list<DnsResponse>&&) { dispatcher_->exit(); });

  // The Hickory resolver does not support tracing, so these should be no-ops.
  EXPECT_NE(query, nullptr);
  query->addTrace(0);
  EXPECT_TRUE(query->getTraces().empty());

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

// -- Tests for the ABI callback and internal error paths --

TEST_F(HickoryDnsImplTest, OnResolveCompleteWithUnknownQueryId) {
  initialize();
  checkStats(0, 0, 0, 0, 0);

  // Call onResolveComplete with a query ID that does not exist in pending_queries_.
  // This exercises the early return in onResolveComplete.
  auto* hickory_resolver = dynamic_cast<HickoryDnsResolver*>(resolver_.get());
  ASSERT_NE(hickory_resolver, nullptr);

  // This should silently return without crashing or affecting stats.
  hickory_resolver->onResolveComplete(
      999999, envoy_dynamic_module_type_dns_resolution_status_Completed, "should_be_ignored", {});
  checkStats(0, 0, 0, 0, 0);
}

TEST_F(HickoryDnsImplTest, AbiCallbackWithNullAddress) {
  initialize();

  auto* hickory_resolver = dynamic_cast<HickoryDnsResolver*>(resolver_.get());
  ASSERT_NE(hickory_resolver, nullptr);

  envoy_dynamic_module_type_dns_address null_addr;
  null_addr.address_ptr = nullptr;
  null_addr.address_length = 0;
  null_addr.ttl_seconds = 60;

  envoy_dynamic_module_type_module_buffer details_buf;
  details_buf.ptr = nullptr;
  details_buf.length = 0;

  // Call the ABI callback directly with a null address entry. The query ID does not match
  // any pending query, so onResolveComplete will early-return after the post.
  envoy_dynamic_module_callback_dns_resolve_complete(
      static_cast<const void*>(hickory_resolver), 0,
      envoy_dynamic_module_type_dns_resolution_status_Completed, details_buf, &null_addr, 1);

  // Run the dispatcher to process the posted lambda.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(HickoryDnsImplTest, AbiCallbackWithEmptyAddress) {
  initialize();
  auto* hickory_resolver = dynamic_cast<HickoryDnsResolver*>(resolver_.get());
  ASSERT_NE(hickory_resolver, nullptr);

  // Address with valid pointer but zero length should also be skipped.
  const char* dummy = "1.2.3.4:0";
  envoy_dynamic_module_type_dns_address empty_addr;
  empty_addr.address_ptr = dummy;
  empty_addr.address_length = 0;
  empty_addr.ttl_seconds = 60;

  envoy_dynamic_module_type_module_buffer details_buf;
  details_buf.ptr = nullptr;
  details_buf.length = 0;

  envoy_dynamic_module_callback_dns_resolve_complete(
      static_cast<const void*>(hickory_resolver), 0,
      envoy_dynamic_module_type_dns_resolution_status_Completed, details_buf, &empty_addr, 1);

  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(HickoryDnsImplTest, AbiCallbackWithInvalidAddress) {
  initialize();
  auto* hickory_resolver = dynamic_cast<HickoryDnsResolver*>(resolver_.get());
  ASSERT_NE(hickory_resolver, nullptr);

  // An address string that cannot be parsed should be silently skipped.
  const std::string bad_addr = "not-an-address";
  envoy_dynamic_module_type_dns_address invalid_addr;
  invalid_addr.address_ptr = bad_addr.c_str();
  invalid_addr.address_length = bad_addr.size();
  invalid_addr.ttl_seconds = 60;

  envoy_dynamic_module_type_module_buffer details_buf;
  details_buf.ptr = nullptr;
  details_buf.length = 0;

  envoy_dynamic_module_callback_dns_resolve_complete(
      static_cast<const void*>(hickory_resolver), 0,
      envoy_dynamic_module_type_dns_resolution_status_Completed, details_buf, &invalid_addr, 1);

  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(HickoryDnsImplTest, CancelledQueryResultIsDropped) {
  initialize();

  // Issue a resolve to a domain that takes some time, then cancel it.
  bool callback_called = false;
  auto* query =
      resolver_->resolve("localhost", DnsLookupFamily::All,
                         [&callback_called](DnsResolver::ResolutionStatus, absl::string_view,
                                            std::list<DnsResponse>&&) { callback_called = true; });

  EXPECT_NE(query, nullptr);
  query->cancel(ActiveDnsQuery::CancelReason::QueryAbandoned);

  // Issue a second query and wait for it to complete. By the time it finishes, the
  // first query's async result has also arrived and been dropped by the dispatcher.
  bool second_callback_called = false;
  resolver_->resolve("localhost", DnsLookupFamily::All,
                     [this, &second_callback_called](DnsResolver::ResolutionStatus,
                                                     absl::string_view, std::list<DnsResponse>&&) {
                       second_callback_called = true;
                       dispatcher_->exit();
                     });

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  EXPECT_TRUE(second_callback_called);
  EXPECT_FALSE(callback_called);
}

TEST_F(HickoryDnsImplTest, OnResolveCompleteForCancelledQuery) {
  initialize();
  auto* hickory_resolver = dynamic_cast<HickoryDnsResolver*>(resolver_.get());
  ASSERT_NE(hickory_resolver, nullptr);

  // Issue a query and immediately cancel it. The query remains in pending_queries_ with
  // cancelled_ set to true.
  bool callback_called = false;
  auto* query =
      resolver_->resolve("localhost", DnsLookupFamily::All,
                         [&callback_called](DnsResolver::ResolutionStatus, absl::string_view,
                                            std::list<DnsResponse>&&) { callback_called = true; });
  EXPECT_NE(query, nullptr);
  query->cancel(ActiveDnsQuery::CancelReason::QueryAbandoned);

  // Simulate the `Tokio` task delivering a result for the cancelled query. The query ID
  // is 1 (first query issued on a fresh resolver). This exercises the cancelled path in
  // onResolveComplete where the result is dropped without invoking the callback.
  hickory_resolver->onResolveComplete(1, envoy_dynamic_module_type_dns_resolution_status_Completed,
                                      "resolved", {});

  EXPECT_FALSE(callback_called);
}

TEST_F(HickoryDnsImplTest, DestroyWithPendingQueries) {
  initialize();

  // Issue multiple queries and then destroy the resolver without waiting for results.
  // This exercises the destructor cleanup path.
  for (int i = 0; i < 3; i++) {
    resolver_->resolve(
        "localhost", DnsLookupFamily::All,
        [](DnsResolver::ResolutionStatus, absl::string_view, std::list<DnsResponse>&&) {});
  }

  // Destroy the resolver immediately. The destructor should cancel all pending queries
  // and shut down the `Tokio` runtime cleanly.
  resolver_.reset();
}

TEST_F(HickoryDnsImplTest, StatsNotFoundOnDirectCallback) {
  initialize();
  auto* hickory_resolver = dynamic_cast<HickoryDnsResolver*>(resolver_.get());
  ASSERT_NE(hickory_resolver, nullptr);

  bool callback_called = false;
  resolver_->resolve("localhost", DnsLookupFamily::All,
                     [this, &callback_called](DnsResolver::ResolutionStatus, absl::string_view,
                                              std::list<DnsResponse>&&) {
                       callback_called = true;
                       dispatcher_->exit();
                     });
  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  EXPECT_TRUE(callback_called);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);

  // Simulate a not-found resolution by directly calling onResolveComplete with a "no record
  // found" detail string matching the pattern produced by the Hickory Rust module.
  callback_called = false;
  auto* query2 =
      resolver_->resolve("example.invalid", DnsLookupFamily::All,
                         [this, &callback_called](DnsResolver::ResolutionStatus status,
                                                  absl::string_view, std::list<DnsResponse>&&) {
                           callback_called = true;
                           EXPECT_EQ(status, DnsResolver::ResolutionStatus::Failure);
                           dispatcher_->exit();
                         });
  EXPECT_NE(query2, nullptr);
  // Override the async resolution by posting a direct onResolveComplete. Query ID is 2
  // (second query on this resolver).
  hickory_resolver->onResolveComplete(
      2, envoy_dynamic_module_type_dns_resolution_status_Failure,
      "A lookup failed: no record found for Query { name: Name(\"example.invalid.\"), "
      "query_type: A, query_class: IN }",
      {});
  EXPECT_TRUE(callback_called);
  checkStats(2 /*resolve_total*/, 0 /*pending_resolutions*/, 1 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_F(HickoryDnsImplTest, StatsTimeoutOnDirectCallback) {
  initialize();
  auto* hickory_resolver = dynamic_cast<HickoryDnsResolver*>(resolver_.get());
  ASSERT_NE(hickory_resolver, nullptr);

  bool callback_called = false;
  auto* query =
      resolver_->resolve("timeout.example", DnsLookupFamily::All,
                         [this, &callback_called](DnsResolver::ResolutionStatus status,
                                                  absl::string_view, std::list<DnsResponse>&&) {
                           callback_called = true;
                           EXPECT_EQ(status, DnsResolver::ResolutionStatus::Failure);
                           dispatcher_->exit();
                         });
  EXPECT_NE(query, nullptr);
  hickory_resolver->onResolveComplete(
      1, envoy_dynamic_module_type_dns_resolution_status_Failure,
      "A lookup failed: DNS request timed out for Name(\"timeout.example.\")", {});
  EXPECT_TRUE(callback_called);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 1 /*timeouts*/);
}

TEST_F(HickoryDnsImplTest, StatsGetAddrFailureOnDirectCallback) {
  initialize();
  auto* hickory_resolver = dynamic_cast<HickoryDnsResolver*>(resolver_.get());
  ASSERT_NE(hickory_resolver, nullptr);

  bool callback_called = false;
  auto* query =
      resolver_->resolve("fail.example", DnsLookupFamily::All,
                         [this, &callback_called](DnsResolver::ResolutionStatus status,
                                                  absl::string_view, std::list<DnsResponse>&&) {
                           callback_called = true;
                           EXPECT_EQ(status, DnsResolver::ResolutionStatus::Failure);
                           dispatcher_->exit();
                         });
  EXPECT_NE(query, nullptr);
  hickory_resolver->onResolveComplete(1, envoy_dynamic_module_type_dns_resolution_status_Failure,
                                      "A lookup failed: connection refused", {});
  EXPECT_TRUE(callback_called);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             1 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_F(HickoryDnsImplTest, StatsPendingResolutionsGauge) {
  initialize();
  checkStats(0, 0, 0, 0, 0);

  // Issue a query. The pending_resolutions gauge is incremented synchronously in resolve().
  resolver_->resolve("localhost", DnsLookupFamily::All,
                     [this](DnsResolver::ResolutionStatus, absl::string_view,
                            std::list<DnsResponse>&&) { dispatcher_->exit(); });

  // After resolve() but before the async callback, the gauge should reflect at least 1
  // pending resolution. However, since the `Tokio` task may complete extremely quickly, we
  // verify the gauge is correct after the dispatcher runs.
  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  checkStats(1 /*resolve_total*/, 0 /*pending_resolutions*/, 0 /*not_found*/,
             0 /*get_addr_failure*/, 0 /*timeouts*/);
}

TEST_F(HickoryDnsImplTest, StatsOnCancelledQuery) {
  initialize();

  auto* query = resolver_->resolve(
      "localhost", DnsLookupFamily::All,
      [](DnsResolver::ResolutionStatus, absl::string_view, std::list<DnsResponse>&&) {});
  EXPECT_NE(query, nullptr);
  query->cancel(ActiveDnsQuery::CancelReason::QueryAbandoned);

  // Resolve a second query and wait for it to complete.
  bool second_callback_called = false;
  resolver_->resolve("localhost", DnsLookupFamily::All,
                     [this, &second_callback_called](DnsResolver::ResolutionStatus,
                                                     absl::string_view, std::list<DnsResponse>&&) {
                       second_callback_called = true;
                       dispatcher_->exit();
                     });
  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  EXPECT_TRUE(second_callback_called);

  // The second query is guaranteed to complete. The cancelled query's `Tokio` task may or may
  // not have posted a result before seeing the cancel flag, so resolve_total is at least 1.
  EXPECT_GE(stats_store_.counter("dns.hickory.resolve_total").value(), 1);
  EXPECT_EQ(0, stats_store_.counter("dns.hickory.not_found").value());
  EXPECT_EQ(0, stats_store_.counter("dns.hickory.get_addr_failure").value());
  EXPECT_EQ(0, stats_store_.counter("dns.hickory.timeouts").value());

  // Destroying the resolver ensures that any remaining pending queries have their
  // pending_resolutions gauge decremented in the destructor.
  resolver_.reset();
  EXPECT_EQ(0, stats_store_
                   .gauge("dns.hickory.pending_resolutions", Stats::Gauge::ImportMode::NeverImport)
                   .value());
}

TEST_F(HickoryDnsImplTest, ResolveIpAddressV4) {
  initialize();

  bool callback_called = false;
  auto* query = resolver_->resolve(
      "127.0.0.1", DnsLookupFamily::All,
      [this, &callback_called](DnsResolver::ResolutionStatus status, absl::string_view,
                               std::list<DnsResponse>&& response) {
        callback_called = true;
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
        EXPECT_FALSE(response.empty());
        if (!response.empty()) {
          EXPECT_NE(response.front().addrInfo().address_->ip()->ipv4(), nullptr);
        }
        dispatcher_->exit();
      });

  EXPECT_NE(query, nullptr);
  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  EXPECT_TRUE(callback_called);
}

TEST_F(HickoryDnsImplTest, ResolveIpAddressV6) {
  initialize();

  bool callback_called = false;
  auto* query = resolver_->resolve(
      "::1", DnsLookupFamily::All,
      [this, &callback_called](DnsResolver::ResolutionStatus status, absl::string_view,
                               std::list<DnsResponse>&& response) {
        callback_called = true;
        EXPECT_EQ(status, DnsResolver::ResolutionStatus::Completed);
        EXPECT_FALSE(response.empty());
        if (!response.empty()) {
          EXPECT_NE(response.front().addrInfo().address_->ip()->ipv6(), nullptr);
        }
        dispatcher_->exit();
      });

  EXPECT_NE(query, nullptr);
  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  EXPECT_TRUE(callback_called);
}

} // namespace
} // namespace Network
} // namespace Envoy
