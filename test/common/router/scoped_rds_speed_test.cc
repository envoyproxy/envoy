#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/common/assert.h"
#include "source/common/router/scoped_config_impl.h"

#include "test/mocks/router/mocks.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Router {
namespace {

using envoy::config::route::v3::ScopedRouteConfiguration;
using envoy::extensions::filters::network::http_connection_manager::v3::ScopedRoutes;
using testing::NiceMock;

static ScopedRouteConfiguration genScopedRoute(int i) {
  ScopedRouteConfiguration config;
  config.set_name(absl::StrCat("scope_", i));
  config.set_route_configuration_name(absl::StrCat("route_config_", i));
  auto* key = config.mutable_key();
  key->add_fragments()->set_string_key(absl::StrCat("10.0.", (i / 256) % 256, ".", i % 256));
  return config;
}

static ScopeKeyPtr genScopeKey(int i) {
  auto key = std::make_unique<ScopeKey>();
  key->addFragment(
      std::make_unique<StringKeyFragment>(absl::StrCat("10.0.", (i / 256) % 256, ".", i % 256)));
  return key;
}

static std::vector<ScopedRouteInfoConstSharedPtr> genScopedRouteInfos(int n) {
  std::vector<ScopedRouteInfoConstSharedPtr> scopes;
  scopes.reserve(n);
  auto mock_route_config = std::make_shared<NiceMock<MockConfig>>();
  for (int i = 0; i < n; ++i) {
    scopes.push_back(std::make_shared<ScopedRouteInfo>(genScopedRoute(i), mock_route_config));
  }
  return scopes;
}

// Benchmark scope lookup time via getRouteConfig in ScopedConfigImpl across varying scope count N.
static void bmScopeLookup(benchmark::State& state) {
  const int n = state.range(0);
  auto scopes = genScopedRouteInfos(n);
  ScopedConfigImpl config(scopes);

  std::vector<ScopeKeyPtr> keys;
  keys.reserve(n);
  for (int i = 0; i < n; ++i) {
    keys.push_back(genScopeKey(i));
  }

  size_t idx = 0;
  for (auto _ : state) { // NOLINT
    auto res = config.getRouteConfig(keys[idx]);
    benchmark::DoNotOptimize(res);
    if (++idx == keys.size()) {
      idx = 0;
    }
  }
}

// Benchmark bulk insertion / creation of ScopedConfigImpl with N scopes.
static void bmScopeAddAll(benchmark::State& state) {
  const int n = state.range(0);
  auto scopes = genScopedRouteInfos(n);

  for (auto _ : state) { // NOLINT
    ScopedConfigImpl config;
    config.addOrUpdateRoutingScopes(scopes);
    benchmark::DoNotOptimize(config);
  }
}

// Benchmark incremental update of a batch of scopes in a map of size N.
static void bmScopeIncrementalUpdate(benchmark::State& state) {
  const int n = state.range(0);
  auto scopes = genScopedRouteInfos(n);
  ScopedConfigImpl config(scopes);

  // Update batch: update 16 existing scopes.
  const int batch_size = 16;
  auto update_batch = genScopedRouteInfos(batch_size);

  for (auto _ : state) { // NOLINT
    config.addOrUpdateRoutingScopes(update_batch);
  }
}

// --- ScopedRouteMap Baseline (std::map) vs Optimized (absl::flat_hash_map + heterogeneous) ---

using BaselineScopedRouteMap = std::map<std::string, ScopedRouteInfoConstSharedPtr>;

// Baseline ScopedRouteMap lookup: std::map lookup requiring std::string copy from string_view.
static void bmBaselineScopedRouteMapLookup(benchmark::State& state) {
  const int n = state.range(0);
  auto scopes = genScopedRouteInfos(n);
  BaselineScopedRouteMap map;
  std::vector<std::string> scope_names;
  scope_names.reserve(n);
  for (const auto& scope : scopes) {
    map[scope->scopeName()] = scope;
    scope_names.push_back(scope->scopeName());
  }

  size_t idx = 0;
  for (auto _ : state) { // NOLINT
    absl::string_view query_name = scope_names[idx];
    auto it = map.find(std::string(query_name));
    benchmark::DoNotOptimize(it);
    if (++idx == scope_names.size()) {
      idx = 0;
    }
  }
}

// Optimized ScopedRouteMap lookup: absl::flat_hash_map with heterogeneous string_view lookup.
static void bmOptimizedScopedRouteMapLookup(benchmark::State& state) {
  const int n = state.range(0);
  auto scopes = genScopedRouteInfos(n);
  ScopedRouteMap map;
  std::vector<std::string> scope_names;
  scope_names.reserve(n);
  for (const auto& scope : scopes) {
    map[scope->scopeName()] = scope;
    scope_names.push_back(scope->scopeName());
  }

  size_t idx = 0;
  for (auto _ : state) { // NOLINT
    absl::string_view query_name = scope_names[idx];
    auto it = map.find(query_name);
    benchmark::DoNotOptimize(it);
    if (++idx == scope_names.size()) {
      idx = 0;
    }
  }
}

// Baseline ScopedRouteMap bulk insertion (std::map).
static void bmBaselineScopedRouteMapAddAll(benchmark::State& state) {
  const int n = state.range(0);
  auto scopes = genScopedRouteInfos(n);

  for (auto _ : state) { // NOLINT
    BaselineScopedRouteMap map;
    for (const auto& scope : scopes) {
      map[scope->scopeName()] = scope;
    }
    benchmark::DoNotOptimize(map);
  }
}

// Optimized ScopedRouteMap bulk insertion (absl::flat_hash_map).
static void bmOptimizedScopedRouteMapAddAll(benchmark::State& state) {
  const int n = state.range(0);
  auto scopes = genScopedRouteInfos(n);

  for (auto _ : state) { // NOLINT
    ScopedRouteMap map;
    for (const auto& scope : scopes) {
      map[scope->scopeName()] = scope;
    }
    benchmark::DoNotOptimize(map);
  }
}

// Baseline ScopedRouteMap incremental update: updating 16 items in std::map of size N.
static void bmBaselineScopedRouteMapIncrementalUpdate(benchmark::State& state) {
  const int n = state.range(0);
  auto scopes = genScopedRouteInfos(n);
  BaselineScopedRouteMap map;
  for (const auto& scope : scopes) {
    map[scope->scopeName()] = scope;
  }
  const int batch_size = 16;
  auto update_batch = genScopedRouteInfos(batch_size);

  for (auto _ : state) { // NOLINT
    for (const auto& scope : update_batch) {
      map[scope->scopeName()] = scope;
    }
    benchmark::DoNotOptimize(map);
  }
}

// Optimized ScopedRouteMap incremental update: updating 16 items in absl::flat_hash_map of size N.
static void bmOptimizedScopedRouteMapIncrementalUpdate(benchmark::State& state) {
  const int n = state.range(0);
  auto scopes = genScopedRouteInfos(n);
  ScopedRouteMap map;
  for (const auto& scope : scopes) {
    map[scope->scopeName()] = scope;
  }
  const int batch_size = 16;
  auto update_batch = genScopedRouteInfos(batch_size);

  for (auto _ : state) { // NOLINT
    for (const auto& scope : update_batch) {
      map[scope->scopeName()] = scope;
    }
    benchmark::DoNotOptimize(map);
  }
}

// --- addOrUpdateScopes Full Workflow Emulation ---

// Baseline addOrUpdateScopes: std::map for scoped_route_map_ and route_provider_by_scope_
// with std::string scope_name copy and non-heterogeneous lookup.
static void bmBaselineAddOrUpdateScopes(benchmark::State& state) {
  const int n = state.range(0);
  std::vector<ScopedRouteConfiguration> configs;
  configs.reserve(n);
  for (int i = 0; i < n; ++i) {
    configs.push_back(genScopedRoute(i));
  }
  auto mock_route_config = std::make_shared<NiceMock<MockConfig>>();

  for (auto _ : state) { // NOLINT
    BaselineScopedRouteMap scoped_route_map;
    std::map<std::string, ScopedRouteInfoConstSharedPtr> route_provider_by_scope;
    absl::flat_hash_map<uint64_t, std::string> scope_name_by_hash;

    for (const auto& scoped_route_config : configs) {
      const std::string scope_name = scoped_route_config.name();
      auto scope_info_iter = scoped_route_map.find(scope_name);
      if (scope_info_iter != scoped_route_map.end() &&
          scope_info_iter->second->configHash() == MessageUtil::hash(scoped_route_config)) {
        continue;
      }
      auto scoped_route_info = std::make_shared<ScopedRouteInfo>(
          ScopedRouteConfiguration(scoped_route_config), mock_route_config);
      route_provider_by_scope[scope_name] = scoped_route_info;
      scope_name_by_hash[scoped_route_info->scopeKey().hash()] = scoped_route_info->scopeName();
      scoped_route_map[scoped_route_info->scopeName()] = scoped_route_info;
    }
    benchmark::DoNotOptimize(scoped_route_map);
    benchmark::DoNotOptimize(route_provider_by_scope);
    benchmark::DoNotOptimize(scope_name_by_hash);
  }
}

// Optimized addOrUpdateScopes: absl::flat_hash_map with HeterogeneousStringHash
// and absl::string_view scope_name avoiding string allocation.
static void bmOptimizedAddOrUpdateScopes(benchmark::State& state) {
  const int n = state.range(0);
  std::vector<ScopedRouteConfiguration> configs;
  configs.reserve(n);
  for (int i = 0; i < n; ++i) {
    configs.push_back(genScopedRoute(i));
  }
  auto mock_route_config = std::make_shared<NiceMock<MockConfig>>();

  for (auto _ : state) { // NOLINT
    ScopedRouteMap scoped_route_map;
    absl::flat_hash_map<std::string, ScopedRouteInfoConstSharedPtr, HeterogeneousStringHash,
                        HeterogeneousStringEqual>
        route_provider_by_scope;
    absl::flat_hash_map<uint64_t, std::string> scope_name_by_hash;

    for (const auto& scoped_route_config : configs) {
      const absl::string_view scope_name = scoped_route_config.name();
      auto scope_info_iter = scoped_route_map.find(scope_name);
      if (scope_info_iter != scoped_route_map.end() &&
          scope_info_iter->second->configHash() == MessageUtil::hash(scoped_route_config)) {
        continue;
      }
      auto scoped_route_info = std::make_shared<ScopedRouteInfo>(
          ScopedRouteConfiguration(scoped_route_config), mock_route_config);
      route_provider_by_scope[scoped_route_info->scopeName()] = scoped_route_info;
      scope_name_by_hash[scoped_route_info->scopeKey().hash()] = scoped_route_info->scopeName();
      scoped_route_map[scoped_route_info->scopeName()] = scoped_route_info;
    }
    benchmark::DoNotOptimize(scoped_route_map);
    benchmark::DoNotOptimize(route_provider_by_scope);
    benchmark::DoNotOptimize(scope_name_by_hash);
  }
}

// Benchmark computing ScopeKey from request headers using ScopeKeyBuilderImpl.
static void bmComputeScopeKey(benchmark::State& state) {
  ScopedRoutes::ScopeKeyBuilder config;
  auto* fragment_builder = config.add_fragments();
  auto* header_extractor = fragment_builder->mutable_header_value_extractor();
  header_extractor->set_name("x-scope-header");
  header_extractor->set_index(0);

  ScopeKeyBuilderImpl builder(std::move(config));
  Http::TestRequestHeaderMapImpl headers{{"x-scope-header", "10.0.0.1"}};

  for (auto _ : state) { // NOLINT
    auto scope_key = builder.computeScopeKey(headers);
    benchmark::DoNotOptimize(scope_key);
  }
}

BENCHMARK(bmScopeLookup)->RangeMultiplier(4)->Range(16, 4096);
BENCHMARK(bmScopeAddAll)->RangeMultiplier(4)->Range(16, 4096);
BENCHMARK(bmScopeIncrementalUpdate)->RangeMultiplier(4)->Range(16, 4096);

BENCHMARK(bmBaselineScopedRouteMapLookup)->RangeMultiplier(4)->Range(16, 4096);
BENCHMARK(bmOptimizedScopedRouteMapLookup)->RangeMultiplier(4)->Range(16, 4096);

BENCHMARK(bmBaselineScopedRouteMapAddAll)->RangeMultiplier(4)->Range(16, 4096);
BENCHMARK(bmOptimizedScopedRouteMapAddAll)->RangeMultiplier(4)->Range(16, 4096);

BENCHMARK(bmBaselineScopedRouteMapIncrementalUpdate)->RangeMultiplier(4)->Range(16, 4096);
BENCHMARK(bmOptimizedScopedRouteMapIncrementalUpdate)->RangeMultiplier(4)->Range(16, 4096);

BENCHMARK(bmBaselineAddOrUpdateScopes)->RangeMultiplier(4)->Range(16, 4096);
BENCHMARK(bmOptimizedAddOrUpdateScopes)->RangeMultiplier(4)->Range(16, 4096);

BENCHMARK(bmComputeScopeKey);

} // namespace
} // namespace Router
} // namespace Envoy
