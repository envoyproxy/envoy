// Note: this should be run with --compilation_mode=opt, and would benefit from
// a quiescent system with disabled cstate power management.

#include <regex>

#include "source/common/common/assert.h"

#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "re2/re2.h"

// NOLINT(namespace-envoy)

static const char* ClusterInputs[] = {
    "cluster.no_trailing_dot",
    "cluster.match.",
    "cluster.match.normal",
    "cluster.match.and.a.whole.lot.of.things.coming.after.the.matches.really.too.much.stuff",
};

static const char ClusterRePattern[] = "^cluster\\.((.*?)\\.)";
static const char ClusterReAltPattern[] = "^cluster\\.(([^\\.]+)\\.).*";

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StdRegex(benchmark::State& state) {
  std::regex re(ClusterRePattern);
  uint32_t passes = 0;
  std::vector<std::string> inputs;
  for (const char* cluster_input : ClusterInputs) {
    inputs.push_back(cluster_input);
  }

  for (auto _ : state) { // NOLINT
    for (const std::string& cluster_input : inputs) {
      std::smatch match;
      if (std::regex_search(cluster_input, match, re)) {
        ASSERT(match.size() >= 3);
        ASSERT(match[1] == "match.");
        ASSERT(match[2] == "match");
        ++passes;
      }
    }
  }
  RELEASE_ASSERT(passes > 0, "");
}
BENCHMARK(BM_StdRegex);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StdRegexStringView(benchmark::State& state) {
  std::regex re(ClusterRePattern);
  std::vector<absl::string_view> inputs;
  for (const char* cluster_input : ClusterInputs) {
    inputs.push_back(cluster_input);
  }
  uint32_t passes = 0;
  for (auto _ : state) { // NOLINT
    for (absl::string_view cluster_input : inputs) {
      std::match_results<absl::string_view::iterator> smatch;
      if (std::regex_search(cluster_input.begin(), cluster_input.end(), smatch, re)) {
        ASSERT(smatch.size() >= 3);
        ASSERT(smatch[1] == "match.");
        ASSERT(smatch[2] == "match");
        ++passes;
      }
    }
  }
  RELEASE_ASSERT(passes > 0, "");
}
BENCHMARK(BM_StdRegexStringView);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_StdRegexStringViewAltPattern(benchmark::State& state) {
  std::regex re(ClusterReAltPattern);
  std::vector<absl::string_view> inputs;
  for (const char* cluster_input : ClusterInputs) {
    inputs.push_back(cluster_input);
  }
  uint32_t passes = 0;
  for (auto _ : state) { // NOLINT
    for (absl::string_view cluster_input : inputs) {
      std::match_results<absl::string_view::iterator> smatch;
      if (std::regex_search(cluster_input.begin(), cluster_input.end(), smatch, re)) {
        ASSERT(smatch.size() >= 3);
        ASSERT(smatch[1] == "match.");
        ASSERT(smatch[2] == "match");
        ++passes;
      }
    }
  }
  RELEASE_ASSERT(passes > 0, "");
}
BENCHMARK(BM_StdRegexStringViewAltPattern);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_RE2(benchmark::State& state) {
  re2::RE2 re(ClusterRePattern);
  uint32_t passes = 0;
  for (auto _ : state) { // NOLINT
    for (const char* cluster_input : ClusterInputs) {
      absl::string_view match1, match2;
      if (re2::RE2::PartialMatch(cluster_input, re, &match1, &match2)) {
        ASSERT(match1 == "match.");
        ASSERT(match2 == "match");
        ++passes;
      }
    }
  }
  RELEASE_ASSERT(passes > 0, "");
}
BENCHMARK(BM_RE2);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_RE2_AltPattern(benchmark::State& state) {
  re2::RE2 re(ClusterReAltPattern);
  uint32_t passes = 0;
  for (auto _ : state) { // NOLINT
    for (const char* cluster_input : ClusterInputs) {
      absl::string_view match1, match2;
      if (re2::RE2::PartialMatch(cluster_input, re, &match1, &match2)) {
        ASSERT(match1 == "match.");
        ASSERT(match2 == "match");
        ++passes;
      }
    }
  }
  RELEASE_ASSERT(passes > 0, "");
}
BENCHMARK(BM_RE2_AltPattern);
