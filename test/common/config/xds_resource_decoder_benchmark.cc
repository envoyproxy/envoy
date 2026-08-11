// Note: this should be run with --compilation_mode=opt.
// To reduce noise, it is recommended to:
// 1. Disable ASLR: setarch $(uname -m) -R
// ./bazel-bin/test/common/config/xds_resource_decoder_benchmark ...
// 2. Run with repetitions: --benchmark_repetitions=10 --benchmark_report_aggregates_only=true
// 3. Increase min time: --benchmark_min_time=2s

#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.validate.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener.pb.validate.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/common/assert.h"
#include "source/common/config/opaque_resource_decoder_impl.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Config {
namespace {

// Helper to make a simple ClusterLoadAssignment.
envoy::config::endpoint::v3::ClusterLoadAssignment makeSimpleCLA() {
  envoy::config::endpoint::v3::ClusterLoadAssignment cla;
  cla.set_cluster_name("simple_cluster");
  auto* locality_lb = cla.add_endpoints();
  locality_lb->mutable_locality()->set_region("us-east1");
  locality_lb->mutable_locality()->set_zone("us-east1-a");
  auto* lb_endpoint = locality_lb->add_lb_endpoints();
  auto* endpoint = lb_endpoint->mutable_endpoint();
  auto* socket_address = endpoint->mutable_address()->mutable_socket_address();
  socket_address->set_address("10.0.0.1");
  socket_address->set_port_value(80);
  return cla;
}

// Helper to make a complex ClusterLoadAssignment.
envoy::config::endpoint::v3::ClusterLoadAssignment makeComplexCLA(int num_localities,
                                                                  int num_endpoints_per_locality) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cla;
  cla.set_cluster_name("complex_cluster");
  for (int i = 0; i < num_localities; ++i) {
    auto* locality_lb = cla.add_endpoints();
    locality_lb->mutable_locality()->set_region("us-central1");
    locality_lb->mutable_locality()->set_zone(absl::StrCat("us-central1-", i));
    for (int j = 0; j < num_endpoints_per_locality; ++j) {
      auto* lb_endpoint = locality_lb->add_lb_endpoints();
      auto* endpoint = lb_endpoint->mutable_endpoint();
      auto* socket_address = endpoint->mutable_address()->mutable_socket_address();
      socket_address->set_address(absl::StrFormat("10.%d.%d.%d", i, j / 256, j % 256));
      socket_address->set_port_value(80);
    }
  }
  return cla;
}

// Helper to make a simple Listener.
envoy::config::listener::v3::Listener makeSimpleListener() {
  envoy::config::listener::v3::Listener listener;
  listener.set_name("simple_listener");
  auto* socket_address = listener.mutable_address()->mutable_socket_address();
  socket_address->set_address("0.0.0.0");
  socket_address->set_port_value(80);
  return listener;
}

// Helper to make a complex Listener.
envoy::config::listener::v3::Listener makeComplexListener(int num_filter_chains, int num_routes) {
  envoy::config::listener::v3::Listener listener;
  listener.set_name("complex_listener");
  auto* socket_address = listener.mutable_address()->mutable_socket_address();
  socket_address->set_address("0.0.0.0");
  socket_address->set_port_value(80);

  for (int i = 0; i < num_filter_chains; ++i) {
    auto* filter_chain = listener.add_filter_chains();
    auto* filter = filter_chain->add_filters();
    filter->set_name("envoy.filters.network.http_connection_manager");

    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager hcm;
    hcm.set_stat_prefix(absl::StrCat("hcm_", i));

    auto* route_config = hcm.mutable_route_config();
    route_config->set_name(absl::StrCat("route_config_", i));
    auto* virtual_host = route_config->add_virtual_hosts();
    virtual_host->set_name("default");
    virtual_host->add_domains("*");

    for (int j = 0; j < num_routes; ++j) {
      auto* route = virtual_host->add_routes();
      route->mutable_match()->set_prefix(absl::StrCat("/route_", j));
      route->mutable_route()->set_cluster(absl::StrCat("cluster_", j));
    }

    bool ok = filter->mutable_typed_config()->PackFrom(hcm);
    RELEASE_ASSERT(ok, "Failed to pack HCM config");
  }
  return listener;
}

