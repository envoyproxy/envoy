#include "source/common/common/radix/trie_hybrid.hpp"
#include "source/common/common/trie_lookup_table.h"
#include <benchmark/benchmark.h>
#include <random>

namespace Envoy {
namespace Common {

// Benchmark for TrieHybrid insertions
static void BM_TrieHybridInsertions(benchmark::State& state) {
    TrieHybrid<std::string, std::string> hybrid;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 20);
    
    for (auto _ : state) {
        std::string key = "key_" + std::to_string(dis(gen));
        if (key.size() <= 8) {
            key = "short_" + key;  // Ensure short keys go to trie
        } else {
            key = "very_long_key_for_radix_tree_" + key;  // Ensure long keys go to radix
        }
        auto [newHybrid, oldVal, didUpdate] = hybrid.insert(key, "value_" + key);
        hybrid = newHybrid;
    }
}

// Benchmark for TrieHybrid lookups
static void BM_TrieHybridLookups(benchmark::State& state) {
    TrieHybrid<std::string, std::string> hybrid;
    
    // Pre-populate with mixed key lengths
    for (int i = 0; i < 1000; ++i) {
        std::string shortKey = "short_" + std::to_string(i);
        std::string longKey = "very_long_key_for_radix_tree_" + std::to_string(i);
        
        auto [newHybrid1, _, __] = hybrid.insert(shortKey, "value_" + shortKey);
        auto [newHybrid2, ___, ____] = newHybrid1.insert(longKey, "value_" + longKey);
        hybrid = newHybrid2;
    }
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 999);
    
    for (auto _ : state) {
        int idx = dis(gen);
        std::string shortKey = "short_" + std::to_string(idx);
        std::string longKey = "very_long_key_for_radix_tree_" + std::to_string(idx);
        
        // Test both short and long key lookups
        auto result1 = hybrid.Get(shortKey);
        auto result2 = hybrid.Get(longKey);
        benchmark::DoNotOptimize(result1);
        benchmark::DoNotOptimize(result2);
    }
}

// Benchmark for TrieHybrid longest prefix matches
static void BM_TrieHybridLongestPrefix(benchmark::State& state) {
    TrieHybrid<std::string, std::string> hybrid;
    
    // Pre-populate with mixed key lengths
    for (int i = 0; i < 1000; ++i) {
        std::string shortKey = "short_" + std::to_string(i);
        std::string longKey = "very_long_key_for_radix_tree_" + std::to_string(i);
        
        auto [newHybrid1, _, __] = hybrid.insert(shortKey, "value_" + shortKey);
        auto [newHybrid2, ___, ____] = newHybrid1.insert(longKey, "value_" + longKey);
        hybrid = newHybrid2;
    }
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 999);
    
    for (auto _ : state) {
        int idx = dis(gen);
        std::string shortPrefix = "short_" + std::to_string(idx);
        std::string longPrefix = "very_long_key_for_radix_tree_" + std::to_string(idx);
        
        // Test longest prefix matches for both short and long keys
        auto result1 = hybrid.LongestPrefix(shortPrefix);
        auto result2 = hybrid.LongestPrefix(longPrefix);
        benchmark::DoNotOptimize(result1);
        benchmark::DoNotOptimize(result2);
    }
}

// Benchmark for TrieHybrid find matching prefixes
static void BM_TrieHybridFindMatchingPrefixes(benchmark::State& state) {
    TrieHybrid<std::string, std::string> hybrid;
    
    // Pre-populate with mixed key lengths
    for (int i = 0; i < 1000; ++i) {
        std::string shortKey = "short_" + std::to_string(i);
        std::string longKey = "very_long_key_for_radix_tree_" + std::to_string(i);
        
        auto [newHybrid1, _, __] = hybrid.insert(shortKey, "value_" + shortKey);
        auto [newHybrid2, ___, ____] = newHybrid1.insert(longKey, "value_" + longKey);
        hybrid = newHybrid2;
    }
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 999);
    
    for (auto _ : state) {
        int idx = dis(gen);
        std::string shortPrefix = "short_" + std::to_string(idx);
        std::string longPrefix = "very_long_key_for_radix_tree_" + std::to_string(idx);
        
        // Test find matching prefixes for both short and long keys
        auto result1 = hybrid.findMatchingPrefixes(shortPrefix);
        auto result2 = hybrid.findMatchingPrefixes(longPrefix);
        benchmark::DoNotOptimize(result1);
        benchmark::DoNotOptimize(result2);
    }
}

// Benchmark comparing TrieHybrid vs TrieLookupTable for short keys
static void BM_TrieHybridVsTrieLookupShortKeys(benchmark::State& state) {
    TrieHybrid<std::string, std::string> hybrid;
    TrieLookupTable<const char*> trieTable;
    
    // Pre-populate both with short keys
    for (int i = 0; i < 1000; ++i) {
        std::string key = "short_" + std::to_string(i);
        std::string value = "value_" + std::to_string(i);
        
        auto [newHybrid, _, __] = hybrid.insert(key, value);
        hybrid = newHybrid;
        trieTable.add(key, value.c_str());
    }
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 999);
    
    for (auto _ : state) {
        int idx = dis(gen);
        std::string key = "short_" + std::to_string(idx);
        
        auto hybridResult = hybrid.Get(key);
        auto trieResult = trieTable.find(key);
        benchmark::DoNotOptimize(hybridResult);
        benchmark::DoNotOptimize(trieResult);
    }
}

// Benchmark comparing TrieHybrid vs TrieLookupTable for long keys
static void BM_TrieHybridVsTrieLookupLongKeys(benchmark::State& state) {
    TrieHybrid<std::string, std::string> hybrid;
    TrieLookupTable<const char*> trieTable;
    
    // Pre-populate both with long keys
    for (int i = 0; i < 1000; ++i) {
        std::string key = "very_long_key_for_radix_tree_" + std::to_string(i);
        std::string value = "value_" + std::to_string(i);
        
        auto [newHybrid, _, __] = hybrid.insert(key, value);
        hybrid = newHybrid;
        trieTable.add(key, value.c_str());
    }
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 999);
    
    for (auto _ : state) {
        int idx = dis(gen);
        std::string key = "very_long_key_for_radix_tree_" + std::to_string(idx);
        
        auto hybridResult = hybrid.Get(key);
        auto trieResult = trieTable.find(key);
        benchmark::DoNotOptimize(hybridResult);
        benchmark::DoNotOptimize(trieResult);
    }
}

// Register benchmarks
BENCHMARK(BM_TrieHybridInsertions);
BENCHMARK(BM_TrieHybridLookups);
BENCHMARK(BM_TrieHybridLongestPrefix);
BENCHMARK(BM_TrieHybridFindMatchingPrefixes);
BENCHMARK(BM_TrieHybridVsTrieLookupShortKeys);
BENCHMARK(BM_TrieHybridVsTrieLookupLongKeys); 