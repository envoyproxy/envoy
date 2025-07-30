// Performance benchmark comparing RadixTree vs traditional linear search
// for HTTP route matching in virtual host wildcard scenarios.

#include <random>
#include <vector>

#include "source/common/common/radix_tree.h"
#include "source/common/router/config_impl.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Router {

// Generate realistic domain names for testing.
class DomainGenerator {
public:
  DomainGenerator() : generator_(42) {} // Fixed seed for reproducibility.

  std::vector<std::string> generateDomains(size_t count) {
    std::vector<std::string> domains;
    domains.reserve(count);

    const std::vector<std::string> tlds = {".com", ".org", ".net", ".io", ".co"};
    const std::vector<std::string> base_names = {"api",    "service", "app",  "web",   "admin",
                                                 "user",   "auth",    "data", "cache", "cdn",
                                                 "static", "media",   "blog", "shop",  "mail"};

    std::uniform_int_distribution<size_t> tld_dist(0, tlds.size() - 1);
    std::uniform_int_distribution<size_t> name_dist(0, base_names.size() - 1);
    std::uniform_int_distribution<size_t> num_dist(1, 9999);

    for (size_t i = 0; i < count; ++i) {
      const std::string base = base_names[name_dist(generator_)];
      const std::string tld = tlds[tld_dist(generator_)];
      const size_t num = num_dist(generator_);

      domains.push_back(base + std::to_string(num) + tld);
    }

    return domains;
  }

  std::vector<std::string> generateWildcardPatterns(const std::vector<std::string>& domains,
                                                    double wildcard_ratio = 0.3) {
    std::vector<std::string> patterns;
    patterns.reserve(domains.size());

    std::uniform_real_distribution<double> wildcard_dist(0.0, 1.0);
    std::uniform_int_distribution<int> type_dist(0, 1); // 0 = prefix, 1 = suffix

    for (const auto& domain : domains) {
      if (wildcard_dist(generator_) < wildcard_ratio) {
        if (type_dist(generator_) == 0) {
          // Prefix wildcard: *.example.com
          size_t dot_pos = domain.find('.');
          if (dot_pos != std::string::npos) {
            patterns.push_back("*" + domain.substr(dot_pos));
          } else {
            patterns.push_back(domain);
          }
        } else {
          // Suffix wildcard: api.*
          size_t dot_pos = domain.find('.');
          if (dot_pos != std::string::npos) {
            patterns.push_back(domain.substr(0, dot_pos) + "*");
          } else {
            patterns.push_back(domain);
          }
        }
      } else {
        patterns.push_back(domain);
      }
    }

    return patterns;
  }

private:
  std::mt19937 generator_;
};

// Traditional linear search implementation for comparison.
class LinearWildcardMatcher {
public:
  void addPattern(const std::string& pattern, int value) { patterns_.emplace_back(pattern, value); }

  int findMatch(absl::string_view host) const {
    for (const auto& [pattern, value] : patterns_) {
      if (matches(host, pattern)) {
        return value;
      }
    }
    return -1; // No match found.
  }

private:
  bool matches(absl::string_view host, const std::string& pattern) const {
    if (pattern.empty())
      return false;

    if (pattern[0] == '*') {
      // Suffix wildcard: *.example.com
      const std::string suffix = pattern.substr(1);
      return host.size() >= suffix.size() && host.substr(host.size() - suffix.size()) == suffix;
    } else if (pattern.back() == '*') {
      // Prefix wildcard: api.*
      const std::string prefix = pattern.substr(0, pattern.size() - 1);
      return host.size() >= prefix.size() && host.substr(0, prefix.size()) == prefix;
    } else {
      // Exact match.
      return host == pattern;
    }
  }

  std::vector<std::pair<std::string, int>> patterns_;
};

// RadixTree-based matcher implementation.
class RadixTreeWildcardMatcher {
public:
  RadixTreeWildcardMatcher()
      : prefix_tree_(std::make_unique<RadixTree<int>>()),
        suffix_tree_(std::make_unique<RadixTree<int>>()),
        exact_tree_(std::make_unique<RadixTree<int>>()) {}

