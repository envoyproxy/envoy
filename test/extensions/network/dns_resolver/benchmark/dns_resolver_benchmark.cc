#include "envoy/config/core/v3/address.pb.h"
#include "envoy/extensions/network/dns_resolver/cares/v3/cares_dns_resolver.pb.h"
#include "envoy/extensions/network/dns_resolver/hickory/v3/hickory_dns_resolver.pb.h"
#include "envoy/network/dns.h"

#include "source/common/event/libevent.h"
#include "source/common/network/dns_resolver/dns_factory_util.h"

#include "test/benchmark/main.h"
#include "test/extensions/network/dns_resolver/common/fake_udp_dns_server.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"
#include "fmt/format.h"

namespace Envoy {
namespace {

envoy::config::core::v3::TypedExtensionConfig
createCaresTypedConfig(const std::string& server_address, uint16_t port) {
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
  auto* resolver = cares.add_resolvers();
  auto* addr = resolver->mutable_socket_address();
  addr->set_address(server_address);
  addr->set_port_value(port);

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  typed_config.set_name("envoy.network.dns_resolver.cares");
  typed_config.mutable_typed_config()->PackFrom(cares);
  return typed_config;
}

envoy::config::core::v3::TypedExtensionConfig
createHickoryTypedConfig(const std::string& server_address, uint16_t port) {
  envoy::extensions::network::dns_resolver::hickory::v3::HickoryDnsResolverConfig hickory;
  auto* resolver = hickory.add_resolvers();
  auto* addr = resolver->mutable_socket_address();
  addr->set_address(server_address);
  addr->set_port_value(port);
  // Disable caching for fair comparison with c-ares (which has no built-in cache).
  hickory.mutable_cache_size()->set_value(0);
  // Prevent reading /etc/resolv.conf.
  hickory.mutable_use_system_config()->set_value(false);
  // Single `Tokio` thread to isolate resolver overhead from thread pool scaling.
  hickory.mutable_num_resolver_threads()->set_value(1);

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  typed_config.set_name("envoy.network.dns_resolver.hickory");
  typed_config.mutable_typed_config()->PackFrom(hickory);
  return typed_config;
}

void ensureLibeventInitialized() {
  if (!Event::Libevent::Global::initialized()) {
    Event::Libevent::Global::initialize();
  }
}

Network::DnsResolverSharedPtr
createDnsResolver(Event::Dispatcher& dispatcher, Api::Api& api,
                  const envoy::config::core::v3::TypedExtensionConfig& typed_config) {
  Network::DnsResolverFactory& factory =
      Network::createDnsResolverFactoryFromTypedConfig(typed_config);
  auto resolver_or = factory.createDnsResolver(dispatcher, api, typed_config);
  RELEASE_ASSERT(resolver_or.ok(), std::string(resolver_or.status().message()));
  return std::move(*resolver_or);
}

// ---------------------------------------------------------------------------
// Single Query Latency
// Measures the full round-trip of one DNS resolution from the resolve ->
// fake-server response -> callback.
// ---------------------------------------------------------------------------

static void BM_CaresSingleQueryLatency(::benchmark::State& state) {
  ensureLibeventInitialized();
  Network::Test::FakeUdpDnsServer dns_server;
  dns_server.setDefaultAResponse("1.2.3.4");
  dns_server.start();

  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher = api->allocateDispatcher("cares_bench");
  auto typed_config = createCaresTypedConfig(dns_server.address(), dns_server.port());
  auto resolver = createDnsResolver(*dispatcher, *api, typed_config);

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    bool resolved = false;
    resolver->resolve("benchmark.example.com", Network::DnsLookupFamily::V4Only,
                      [&resolved, &dispatcher](Network::DnsResolver::ResolutionStatus,
                                               absl::string_view /*details*/,
                                               std::list<Network::DnsResponse>&& /*responses*/) {
                        resolved = true;
                        dispatcher->exit();
                      });
    dispatcher->run(Event::Dispatcher::RunType::RunUntilExit);
    RELEASE_ASSERT(resolved, "c-ares DNS resolution did not complete.");
  }

  dns_server.stop();
}

static void BM_HickorySingleQueryLatency(::benchmark::State& state) {
  if (benchmark::skipExpensiveBenchmarks()) {
    state.SkipWithError("Skipping expensive Hickory benchmark.");
    return;
  }

  ensureLibeventInitialized();
  Network::Test::FakeUdpDnsServer dns_server;
  dns_server.setDefaultAResponse("1.2.3.4");
  dns_server.start();

  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher = api->allocateDispatcher("hickory_bench");
  auto typed_config = createHickoryTypedConfig(dns_server.address(), dns_server.port());
  auto resolver = createDnsResolver(*dispatcher, *api, typed_config);

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    bool resolved = false;
    resolver->resolve("benchmark.example.com", Network::DnsLookupFamily::V4Only,
                      [&resolved, &dispatcher](Network::DnsResolver::ResolutionStatus,
                                               absl::string_view /*details*/,
                                               std::list<Network::DnsResponse>&& /*responses*/) {
                        resolved = true;
                        dispatcher->exit();
                      });
    dispatcher->run(Event::Dispatcher::RunType::RunUntilExit);
    RELEASE_ASSERT(resolved, "Hickory DNS resolution did not complete.");
  }

