#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/router/scoped_config_impl.h"

#include "test/mocks/router/mocks.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"

namespace Envoy {
namespace Router {
namespace {

using envoy::extensions::filters::network::http_connection_manager::v3::ScopedRoutes;
using testing::NiceMock;

// Benchmark ScopeKey creation and fragment hashing via addFragment.
static void bmScopeKeyAddFragment(benchmark::State& state) {
  const int num_fragments = state.range(0);
  const std::string fragment_str = "scope_fragment_test_value";
  for (auto _ : state) {
    ScopeKey key;
    for (int i = 0; i < num_fragments; ++i) {
      key.addFragment(std::make_unique<StringKeyFragment>(fragment_str));
    }
    benchmark::DoNotOptimize(key.hash());
  }
}
BENCHMARK(bmScopeKeyAddFragment)->DenseRange(1, 8, 1);

// Benchmark ScopeKeyBuilder computing scope key with index-based header extraction.
static void bmScopeKeyBuilderByIndex(benchmark::State& state) {
  const int num_fragments = state.range(0);
  ScopedRoutes::ScopeKeyBuilder config;
  Http::TestRequestHeaderMapImpl headers;
  for (int i = 0; i < num_fragments; ++i) {
    auto* fragment_builder = config.add_fragments()->mutable_header_value_extractor();
    const std::string header_name = absl::StrCat("header_", i);
    fragment_builder->set_name(header_name);
    fragment_builder->set_element_separator(",");
    fragment_builder->set_index(1);
    headers.addCopy(Http::LowerCaseString(header_name), "val0,target_val_1,val2");
  }
  ScopeKeyBuilderImpl builder(std::move(config));
  for (auto _ : state) {
    ScopeKeyPtr key = builder.computeScopeKey(headers);
    benchmark::DoNotOptimize(key);
  }
}
BENCHMARK(bmScopeKeyBuilderByIndex)->DenseRange(1, 8, 1);

// Benchmark ScopeKeyBuilder computing scope key with key-value header extraction.
static void bmScopeKeyBuilderByKey(benchmark::State& state) {
  const int num_fragments = state.range(0);
  ScopedRoutes::ScopeKeyBuilder config;
  Http::TestRequestHeaderMapImpl headers;
  for (int i = 0; i < num_fragments; ++i) {
    auto* fragment_builder = config.add_fragments()->mutable_header_value_extractor();
    const std::string header_name = absl::StrCat("header_", i);
    fragment_builder->set_name(header_name);
    fragment_builder->set_element_separator(";");
    auto* element = fragment_builder->mutable_element();
    element->set_key("target_key");
    element->set_separator("=");
    headers.addCopy(Http::LowerCaseString(header_name),
                    "foo=bar;target_key=scope_value_123;baz=qux");
  }
  ScopeKeyBuilderImpl builder(std::move(config));
  for (auto _ : state) {
    ScopeKeyPtr key = builder.computeScopeKey(headers);
    benchmark::DoNotOptimize(key);
  }
}
BENCHMARK(bmScopeKeyBuilderByKey)->DenseRange(1, 8, 1);

// Benchmark ScopedConfigImpl route config lookup by ScopeKey with varying numbers of scopes.
static void bmScopedConfigGetRouteConfig(benchmark::State& state) {
  const int num_scopes = state.range(0);
  std::vector<ScopedRouteInfoConstSharedPtr> scopes;
  scopes.reserve(num_scopes);
  for (int i = 0; i < num_scopes; ++i) {
    envoy::config::route::v3::ScopedRouteConfiguration scoped_route;
    scoped_route.set_name(absl::StrCat("scope_", i));
    scoped_route.set_route_configuration_name(absl::StrCat("route_", i));
    auto* fragment = scoped_route.mutable_key()->add_fragments();
    fragment->set_string_key(absl::StrCat("key_", i));
    auto route_config = std::make_shared<NiceMock<MockConfig>>();
    scopes.push_back(
        std::make_shared<ScopedRouteInfo>(std::move(scoped_route), std::move(route_config)));
  }
  ScopedConfigImpl scoped_config(scopes);

  ScopeKeyPtr key_ptr = std::make_unique<ScopeKey>();
  key_ptr->addFragment(std::make_unique<StringKeyFragment>(absl::StrCat("key_", num_scopes / 2)));

  for (auto _ : state) {
    ConfigConstSharedPtr route = scoped_config.getRouteConfig(key_ptr);
    benchmark::DoNotOptimize(route);
  }
}
BENCHMARK(bmScopedConfigGetRouteConfig)->RangeMultiplier(10)->Range(10, 1000);

} // namespace
} // namespace Router
} // namespace Envoy
