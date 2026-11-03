// Benchmarks ParentHistogramImpl::merge(). The focused cases isolate one parent; the production
// case models 154K parents and eight workers with lazily created TLS histograms.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "envoy/stats/histogram.h"

#include "source/common/stats/allocator.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/thread_local_store.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace {

enum class ValuePattern { Overlapping, Disjoint };
enum class AllocationPattern { Packed, Scattered };

class ParentHistogramMergeBenchmark {
public:
  ParentHistogramMergeBenchmark(size_t source_count, size_t non_empty_sources,
                                size_t values_per_source, ValuePattern value_pattern,
                                AllocationPattern allocation_pattern)
      : symbol_table_(), allocator_(symbol_table_), store_(allocator_),
        values_per_source_(values_per_source), value_pattern_(value_pattern) {
    auto& histogram = store_.rootScope()->histogramFromString("merge_histogram",
                                                              Stats::Histogram::Unit::Unspecified);
    parent_ = &static_cast<Stats::ParentHistogramImpl&>(histogram);

    tls_histograms_.reserve(source_count);
    for (size_t i = 0; i < source_count; ++i) {
      if (allocation_pattern == AllocationPattern::Scattered) {
        allocateNoise();
      }
      Stats::TlsHistogramSharedPtr tls_histogram(new Stats::ThreadLocalHistogramImpl(
          parent_->statName(), parent_->unit(), parent_->statName(), Stats::StatNameTagSpan{},
          symbol_table_, parent_->bins()));
      parent_->addTlsHistogram(tls_histogram);
      tls_histograms_.push_back(std::move(tls_histogram));
    }

    non_empty_sources_ = std::min(non_empty_sources, source_count);
    // Prime the parent so that subsequent empty intervals still execute merge().
    if (!tls_histograms_.empty()) {
      tls_histograms_[0]->recordValue(1);
      for (const auto& tls_histogram : tls_histograms_) {
        tls_histogram->beginMerge();
      }
      parent_->merge();
    }
  }

  void prepareInterval() {
    for (size_t i = 0; i < tls_histograms_.size(); ++i) {
      const size_t value_count = i < non_empty_sources_ ? values_per_source_ : 0;
      if (value_count != 0) {
        // The insertion call uses scale zero. The library normalizes values
        // to two significant digits plus an exponent, so the 100-wide steps below map to
        // distinct buckets for the ranges used by this benchmark. Disjoint sources are separated
        // by 10000, which changes the exponent and keeps their buckets distinct.
        for (size_t value_index = 0; value_index < value_count; ++value_index) {
          const uint64_t source_offset = value_pattern_ == ValuePattern::Disjoint ? i * 10000 : 0;
          tls_histograms_[i]->recordValue(1000 + source_offset + value_index * 100);
        }
      }
      tls_histograms_[i]->beginMerge();
    }
  }

  void merge() { parent_->merge(); }

private:
  Stats::SymbolTableImpl symbol_table_;
  Stats::Allocator allocator_;
  Stats::ThreadLocalStoreImpl store_;
  Stats::ParentHistogramImpl* parent_{nullptr};
  std::vector<Stats::TlsHistogramSharedPtr> tls_histograms_;
  size_t non_empty_sources_{0};
  size_t values_per_source_{0};
  void allocateNoise() {
    const size_t noise_size = tls_histograms_.size() < 1000 ? 64 * 1024 : 4 * 1024;
    auto noise = std::make_unique<std::byte[]>(noise_size);
    for (size_t offset = 0; offset < noise_size; offset += 4096) {
      noise[offset] = std::byte{1};
    }
    allocation_noise_.push_back(std::move(noise));
  }

  ValuePattern value_pattern_{ValuePattern::Disjoint};
  std::vector<std::unique_ptr<std::byte[]>> allocation_noise_;
};

void BM_ParentHistogramMerge(benchmark::State& state, ValuePattern value_pattern,
                             AllocationPattern allocation_pattern) {
  const size_t source_count = state.range(0);
  const size_t non_empty_sources = state.range(1);
  const size_t values_per_source = state.range(2);
  ParentHistogramMergeBenchmark benchmark(source_count, non_empty_sources, values_per_source,
                                          value_pattern, allocation_pattern);

  for (auto _ : state) {
    state.PauseTiming();
    benchmark.prepareInterval();
    state.ResumeTiming();
    benchmark.merge();
  }
}

BENCHMARK_CAPTURE(BM_ParentHistogramMerge, Overlapping, ValuePattern::Overlapping,
                  AllocationPattern::Packed)
    ->Args({8, 0, 1})
    ->Args({32, 0, 1})
    ->Args({64, 0, 1})
    ->Args({96, 0, 1})
    ->Args({8, 1, 1})
    ->Args({32, 1, 1})
    ->Args({64, 1, 1})
    ->Args({96, 1, 1})
    ->Args({32, 2, 4})
    ->Args({64, 8, 4})
    ->Args({96, 32, 4})
    ->Args({32, 32, 16})
    ->Args({64, 64, 16});

BENCHMARK_CAPTURE(BM_ParentHistogramMerge, Disjoint, ValuePattern::Disjoint,
                  AllocationPattern::Packed)
    ->Args({8, 0, 1})
    ->Args({32, 0, 1})
    ->Args({64, 0, 1})
    ->Args({96, 0, 1})
    ->Args({8, 1, 1})
    ->Args({32, 1, 1})
    ->Args({64, 1, 1})
    ->Args({96, 1, 1})
    ->Args({32, 2, 4})
    ->Args({64, 8, 4})
    ->Args({96, 32, 4})
    ->Args({32, 32, 16})
    ->Args({64, 64, 16});

