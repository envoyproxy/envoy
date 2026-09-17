#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace {

constexpr std::array<absl::string_view, 7> kRequestDirectives = {
    "no-cache", "no-store", "no-transform", "only-if-cached", "max-age", "min-fresh", "max-stale"};
constexpr std::array<absl::string_view, 9> kResponseDirectives = {
    "no-cache",     "must-revalidate", "proxy-revalidate", "no-store", "private",
    "no-transform", "public",          "s-maxage",         "max-age"};

constexpr unsigned int caseInsensitiveHash(absl::string_view directive) {
  constexpr unsigned int kOffsetBasis = 2166136261u;
  constexpr unsigned int kPrime = 16777619u;

  unsigned int hash = kOffsetBasis;
  for (const char c : directive) {
    const char lowercase_c = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
    hash ^= static_cast<unsigned char>(lowercase_c);
    hash *= kPrime;
  }
  return hash;
}

constexpr unsigned int operator""_case_insensitive_hash(const char* directive, std::size_t length) {
  return caseInsensitiveHash(absl::string_view(directive, length));
}

template <size_t ValidDirectiveCount>
std::vector<std::string>
makeDirectiveCorpus(const std::array<absl::string_view, ValidDirectiveCount>& valid_directives) {
  constexpr size_t kValidDirectiveCount = 63;
  constexpr size_t kUnknownDirectiveCount = 7;
  static_assert(kValidDirectiveCount % ValidDirectiveCount == 0);

  std::vector<std::string> corpus;
  corpus.reserve(kValidDirectiveCount + kUnknownDirectiveCount);
  for (size_t i = 0; i < kValidDirectiveCount; ++i) {
    corpus.emplace_back(valid_directives[i % ValidDirectiveCount]);
  }

  std::mt19937 generator(1); // Fixed seed for repeatability.
  std::uniform_int_distribution<size_t> length_distribution(1, 8);
  std::uniform_int_distribution<int> character_distribution('a', 'z');
  for (size_t i = 0; i < kUnknownDirectiveCount; ++i) {
    std::string unknown_directive;
    do {
      unknown_directive.clear();
      const size_t length = length_distribution(generator);
      unknown_directive.reserve(length);
      for (size_t j = 0; j < length; ++j) {
        unknown_directive.push_back(static_cast<char>(character_distribution(generator)));
      }
    } while (std::any_of(valid_directives.begin(), valid_directives.end(),
                         [&unknown_directive](const absl::string_view valid_directive) {
                           return absl::EqualsIgnoreCase(unknown_directive, valid_directive);
                         }));
    corpus.push_back(std::move(unknown_directive));
  }

  std::shuffle(corpus.begin(), corpus.end(), generator);
  return corpus;
}

const std::vector<std::string>& requestDirectiveCorpus() {
  static const std::vector<std::string> corpus = makeDirectiveCorpus(kRequestDirectives);
  return corpus;
}

const std::vector<std::string>& responseDirectiveCorpus() {
  static const std::vector<std::string> corpus = makeDirectiveCorpus(kResponseDirectives);
  return corpus;
}

int requestDirectiveWithSwitch(absl::string_view directive) {
  switch (caseInsensitiveHash(directive)) {
  case "no-cache"_case_insensitive_hash:
    return 1;
  case "no-store"_case_insensitive_hash:
    return 2;
  case "no-transform"_case_insensitive_hash:
    return 3;
  case "only-if-cached"_case_insensitive_hash:
    return 4;
  case "max-age"_case_insensitive_hash:
    return 5;
  case "min-fresh"_case_insensitive_hash:
    return 6;
  case "max-stale"_case_insensitive_hash:
    return 7;
  default:
    return 0;
  }
}

int responseDirectiveWithSwitch(absl::string_view directive) {
  switch (caseInsensitiveHash(directive)) {
  case "no-cache"_case_insensitive_hash:
    return 1;
  case "must-revalidate"_case_insensitive_hash:
    return 2;
  case "proxy-revalidate"_case_insensitive_hash:
    return 3;
  case "no-store"_case_insensitive_hash:
    return 4;
  case "private"_case_insensitive_hash:
    return 5;
  case "no-transform"_case_insensitive_hash:
    return 6;
  case "public"_case_insensitive_hash:
    return 7;
  case "s-maxage"_case_insensitive_hash:
    return 8;
  case "max-age"_case_insensitive_hash:
    return 9;
  default:
    return 0;
  }
}

int requestDirectiveWithPreTransformedStringEquality(absl::string_view directive) {
  const std::string lowercase_directive = absl::AsciiStrToLower(directive);
  if (lowercase_directive == "no-cache") {
    return 1;
  } else if (lowercase_directive == "no-store") {
    return 2;
  } else if (lowercase_directive == "no-transform") {
    return 3;
  } else if (lowercase_directive == "only-if-cached") {
    return 4;
  } else if (lowercase_directive == "max-age") {
    return 5;
  } else if (lowercase_directive == "min-fresh") {
    return 6;
  } else if (lowercase_directive == "max-stale") {
    return 7;
  }
  return 0;
}

