#include "envoy/extensions/network/dns_resolver/hickory/v3/hickory_dns_resolver.pb.h"

#include "source/common/network/dns_resolver/dns_factory_util.h"
#include "source/extensions/network/dns_resolver/hickory/hickory_dns_impl.h"

#include "test/mocks/api/mocks.h"
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
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

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
  auto* query = resolver_->resolve(
      "this-domain-does-not-exist-at-all.invalid", DnsLookupFamily::All,
      [this, &callback_called](DnsResolver::ResolutionStatus status, absl::string_view,
                               std::list<DnsResponse>&& response) {
        callback_called = true;
        // Non-existent domain should fail or return empty results.
        if (status == DnsResolver::ResolutionStatus::Failure) {
          EXPECT_TRUE(response.empty());
        }
        dispatcher_->exit();
      });

  EXPECT_NE(query, nullptr);
  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  EXPECT_TRUE(callback_called);
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

  // Call onResolveComplete with a query ID that does not exist in pending_queries_.
  // This exercises the early return on line 164-166.
  auto* hickory_resolver = dynamic_cast<HickoryDnsResolver*>(resolver_.get());
  ASSERT_NE(hickory_resolver, nullptr);

  // This should silently return without crashing.
  hickory_resolver->onResolveComplete(
      999999, envoy_dynamic_module_type_dns_resolution_status_Completed, "should_be_ignored", {});
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
