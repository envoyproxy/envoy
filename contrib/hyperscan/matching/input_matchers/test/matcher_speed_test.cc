// Note: this should be run with --compilation_mode=opt, and would benefit from
// a quiescent system with disabled cstate power management.

#include "source/common/common/assert.h"
#include "source/common/common/regex.h"
#include "source/common/thread_local/thread_local_impl.h"

#include "benchmark/benchmark.h"
#include "contrib/hyperscan/matching/input_matchers/source/matcher.h"

namespace Envoy {

const std::vector<std::string>& clusterInputs() {
  CONSTRUCT_ON_FIRST_USE(
      std::vector<std::string>,
      {
          "cluster.no_trailing_dot",
          "cluster.match.",
          "cluster.match.normal",
          "cluster.match.and.a.whole.lot.of.things.coming.after.the.matches.really.too.much.stuff",
      });
}

constexpr absl::string_view ClusterRePattern = "^cluster\\.((.*?)\\.)";

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_CompiledGoogleReMatcher(benchmark::State& state) {
  envoy::type::matcher::v3::RegexMatcher config;
  config.mutable_google_re2();
  config.set_regex(std::string(ClusterRePattern));
  const auto matcher = Regex::CompiledGoogleReMatcher(config);
  uint32_t passes = 0;
  for (auto _ : state) { // NOLINT
    for (const std::string& cluster_input : clusterInputs()) {
      if (matcher.match(cluster_input)) {
        ++passes;
      }
    }
  }
  RELEASE_ASSERT(passes > 0, "");
}
BENCHMARK(BM_CompiledGoogleReMatcher);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_HyperscanMatcher(benchmark::State& state) {
  auto instance = ThreadLocal::InstanceImpl();
  auto matcher = Extensions::Matching::InputMatchers::Hyperscan::Matcher(
      {std::string(ClusterRePattern).c_str()}, {0}, {0}, instance, false);
  uint32_t passes = 0;
  for (auto _ : state) { // NOLINT
    for (const std::string& cluster_input : clusterInputs()) {
      if (matcher.match(cluster_input)) {
        ++passes;
      }
    }
  }
  RELEASE_ASSERT(passes > 0, "");
}
BENCHMARK(BM_HyperscanMatcher);

} // namespace Envoy
