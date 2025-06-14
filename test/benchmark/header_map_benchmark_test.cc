#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_map_optimized.h"

#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Http {
namespace {

// Helper function to generate random header values
std::string generateRandomString(size_t length) {
  static const char alphanum[] = "0123456789"
                                 "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                 "abcdefghijklmnopqrstuvwxyz";
  std::string result;
  result.reserve(length);
  for (size_t i = 0; i < length; ++i) {
    result += alphanum[rand() % (sizeof(alphanum) - 1)];
  }
  return result;
}

// Benchmark header insertion for original implementation
static void BM_HeaderMapImplInsert(benchmark::State& state) {
  auto headers = RequestHeaderMapImpl::create();
  const int num_headers = state.range(0);
  const int value_size = state.range(1);

  // Pre-generate headers to avoid measuring string generation
  std::vector<std::pair<LowerCaseString, std::string>> test_headers;
  for (int i = 0; i < num_headers; ++i) {
    test_headers.emplace_back(LowerCaseString("test-header-" + std::to_string(i)),
                              generateRandomString(value_size));
  }

  for (auto _ : state) {
    for (const auto& header : test_headers) {
      headers->addCopy(header.first, header.second);
    }
    headers->clear();
  }

  state.SetItemsProcessed(state.iterations() * num_headers);
}

// Benchmark header insertion for optimized implementation
static void BM_HeaderMapOptimizedInsert(benchmark::State& state) {
  HeaderMapOptimized headers;
  const int num_headers = state.range(0);
  const int value_size = state.range(1);

  // Pre-generate headers to avoid measuring string generation
  std::vector<std::pair<LowerCaseString, std::string>> test_headers;
  for (int i = 0; i < num_headers; ++i) {
    test_headers.emplace_back(LowerCaseString("test-header-" + std::to_string(i)),
                              generateRandomString(value_size));
  }

  for (auto _ : state) {
    for (const auto& header : test_headers) {
      headers.addCopy(header.first, header.second);
    }
    headers.clear();
  }

  state.SetItemsProcessed(state.iterations() * num_headers);
}

// Benchmark header lookup for original implementation
static void BM_HeaderMapImplLookup(benchmark::State& state) {
  auto headers = RequestHeaderMapImpl::create();
  const int num_headers = state.range(0);
  const int value_size = state.range(1);

  // Pre-generate and insert headers
  std::vector<LowerCaseString> header_keys;
  for (int i = 0; i < num_headers; ++i) {
    auto key = LowerCaseString("test-header-" + std::to_string(i));
    header_keys.push_back(key);
    headers->addCopy(key, generateRandomString(value_size));
  }

  for (auto _ : state) {
    for (const auto& key : header_keys) {
      benchmark::DoNotOptimize(headers->get(key));
    }
  }

  state.SetItemsProcessed(state.iterations() * num_headers);
}

// Benchmark header lookup for optimized implementation
static void BM_HeaderMapOptimizedLookup(benchmark::State& state) {
  HeaderMapOptimized headers;
  const int num_headers = state.range(0);
  const int value_size = state.range(1);

  // Pre-generate and insert headers
  std::vector<LowerCaseString> header_keys;
  for (int i = 0; i < num_headers; ++i) {
    auto key = LowerCaseString("test-header-" + std::to_string(i));
    header_keys.push_back(key);
    headers.addCopy(key, generateRandomString(value_size));
  }

  for (auto _ : state) {
    for (const auto& key : header_keys) {
      benchmark::DoNotOptimize(headers.get(key));
    }
  }

  state.SetItemsProcessed(state.iterations() * num_headers);
}

// Benchmark header iteration for original implementation
static void BM_HeaderMapImplIteration(benchmark::State& state) {
  auto headers = RequestHeaderMapImpl::create();
  const int num_headers = state.range(0);
  const int value_size = state.range(1);

  // Pre-generate and insert headers
  for (int i = 0; i < num_headers; ++i) {
    headers->addCopy(LowerCaseString("test-header-" + std::to_string(i)),
                     generateRandomString(value_size));
  }

  for (auto _ : state) {
    headers->iterate([](const HeaderEntry& header) -> HeaderMap::Iterate {
      benchmark::DoNotOptimize(header);
      return HeaderMap::Iterate::Continue;
    });
  }

  state.SetItemsProcessed(state.iterations() * num_headers);
}

// Benchmark header iteration for optimized implementation
static void BM_HeaderMapOptimizedIteration(benchmark::State& state) {
  HeaderMapOptimized headers;
  const int num_headers = state.range(0);
  const int value_size = state.range(1);

  // Pre-generate and insert headers
  for (int i = 0; i < num_headers; ++i) {
    headers.addCopy(LowerCaseString("test-header-" + std::to_string(i)),
                    generateRandomString(value_size));
  }

  for (auto _ : state) {
    for (const auto& header : headers) {
      benchmark::DoNotOptimize(header);
    }
  }

  state.SetItemsProcessed(state.iterations() * num_headers);
}

// Benchmark core HTTP/2 headers access patterns
static void BM_HeaderMapOptimizedCoreHeaders(benchmark::State& state) {
  const int num_requests = state.range(0);

  // Core HTTP/2 headers that Envoy frequently accesses
  const std::vector<std::pair<LowerCaseString, std::string>> core_headers = {
      {LowerCaseString(":path"), "/api/v1/users"},
      {LowerCaseString(":authority"), "example.com"},
      {LowerCaseString(":method"), "GET"},
      {LowerCaseString(":scheme"), "https"},
      {LowerCaseString("content-type"), "application/json"},
      {LowerCaseString("user-agent"), "curl/7.68.0"},
      {LowerCaseString("accept"), "*/*"},
      {LowerCaseString("x-forwarded-for"), "10.0.0.1"},
      {LowerCaseString("x-envoy-internal"), "true"},
      {LowerCaseString("x-request-id"), "123e4567-e89b-12d3-a456-426614174000"}};

  for (auto _ : state) {
    HeaderMapOptimized headers;

    // Simulate processing multiple requests
    for (int i = 0; i < num_requests; ++i) {
      // Add headers
      for (const auto& [key, value] : core_headers) {
        headers.addCopy(key, value);
      }

      // Access headers multiple times (simulating routing/processing)
      for (int j = 0; j < 10; ++j) {
        for (const auto& [key, _] : core_headers) {
          benchmark::DoNotOptimize(headers.get(key));
        }
      }

      // Remove some headers
      headers.removeIf([](const HeaderEntry& entry) {
        return entry.key().getStringView() == "x-envoy-internal" ||
               entry.key().getStringView() == "x-request-id";
      });

      // Add new headers
      headers.addCopy(LowerCaseString("x-envoy-retry-on"), "5xx");
      headers.addCopy(LowerCaseString("x-envoy-max-retries"), "3");

      // Clear for next request
      headers.clear();
    }
  }

  state.SetItemsProcessed(state.iterations() * num_requests * core_headers.size());
}

// Core HTTP/2 headers commonly accessed during request processing
const std::vector<std::pair<LowerCaseString, std::string>> CORE_HEADERS = {
    {LowerCaseString(":method"), "GET"},
    {LowerCaseString(":path"), "/api/v1/clusters"},
    {LowerCaseString(":scheme"), "https"},
    {LowerCaseString(":authority"), "example.com"},
    {LowerCaseString("x-request-id"), "123e4567-e89b-12d3-a456-426614174000"},
    {LowerCaseString("x-forwarded-for"), "10.0.0.1"},
    {LowerCaseString("user-agent"), "curl/7.68.0"}};

const std::vector<std::pair<LowerCaseString, std::string>> ROUTING_HEADERS = {
    {LowerCaseString("x-envoy-retry-on"), "5xx"},
    {LowerCaseString("x-envoy-retry-grpc-on"), "cancelled"},
    {LowerCaseString("x-envoy-max-retries"), "3"},
    {LowerCaseString("x-envoy-upstream-rq-timeout-ms"), "15000"},
    {LowerCaseString("x-envoy-upstream-rq-per-try-timeout-ms"), "2500"}};

// Benchmark that simulates real-world request processing patterns
static void BM_HeaderMapImplRequestProcessing(benchmark::State& state) {
  auto headers = RequestHeaderMapImpl::create();

  for (auto _ : state) {
    // Add core headers (happens on request start)
    for (const auto& [key, value] : CORE_HEADERS) {
      headers->addCopy(key, value);
    }

    // Multiple reads of core headers (routing, logging, tracing)
    for (int i = 0; i < 5; i++) {
      benchmark::DoNotOptimize(headers->get(LowerCaseString(":path")));
      benchmark::DoNotOptimize(headers->get(LowerCaseString(":authority")));
      benchmark::DoNotOptimize(headers->get(LowerCaseString(":method")));
      benchmark::DoNotOptimize(headers->get(LowerCaseString("x-request-id")));
    }

    // Add routing headers (happens during request processing)
    for (const auto& [key, value] : ROUTING_HEADERS) {
      headers->addCopy(key, value);
    }

    // Read routing headers (happens multiple times during processing)
    for (int i = 0; i < 3; i++) {
      for (const auto& [key, _] : ROUTING_HEADERS) {
        benchmark::DoNotOptimize(headers->get(key));
      }
    }

    // Remove some headers (cleanup)
    headers->remove(LowerCaseString("x-envoy-retry-on"));
    headers->remove(LowerCaseString("x-envoy-max-retries"));

    headers->clear();
  }
}

// Same benchmark for optimized implementation
static void BM_HeaderMapOptimizedRequestProcessing(benchmark::State& state) {
  HeaderMapOptimized headers;

  for (auto _ : state) {
    // Add core headers (happens on request start)
    for (const auto& [key, value] : CORE_HEADERS) {
      headers.addCopy(key, value);
    }

    // Multiple reads of core headers (routing, logging, tracing)
    for (int i = 0; i < 5; i++) {
      benchmark::DoNotOptimize(headers.get(LowerCaseString(":path")));
      benchmark::DoNotOptimize(headers.get(LowerCaseString(":authority")));
      benchmark::DoNotOptimize(headers.get(LowerCaseString(":method")));
      benchmark::DoNotOptimize(headers.get(LowerCaseString("x-request-id")));
    }

    // Add routing headers (happens during request processing)
    for (const auto& [key, value] : ROUTING_HEADERS) {
      headers.addCopy(key, value);
    }

    // Read routing headers (happens multiple times during processing)
    for (int i = 0; i < 3; i++) {
      for (const auto& [key, _] : ROUTING_HEADERS) {
        benchmark::DoNotOptimize(headers.get(key));
      }
    }

    // Remove some headers (cleanup)
    headers.remove(LowerCaseString("x-envoy-retry-on"));
    headers.remove(LowerCaseString("x-envoy-max-retries"));

    headers.clear();
  }
}

// Register benchmarks
BENCHMARK(BM_HeaderMapImplInsert)
    ->Args({10, 10})   // 10 headers, 10 bytes each
    ->Args({50, 20})   // 50 headers, 20 bytes each
    ->Args({100, 50}); // 100 headers, 50 bytes each

BENCHMARK(BM_HeaderMapOptimizedInsert)->Args({10, 10})->Args({50, 20})->Args({100, 50});

BENCHMARK(BM_HeaderMapImplLookup)->Args({10, 10})->Args({50, 20})->Args({100, 50});

BENCHMARK(BM_HeaderMapOptimizedLookup)->Args({10, 10})->Args({50, 20})->Args({100, 50});

BENCHMARK(BM_HeaderMapImplIteration)->Args({10, 10})->Args({50, 20})->Args({100, 50});

BENCHMARK(BM_HeaderMapOptimizedIteration)->Args({10, 10})->Args({50, 20})->Args({100, 50});

BENCHMARK(BM_HeaderMapOptimizedCoreHeaders)
    ->Args({100})    // 100 requests
    ->Args({1000})   // 1000 requests
    ->Args({10000}); // 10000 requests

BENCHMARK(BM_HeaderMapImplRequestProcessing);
BENCHMARK(BM_HeaderMapOptimizedRequestProcessing);

} // namespace
} // namespace Http
} // namespace Envoy