  void addPattern(const std::string& pattern, int value) {
    if (pattern.empty())
      return;

    if (pattern[0] == '*') {
      // Suffix wildcard: *.example.com -> reverse for prefix matching.
      std::string suffix = pattern.substr(1);
      std::reverse(suffix.begin(), suffix.end());
      suffix_tree_->add(suffix, value);
    } else if (pattern.back() == '*') {
      // Prefix wildcard: api.*
      const std::string prefix = pattern.substr(0, pattern.size() - 1);
      prefix_tree_->add(prefix, value);
    } else {
      // Exact match.
      exact_tree_->add(pattern, value);
    }
  }

  int findMatch(absl::string_view host) const {
    // Try exact match first.
    int exact_match = exact_tree_->find(host);
    if (exact_match != 0) {
      return exact_match;
    }

    // Try prefix match.
    int prefix_match = prefix_tree_->findLongestPrefix(host);
    if (prefix_match != 0) {
      return prefix_match;
    }

    // Try suffix match.
    std::string reversed_host(host);
    std::reverse(reversed_host.begin(), reversed_host.end());
    int suffix_match = suffix_tree_->findLongestPrefix(reversed_host);
    if (suffix_match != 0) {
      return suffix_match;
    }

    return -1; // No match found.
  }

private:
  std::unique_ptr<RadixTree<int>> prefix_tree_;
  std::unique_ptr<RadixTree<int>> suffix_tree_;
  std::unique_ptr<RadixTree<int>> exact_tree_;
};

// Benchmark linear search implementation.
static void BM_LinearWildcardMatching(benchmark::State& state) {
  const size_t num_patterns = state.range(0);
  const size_t num_queries = 1000;

  DomainGenerator generator;
  auto domains = generator.generateDomains(num_patterns);
  auto patterns = generator.generateWildcardPatterns(domains, 0.4); // 40% wildcards
  auto query_domains = generator.generateDomains(num_queries);

  LinearWildcardMatcher matcher;
  for (size_t i = 0; i < patterns.size(); ++i) {
    matcher.addPattern(patterns[i], static_cast<int>(i));
  }

  // Pre-generate random queries for consistent benchmark.
  std::mt19937 rng(12345);
  std::uniform_int_distribution<size_t> dist(0, query_domains.size() - 1);
  std::vector<size_t> query_indices;
  for (size_t i = 0; i < 1024; ++i) {
    query_indices.push_back(dist(rng));
  }

  size_t query_idx = 0;
  for (auto _ : state) {
    const auto& query = query_domains[query_indices[query_idx % 1024]];
    int result = matcher.findMatch(query);
    benchmark::DoNotOptimize(result);
    query_idx++;
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel(fmt::format("LinearSearch_{}patterns", num_patterns));
}

// Benchmark RadixTree implementation.
static void BM_RadixTreeWildcardMatching(benchmark::State& state) {
  const size_t num_patterns = state.range(0);
  const size_t num_queries = 1000;

  DomainGenerator generator;
  auto domains = generator.generateDomains(num_patterns);
  auto patterns = generator.generateWildcardPatterns(domains, 0.4); // 40% wildcards
  auto query_domains = generator.generateDomains(num_queries);

  RadixTreeWildcardMatcher matcher;
  for (size_t i = 0; i < patterns.size(); ++i) {
    matcher.addPattern(patterns[i], static_cast<int>(i));
  }

  // Pre-generate random queries for consistent benchmark.
  std::mt19937 rng(12345);
  std::uniform_int_distribution<size_t> dist(0, query_domains.size() - 1);
  std::vector<size_t> query_indices;
  for (size_t i = 0; i < 1024; ++i) {
    query_indices.push_back(dist(rng));
  }

  size_t query_idx = 0;
  for (auto _ : state) {
    const auto& query = query_domains[query_indices[query_idx % 1024]];
    int result = matcher.findMatch(query);
    benchmark::DoNotOptimize(result);
    query_idx++;
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel(fmt::format("RadixTree_{}patterns", num_patterns));
}

// Comprehensive benchmark with varying pattern counts.
BENCHMARK(BM_LinearWildcardMatching)->Range(10, 10000)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_RadixTreeWildcardMatching)->Range(10, 10000)->Unit(benchmark::kNanosecond);

// Focused benchmarks for specific scenarios.
BENCHMARK(BM_LinearWildcardMatching)->Arg(100)->Arg(500)->Arg(1000)->Arg(5000);
BENCHMARK(BM_RadixTreeWildcardMatching)->Arg(100)->Arg(500)->Arg(1000)->Arg(5000);

} // namespace Router
} // namespace Envoy