BENCHMARK_CAPTURE(BM_ParentHistogramMerge, ScatteredOverlapping, ValuePattern::Overlapping,
                  AllocationPattern::Scattered)
    ->Args({32, 0, 1})
    ->Args({64, 0, 1})
    ->Args({96, 0, 1})
    ->Args({32, 1, 1})
    ->Args({64, 1, 1})
    ->Args({96, 1, 1})
    ->Args({64, 8, 4})
    ->Args({96, 32, 4})
    ->Args({5000, 0, 1})
    ->Args({5000, 1, 1});

BENCHMARK_CAPTURE(BM_ParentHistogramMerge, ScatteredDisjoint, ValuePattern::Disjoint,
                  AllocationPattern::Scattered)
    ->Args({32, 0, 1})
    ->Args({64, 0, 1})
    ->Args({96, 0, 1})
    ->Args({32, 1, 1})
    ->Args({64, 1, 1})
    ->Args({96, 1, 1})
    ->Args({64, 8, 4})
    ->Args({96, 32, 4})
    ->Args({5000, 0, 1})
    ->Args({5000, 1, 1});

class ProductionHistogramMergeBenchmark {
public:
  explicit ProductionHistogramMergeBenchmark(size_t parent_count)
      : symbol_table_(), allocator_(symbol_table_), store_(allocator_) {
    parents_.reserve(parent_count);
    sources_.reserve(parent_count);

    // Keep the requested population ratios, but interleave parent shapes so the merge does not
    // process all one-bucket parents before all multi-bucket parents. The fixed seed makes runs
    // reproducible while preventing parent-order artifacts from dominating the benchmark.
    std::vector<size_t> bucket_counts;
    bucket_counts.reserve(parent_count);
    for (size_t parent_index = 0; parent_index < parent_count; ++parent_index) {
      bucket_counts.push_back(parentBucketCount(parent_index, parent_count));
    }
    std::mt19937 random(0x5eed);
    std::shuffle(bucket_counts.begin(), bucket_counts.end(), random);

    for (size_t parent_index = 0; parent_index < parent_count; ++parent_index) {
      const size_t bucket_count = bucket_counts[parent_index];
      auto& histogram = store_.rootScope()->histogramFromString(
          "production_merge_histogram_" + std::to_string(parent_index),
          Stats::Histogram::Unit::Unspecified);
      auto& parent = static_cast<Stats::ParentHistogramImpl&>(histogram);
      parents_.push_back(&parent);
      sources_.emplace_back();

      // Each parent owns a subset of the fixed eight-worker pool. The TLS bucket sets are
      // overlapping but not identical; their union is exactly the parent's bucket set.
      const size_t worker_count = activeWorkerCount(parent_index, bucket_count);
      sources_.back().resize(worker_count);
      for (size_t bucket = 0; bucket < bucket_count; ++bucket) {
        const size_t worker = bucket % worker_count;
        const uint64_t value = 1000 + bucket * 100;
        sources_.back()[worker].values.push_back(value);
        if (worker_count > 1 && bucket % 2 == 0) {
          sources_.back()[(worker + 1) % worker_count].values.push_back(value);
        }
      }

      for (auto& source : sources_.back()) {
        source.tls = Stats::TlsHistogramSharedPtr(new Stats::ThreadLocalHistogramImpl(
            parent.statName(), parent.unit(), parent.statName(), Stats::StatNameTagSpan{},
            symbol_table_, parent.bins()));
        parent.addTlsHistogram(source.tls);
      }
    }
  }

  void prepareInterval() {
    for (auto& parent_sources : sources_) {
      for (auto& source : parent_sources) {
        for (const uint64_t value : source.values) {
          source.tls->recordValue(value);
        }
        source.tls->beginMerge();
      }
    }
  }

  void merge() {
    store_.forEachHistogram(nullptr, [](Stats::ParentHistogram& histogram) { histogram.merge(); });
  }

private:
  struct Source {
    Stats::TlsHistogramSharedPtr tls;
    std::vector<uint64_t> values;
  };

  static size_t parentBucketCount(size_t parent_index, size_t parent_count) {
    if (parent_index < parent_count * 75 / 100) {
      return 1;
    }
    if (parent_index < parent_count * 99 / 100) {
      return 2 + parent_index % 15;
    }
    return 17 + parent_index % 84;
  }

  static size_t activeWorkerCount(size_t parent_index, size_t bucket_count) {
    constexpr size_t worker_pool_size = 8;
    if (bucket_count == 1) {
      return 1 + parent_index % 2;
    }
    if (bucket_count > 16) {
      return worker_pool_size;
    }
    return std::min(worker_pool_size, std::max<size_t>(2, bucket_count / 2));
  }

  Stats::SymbolTableImpl symbol_table_;
  Stats::Allocator allocator_;
  Stats::ThreadLocalStoreImpl store_;
  std::vector<Stats::ParentHistogramImpl*> parents_;
  std::vector<std::vector<Source>> sources_;
};

void BM_ParentHistogramMergeProduction(benchmark::State& state) {
  ProductionHistogramMergeBenchmark benchmark(state.range(0));

  for (auto _ : state) {
    state.PauseTiming();
    benchmark.prepareInterval();
    state.ResumeTiming();
    benchmark.merge();
  }
}

BENCHMARK(BM_ParentHistogramMergeProduction)->Arg(154000);

} // namespace
} // namespace Envoy
