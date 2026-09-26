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

} // namespace
} // namespace Router
} // namespace Envoy
