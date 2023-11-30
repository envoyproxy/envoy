// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include "source/common/protobuf/utility.h"

#include "test/common/protobuf/deterministic_hash_test.pb.h"
#include "test/test_common/test_runtime.h"

#include "benchmark/benchmark.h"

namespace Envoy {

// NOLINT(namespace-envoy)

static deterministichashtest::Maps testProto() {
  deterministichashtest::Maps msg;
  (*msg.mutable_bool_string())[false] = "abcdefghijklmnopqrstuvwxyz";
  (*msg.mutable_bool_string())[true] = "abcdefghijklmnopqrstuvwxyz";
  for (int i = 0; i < 100; i++) {
    (*msg.mutable_string_bool())[absl::StrCat(i)] = true;
    (*msg.mutable_int32_uint32())[i] = i;
    (*msg.mutable_uint32_int32())[i] = i;
    (*msg.mutable_uint64_int64())[i + 1000000000000L] = i + 1000000000000L;
    (*msg.mutable_int64_uint64())[i + 1000000000000L] = i + 1000000000000L;
  }
  return msg;
}

static void bmHashByTextFormat(benchmark::State& state) {
  TestScopedRuntime runtime_;
  runtime_.mergeValues({{"envoy.restart_features.use_fast_protobuf_hash", "false"}});
  auto msg = testProto();
  uint64_t hash = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    hash += MessageUtil::hash(msg);
  }
  benchmark::DoNotOptimize(hash);
}
BENCHMARK(bmHashByTextFormat);

static void bmHashByDeterministicHash(benchmark::State& state) {
  auto msg = testProto();
  uint64_t hash = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    hash += MessageUtil::hash(msg);
  }
  benchmark::DoNotOptimize(hash);
}
BENCHMARK(bmHashByDeterministicHash);

} // namespace Envoy