int responseDirectiveWithPreTransformedStringEquality(absl::string_view directive) {
  const std::string lowercase_directive = absl::AsciiStrToLower(directive);
  if (lowercase_directive == "no-cache") {
    return 1;
  } else if (lowercase_directive == "must-revalidate") {
    return 2;
  } else if (lowercase_directive == "proxy-revalidate") {
    return 3;
  } else if (lowercase_directive == "no-store") {
    return 4;
  } else if (lowercase_directive == "private") {
    return 5;
  } else if (lowercase_directive == "no-transform") {
    return 6;
  } else if (lowercase_directive == "public") {
    return 7;
  } else if (lowercase_directive == "s-maxage") {
    return 8;
  } else if (lowercase_directive == "max-age") {
    return 9;
  }
  return 0;
}

int requestDirectiveWithEqualsIgnoreCase(absl::string_view directive) {
  if (absl::EqualsIgnoreCase(directive, "no-cache")) {
    return 1;
  } else if (absl::EqualsIgnoreCase(directive, "no-store")) {
    return 2;
  } else if (absl::EqualsIgnoreCase(directive, "no-transform")) {
    return 3;
  } else if (absl::EqualsIgnoreCase(directive, "only-if-cached")) {
    return 4;
  } else if (absl::EqualsIgnoreCase(directive, "max-age")) {
    return 5;
  } else if (absl::EqualsIgnoreCase(directive, "min-fresh")) {
    return 6;
  } else if (absl::EqualsIgnoreCase(directive, "max-stale")) {
    return 7;
  }
  return 0;
}

int responseDirectiveWithEqualsIgnoreCase(absl::string_view directive) {
  if (absl::EqualsIgnoreCase(directive, "no-cache")) {
    return 1;
  } else if (absl::EqualsIgnoreCase(directive, "must-revalidate")) {
    return 2;
  } else if (absl::EqualsIgnoreCase(directive, "proxy-revalidate")) {
    return 3;
  } else if (absl::EqualsIgnoreCase(directive, "no-store")) {
    return 4;
  } else if (absl::EqualsIgnoreCase(directive, "private")) {
    return 5;
  } else if (absl::EqualsIgnoreCase(directive, "no-transform")) {
    return 6;
  } else if (absl::EqualsIgnoreCase(directive, "public")) {
    return 7;
  } else if (absl::EqualsIgnoreCase(directive, "s-maxage")) {
    return 8;
  } else if (absl::EqualsIgnoreCase(directive, "max-age")) {
    return 9;
  }
  return 0;
}

template <typename Matcher>
void runDirectiveBenchmark(benchmark::State& state, const std::vector<std::string>& corpus,
                           Matcher matcher) {
  uint64_t matches = 0;
  for (auto _ : state) {
    for (const std::string& directive : corpus) {
      matches += matcher(absl::string_view(directive));
    }
  }
  benchmark::DoNotOptimize(matches);
  state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(corpus.size()));
}

void bmRequestDirectiveWithSwitch(benchmark::State& state) {
  runDirectiveBenchmark(state, requestDirectiveCorpus(), requestDirectiveWithSwitch);
}
BENCHMARK(bmRequestDirectiveWithSwitch);

void bmRequestDirectiveWithPreTransformedStringEquality(benchmark::State& state) {
  runDirectiveBenchmark(state, requestDirectiveCorpus(),
                        requestDirectiveWithPreTransformedStringEquality);
}
BENCHMARK(bmRequestDirectiveWithPreTransformedStringEquality);

void bmRequestDirectiveWithEqualsIgnoreCase(benchmark::State& state) {
  runDirectiveBenchmark(state, requestDirectiveCorpus(), requestDirectiveWithEqualsIgnoreCase);
}
BENCHMARK(bmRequestDirectiveWithEqualsIgnoreCase);

void bmResponseDirectiveWithSwitch(benchmark::State& state) {
  runDirectiveBenchmark(state, responseDirectiveCorpus(), responseDirectiveWithSwitch);
}
BENCHMARK(bmResponseDirectiveWithSwitch);

void bmResponseDirectiveWithPreTransformedStringEquality(benchmark::State& state) {
  runDirectiveBenchmark(state, responseDirectiveCorpus(),
                        responseDirectiveWithPreTransformedStringEquality);
}
BENCHMARK(bmResponseDirectiveWithPreTransformedStringEquality);

void bmResponseDirectiveWithEqualsIgnoreCase(benchmark::State& state) {
  runDirectiveBenchmark(state, responseDirectiveCorpus(), responseDirectiveWithEqualsIgnoreCase);
}
BENCHMARK(bmResponseDirectiveWithEqualsIgnoreCase);

} // namespace
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
