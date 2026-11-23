#include "source/common/http/http_server_properties_cache_manager_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/thread_local/mocks.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Http {
namespace {

// Measures the lookup of an existing cache, which the alternate protocols cache filter makes for
// every Alt-Svc response from a cluster with alternate protocols cache options. With an argument
// of 1, the lookup passes equivalent options that serialize differently.
// NOLINTNEXTLINE(readability-identifier-naming)
void BM_GetExistingCache(benchmark::State& state) {
  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  testing::NiceMock<ThreadLocal::MockInstance> tls;
  testing::NiceMock<Event::MockDispatcher> dispatcher;
  HttpServerPropertiesCacheManagerImpl manager(context.server_factory_context_,
                                               context.messageValidationVisitor(), tls);
  envoy::config::core::v3::AlternateProtocolsCacheOptions options;
  options.set_name("default_alternate_protocols_cache");
  options.mutable_max_entries()->set_value(1024);
  options.add_canonical_suffixes(".example.com");
  const HttpServerPropertiesCacheSharedPtr cache = manager.getCache(options, dispatcher);
  envoy::config::core::v3::AlternateProtocolsCacheOptions lookup_options = options;
  if (state.range(0) == 1) {
    lookup_options.mutable_key_value_store_config();
  }

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    RELEASE_ASSERT(manager.getCache(lookup_options, dispatcher) == cache, "");
  }
}
BENCHMARK(BM_GetExistingCache)->Arg(0)->Arg(1);

} // namespace
} // namespace Http
} // namespace Envoy
