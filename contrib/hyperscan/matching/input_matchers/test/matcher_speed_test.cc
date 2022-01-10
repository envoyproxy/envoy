// Note: this should be run with --compilation_mode=opt, and would benefit from
// a quiescent system with disabled cstate power management.

#include "source/common/common/assert.h"
#include "source/common/common/regex.h"

#include "benchmark/benchmark.h"
#include "contrib/hyperscan/matching/input_matchers/source/matcher.h"

namespace Envoy {

static const std::vector<std::string> ClusterInputs = {
    "cluster.no_trailing_dot",
    "cluster.match.",
    "cluster.match.normal",
    "cluster.match.and.a.whole.lot.of.things.coming.after.the.matches.really.too.much.stuff",
};

static const std::string ClusterRePattern = "^cluster\\.((.*?)\\.)";

static void BM_CompiledGoogleReMatcher(benchmark::State& state) {
  envoy::type::matcher::v3::RegexMatcher config;
  config.mutable_google_re2();
  config.set_regex(ClusterRePattern);
  const auto matcher = Regex::CompiledGoogleReMatcher(config);
  uint32_t passes = 0;
  for (auto _ : state) {
    for (const std::string& cluster_input : ClusterInputs) {
      if (matcher.match(cluster_input)) {
        ++passes;
      }
    }
  }
  RELEASE_ASSERT(passes > 0, "");
}
BENCHMARK(BM_CompiledGoogleReMatcher);

static void BM_HyperscanMatcher(benchmark::State& state) {
  envoy::extensions::matching::input_matchers::hyperscan::v3alpha::Hyperscan config;
  auto regex = config.add_regexes();
  regex->set_regex(ClusterRePattern);
  auto matcher = Extensions::Matching::InputMatchers::Hyperscan::Matcher(config);
  uint32_t passes = 0;
  for (auto _ : state) {
    for (const std::string& cluster_input : ClusterInputs) {
      if (matcher.match(cluster_input)) {
        ++passes;
      }
    }
  }
  RELEASE_ASSERT(passes > 0, "");
}
BENCHMARK(BM_HyperscanMatcher);

} // namespace Envoy
