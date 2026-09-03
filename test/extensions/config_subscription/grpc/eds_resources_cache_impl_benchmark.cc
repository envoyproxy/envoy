#include "source/common/event/libevent.h"
#include "source/extensions/config_subscription/grpc/eds_resources_cache_impl.h"

#include "test/benchmark/main.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Config {
namespace {

static bool initLibevent() {
  if (!Event::Libevent::Global::initialized()) {
    Event::Libevent::Global::initialize();
  }
  return true;
}

envoy::config::endpoint::v3::ClusterLoadAssignment
buildComplexClusterLoadAssignment(int num_localities, int num_endpoints_per_locality,
                                  int num_metadata_fields) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cla;
  cla.set_cluster_name("test_cluster");

  for (int i = 0; i < num_localities; ++i) {
    auto* locality_endpoints = cla.add_endpoints();
    locality_endpoints->mutable_locality()->set_region("region");
    locality_endpoints->mutable_locality()->set_zone(absl::StrCat("zone_", i));

    for (int j = 0; j < num_endpoints_per_locality; ++j) {
      auto* lb_endpoint = locality_endpoints->add_lb_endpoints();
      auto* endpoint = lb_endpoint->mutable_endpoint();
      auto* address = endpoint->mutable_address()->mutable_socket_address();
      address->set_address(absl::StrCat("10.0.", i, ".", j));
      address->set_port_value(80);

      // Add some metadata to make it complex
      auto* metadata = lb_endpoint->mutable_metadata();
      auto* fields = (*metadata->mutable_filter_metadata())["envoy.lb"].mutable_fields();
      for (int k = 0; k < num_metadata_fields; ++k) {
        (*fields)[absl::StrCat("key_", k)].set_string_value(absl::StrCat("value_", k));
      }
    }
  }
  return cla;
}

class EdsResourcesCacheImplBenchmark {
public:
  EdsResourcesCacheImplBenchmark()
      : api_((initLibevent(), Api::createApiForTest(time_system_))),
        dispatcher_(api_->allocateDispatcher("benchmark_thread")), resources_cache_(*dispatcher_) {}

  Event::SimulatedTimeSystemHelper time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  EdsResourcesCacheImpl resources_cache_;
};

void bmSetResource(::benchmark::State& state) {
  EdsResourcesCacheImplBenchmark benchmark_env;
  const int num_localities = state.range(0);
  const int num_endpoints = state.range(1);
  const int num_metadata_fields = 5;

  auto cla = buildComplexClusterLoadAssignment(num_localities, num_endpoints, num_metadata_fields);

  for (auto _ : state) {
    benchmark_env.resources_cache_.setResource("test_resource", cla);
  }
}

// Benchmark arguments: {num_localities, num_endpoints_per_locality}
BENCHMARK(bmSetResource)
    ->Args({1, 1})
    ->Args({1, 10})
    ->Args({10, 10})
    ->Args({10, 100})
    ->Args({100, 100});

void bmGetResource(::benchmark::State& state) {
  EdsResourcesCacheImplBenchmark benchmark_env;
  const int num_localities = state.range(0);
  const int num_endpoints = state.range(1);
  const int num_metadata_fields = 5;

  auto cla = buildComplexClusterLoadAssignment(num_localities, num_endpoints, num_metadata_fields);
  benchmark_env.resources_cache_.setResource("test_resource", cla);

  for (auto _ : state) {
    auto fetched = benchmark_env.resources_cache_.getResource("test_resource", nullptr);
    ::benchmark::DoNotOptimize(fetched);
  }
}

BENCHMARK(bmGetResource)
    ->Args({1, 1})
    ->Args({1, 10})
    ->Args({10, 10})
    ->Args({10, 100})
    ->Args({100, 100});

} // namespace
} // namespace Config
} // namespace Envoy
