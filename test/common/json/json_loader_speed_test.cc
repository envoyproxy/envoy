#include <iostream>

#include "common/json/json_loader.h"
#include "common/json/rapidjson_loader.h"

#include "benchmark/benchmark.h"
#include "include/nlohmann/json.hpp"

namespace Envoy {

// Creates a JSON string with a variable number of elements and array sizes.
std::string createJsonObject(const int elements) {
  nlohmann::json j = nlohmann::json::object();
  for (int i = 0; i < elements; i++) {
    j.emplace(std::to_string(i), i);
  }
  return j.dump();
}

std::string createJsonArray(const int elements) {
  nlohmann::json j = nlohmann::json::array();
  for (int i = 0; i < elements; i++) {
    j.push_back(std::to_string(i));
  }
  return j.dump();
}

template <class Parser> void BM_JsonLoadObjects(benchmark::State& state) {
  std::string json = createJsonObject(state.range(0));
  for (auto _ : state) {
    Parser::loadFromString(json);
  }
  benchmark::DoNotOptimize(json);
}
BENCHMARK_TEMPLATE(BM_JsonLoadObjects, Json::Factory)->Arg(1)->Arg(10)->Arg(50)->Arg(100);
BENCHMARK_TEMPLATE(BM_JsonLoadObjects, Json::RapidJson::Factory)
    ->Arg(1)
    ->Arg(10)
    ->Arg(50)
    ->Arg(100);

template <class Parser> void BM_JsonLoadArray(benchmark::State& state) {
  std::string json = createJsonArray(state.range(0));
  for (auto _ : state) {
    Parser::loadFromString(json);
  }
  benchmark::DoNotOptimize(json);
}
BENCHMARK_TEMPLATE(BM_JsonLoadArray, Json::Factory)->Arg(1)->Arg(10)->Arg(50)->Arg(100);
BENCHMARK_TEMPLATE(BM_JsonLoadArray, Json::RapidJson::Factory)->Arg(1)->Arg(10)->Arg(50)->Arg(100);

} // namespace Envoy