// Benchmark decoding using OpaqueResourceDecoderImpl.
template <typename Current>
void decodeResource(OpaqueResourceDecoderImpl<Current>& decoder, const Protobuf::Any& resource) {
  auto decoded_resource = decoder.decodeResource(resource);
  benchmark::DoNotOptimize(decoded_resource);
}

// Benchmark class to hold state.
class XdsDecodingBenchmark {
public:
  XdsDecodingBenchmark()
      : cla_decoder_(null_validation_visitor_, "cluster_name"),
        listener_decoder_(null_validation_visitor_, "name") {
    // Prepare simple CLA Any.
    auto simple_cla = makeSimpleCLA();
    bool ok = simple_cla_any_.PackFrom(simple_cla);
    RELEASE_ASSERT(ok, "Failed to pack simple CLA");

    // Prepare complex CLA Any.
    auto complex_cla =
        makeComplexCLA(10, 100); // 10 localities, 100 endpoints each = 1000 endpoints.
    ok = complex_cla_any_.PackFrom(complex_cla);
    RELEASE_ASSERT(ok, "Failed to pack complex CLA");

    // Prepare simple Listener Any.
    auto simple_listener = makeSimpleListener();
    ok = simple_listener_any_.PackFrom(simple_listener);
    RELEASE_ASSERT(ok, "Failed to pack simple Listener");

    // Prepare complex Listener Any.
    auto complex_listener =
        makeComplexListener(5, 50); // 5 filter chains, 50 routes each = 250 routes.
    ok = complex_listener_any_.PackFrom(complex_listener);
    RELEASE_ASSERT(ok, "Failed to pack complex Listener");
  }

  OpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment> cla_decoder_;
  OpaqueResourceDecoderImpl<envoy::config::listener::v3::Listener> listener_decoder_;

  Protobuf::Any simple_cla_any_;
  Protobuf::Any complex_cla_any_;
  Protobuf::Any simple_listener_any_;
  Protobuf::Any complex_listener_any_;
  ProtobufMessage::NullValidationVisitorImpl null_validation_visitor_;
};

// Global benchmark state.
XdsDecodingBenchmark* g_benchmark_state = nullptr;

void setupBenchmarkState() {
  if (g_benchmark_state == nullptr) {
    g_benchmark_state = new XdsDecodingBenchmark();
  }
}

// --- CLA Benchmarks ---

static void BM_CLA_Decode_Simple(benchmark::State& state) {
  setupBenchmarkState();
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    decodeResource(g_benchmark_state->cla_decoder_, g_benchmark_state->simple_cla_any_);
  }
}
BENCHMARK(BM_CLA_Decode_Simple);

static void BM_CLA_Decode_Complex(benchmark::State& state) {
  setupBenchmarkState();
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    decodeResource(g_benchmark_state->cla_decoder_, g_benchmark_state->complex_cla_any_);
  }
}
BENCHMARK(BM_CLA_Decode_Complex);

// --- Listener Benchmarks ---

static void BM_Listener_Decode_Simple(benchmark::State& state) {
  setupBenchmarkState();
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    decodeResource(g_benchmark_state->listener_decoder_, g_benchmark_state->simple_listener_any_);
  }
}
BENCHMARK(BM_Listener_Decode_Simple);

static void BM_Listener_Decode_Complex(benchmark::State& state) {
  setupBenchmarkState();
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    decodeResource(g_benchmark_state->listener_decoder_, g_benchmark_state->complex_listener_any_);
  }
}
BENCHMARK(BM_Listener_Decode_Complex);

} // namespace
} // namespace Config
} // namespace Envoy
