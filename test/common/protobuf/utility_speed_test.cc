// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include "source/common/protobuf/utility.h"

#include "test/common/protobuf/deterministic_hash_test.pb.h"

#include "benchmark/benchmark.h"

namespace Envoy {

// NOLINT(namespace-envoy)

static std::unique_ptr<Protobuf::Message> testProtoWithMaps() {
  auto msg = std::make_unique<deterministichashtest::Maps>();
  (*msg->mutable_bool_string())[false] = "abcdefghijklmnopqrstuvwxyz";
  (*msg->mutable_bool_string())[true] = "abcdefghijklmnopqrstuvwxyz";
  for (int i = 0; i < 100; i++) {
    (*msg->mutable_string_bool())[absl::StrCat(i)] = true;
    (*msg->mutable_int32_uint32())[i] = i;
    (*msg->mutable_uint32_int32())[i] = i;
    (*msg->mutable_uint64_int64())[i + 1000000000000L] = i + 1000000000000L;
    (*msg->mutable_int64_uint64())[i + 1000000000000L] = i + 1000000000000L;
  }
  return msg;
}

static std::unique_ptr<Protobuf::Message> testProtoWithRecursion() {
  auto msg = std::make_unique<deterministichashtest::Recursion>();
  deterministichashtest::Recursion* p = msg.get();
  for (int i = 0; i < 100; i++) {
    p->set_index(i);
    p = p->mutable_child();
  }
  return msg;
}

static std::unique_ptr<Protobuf::Message> testProtoWithRepeatedFields() {
  auto msg = std::make_unique<deterministichashtest::RepeatedFields>();
  for (int i = 0; i < 100; i++) {
    msg->add_bools(true);
    msg->add_strings("abcdefghijklmnopqrstuvwxyz");
    msg->add_int32s(-12345);
    msg->add_uint32s(12345);
    msg->add_int64s(123456789012345L);
    msg->add_uint64s(-123456789012345UL);
    msg->add_byteses("abcdefghijklmnopqrstuvwxyz");
    msg->add_doubles(123456789.12345);
    msg->add_floats(12345.12345);
    msg->add_enums(deterministichashtest::FOO);
  }
  return msg;
}

static void bmHashByDeterministicHash(benchmark::State& state,
                                      std::unique_ptr<Protobuf::Message> msg) {
  uint64_t hash = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    hash += MessageUtil::hash(*msg);
  }
  benchmark::DoNotOptimize(hash);
}
BENCHMARK_CAPTURE(bmHashByDeterministicHash, map, testProtoWithMaps());
BENCHMARK_CAPTURE(bmHashByDeterministicHash, recursion, testProtoWithRecursion());
BENCHMARK_CAPTURE(bmHashByDeterministicHash, repeatedFields, testProtoWithRepeatedFields());

} // namespace Envoy