  dns_server.stop();
}

// ---------------------------------------------------------------------------
// Concurrent Query Throughput
// Fires N queries simultaneously and measures the total time until all the
// queries are completed. Reports items/second.
// ---------------------------------------------------------------------------

static void BM_CaresConcurrentQueries(::benchmark::State& state) {
  const int concurrent = static_cast<int>(state.range(0));

  if (benchmark::skipExpensiveBenchmarks() && concurrent > 50) {
    state.SkipWithError("Skipping expensive c-ares concurrent benchmark.");
    return;
  }

  ensureLibeventInitialized();
  Network::Test::FakeUdpDnsServer dns_server;
  dns_server.setDefaultAResponse("1.2.3.4");
  dns_server.start();

  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher = api->allocateDispatcher("cares_bench");
  auto typed_config = createCaresTypedConfig(dns_server.address(), dns_server.port());
  auto resolver = createDnsResolver(*dispatcher, *api, typed_config);

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    int completed = 0;
    for (int i = 0; i < concurrent; i++) {
      resolver->resolve(fmt::format("host{}.example.com", i), Network::DnsLookupFamily::V4Only,
                        [&completed, concurrent, &dispatcher](
                            Network::DnsResolver::ResolutionStatus, absl::string_view /*details*/,
                            std::list<Network::DnsResponse>&& /*responses*/) {
                          if (++completed == concurrent) {
                            dispatcher->exit();
                          }
                        });
    }
    dispatcher->run(Event::Dispatcher::RunType::RunUntilExit);
    RELEASE_ASSERT(completed == concurrent, "Not all c-ares concurrent queries completed.");
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * concurrent);

  dns_server.stop();
}

static void BM_HickoryConcurrentQueries(::benchmark::State& state) {
  const int concurrent = static_cast<int>(state.range(0));

  if (benchmark::skipExpensiveBenchmarks() && concurrent > 10) {
    state.SkipWithError("Skipping expensive Hickory concurrent benchmark.");
    return;
  }

  ensureLibeventInitialized();
  Network::Test::FakeUdpDnsServer dns_server;
  dns_server.setDefaultAResponse("1.2.3.4");
  dns_server.start();

  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher = api->allocateDispatcher("hickory_bench");
  auto typed_config = createHickoryTypedConfig(dns_server.address(), dns_server.port());
  auto resolver = createDnsResolver(*dispatcher, *api, typed_config);

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    int completed = 0;
    for (int i = 0; i < concurrent; i++) {
      resolver->resolve(fmt::format("host{}.example.com", i), Network::DnsLookupFamily::V4Only,
                        [&completed, concurrent, &dispatcher](
                            Network::DnsResolver::ResolutionStatus, absl::string_view /*details*/,
                            std::list<Network::DnsResponse>&& /*responses*/) {
                          if (++completed == concurrent) {
                            dispatcher->exit();
                          }
                        });
    }
    dispatcher->run(Event::Dispatcher::RunType::RunUntilExit);
    RELEASE_ASSERT(completed == concurrent, "Not all Hickory concurrent queries completed.");
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * concurrent);

  dns_server.stop();
}

// ---------------------------------------------------------------------------
// Resolver Creation Time
// Measures the cost of constructing a resolver instance through the factory.
// It is relevant for dynamic cluster creation.
// ---------------------------------------------------------------------------

static void BM_CaresResolverCreation(::benchmark::State& state) {
  ensureLibeventInitialized();
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher = api->allocateDispatcher("cares_bench");
  auto typed_config = createCaresTypedConfig("127.0.0.1", 53);

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    auto resolver = createDnsResolver(*dispatcher, *api, typed_config);
    ::benchmark::DoNotOptimize(resolver.get());
  }
}

static void BM_HickoryResolverCreation(::benchmark::State& state) {
  if (benchmark::skipExpensiveBenchmarks()) {
    state.SkipWithError("Skipping expensive Hickory resolver creation benchmark.");
    return;
  }

  ensureLibeventInitialized();
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher = api->allocateDispatcher("hickory_bench");
  auto typed_config = createHickoryTypedConfig("127.0.0.1", 53);

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    auto resolver = createDnsResolver(*dispatcher, *api, typed_config);
    ::benchmark::DoNotOptimize(resolver.get());
  }
}

// --- Registration ---

BENCHMARK(BM_CaresSingleQueryLatency)->Unit(::benchmark::kMicrosecond);
BENCHMARK(BM_HickorySingleQueryLatency)->Unit(::benchmark::kMicrosecond);

BENCHMARK(BM_CaresConcurrentQueries)
    ->Arg(1)
    ->Arg(10)
    ->Arg(50)
    ->Arg(100)
    ->Arg(500)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK(BM_HickoryConcurrentQueries)
    ->Arg(1)
    ->Arg(10)
    ->Arg(50)
    ->Arg(100)
    ->Arg(500)
    ->Unit(::benchmark::kMicrosecond);

BENCHMARK(BM_CaresResolverCreation)->Unit(::benchmark::kMicrosecond);
BENCHMARK(BM_HickoryResolverCreation)->Unit(::benchmark::kMicrosecond);

} // namespace
} // namespace Envoy
