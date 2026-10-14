// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include <fstream>
#include <sstream>

#include "envoy/config/listener/v3/listener.pb.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

#include "test/common/protobuf/deterministic_hash_test.pb.h"

#include "benchmark/benchmark.h"
#include "yaml-cpp/yaml.h"

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

// --- loadFromYaml vs. loadFromJson on a realistic large Listener config ---
//
// Investigates the overhead of MessageUtil::loadFromFile's YAML path for configs
// at production LDS scale (one listener, ~12.7k virtual hosts). Fixtures are read
// from an absolute local path -- not bazel data deps -- since these are exploratory
// numbers for https://github.com/envoyproxy/envoy (yaml-parsing-perf issue), not yet
// a fixture intended to land in the repo.
//
// Set YAML_PERF_FIXTURE_DIR to point at a directory containing
// listener_prod_scale.json / listener_prod_scale.yaml (same content, two syntaxes).

namespace {

std::string envOr(const char* name, const std::string& fallback) {
  const char* v = std::getenv(name);
  return v != nullptr ? std::string(v) : fallback;
}

std::string readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  RELEASE_ASSERT(f.good(), absl::StrCat("failed to open fixture: ", path));
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::string fixtureDir() { return envOr("YAML_PERF_FIXTURE_DIR", "/tmp"); }

} // namespace

// Isolates yaml-cpp's own text->Node-tree parse, with nothing downstream at all --
// not even our own walk. Lower-bounds how much of loadFromYaml's cost is yaml-cpp
// itself vs. anything Envoy does with the resulting tree.
static void bmYamlCppLoadOnly(benchmark::State& state) {
  const std::string yaml = readFile(fixtureDir() + "/listener_prod_scale.yaml");
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    YAML::Node node = YAML::Load(yaml);
    benchmark::DoNotOptimize(node);
  }
}
BENCHMARK(bmYamlCppLoadOnly);

// Stage 1+2 only: yaml-cpp YAML::Load + parseYamlNode, i.e. building the
// intermediate Protobuf::Value tree. This is ValueUtil::loadFromYaml itself.
static void bmYamlToValueTree(benchmark::State& state) {
  const std::string yaml = readFile(fixtureDir() + "/listener_prod_scale.yaml");
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    Protobuf::Value value = ValueUtil::loadFromYaml(yaml);
    benchmark::DoNotOptimize(value);
  }
}
BENCHMARK(bmYamlToValueTree);

// Stage 3 only: re-serializing the intermediate Value tree back to JSON text
// (Protobuf::util::MessageToJsonString), isolated from stages 1/2/4.
static void bmValueTreeToJsonText(benchmark::State& state) {
  const std::string yaml = readFile(fixtureDir() + "/listener_prod_scale.yaml");
  Protobuf::Value value = ValueUtil::loadFromYaml(yaml);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    Protobuf::util::JsonPrintOptions json_options;
    json_options.preserve_proto_field_names = true;
    std::string json;
    const auto status = Protobuf::util::MessageToJsonString(value, &json, json_options);
    RELEASE_ASSERT(status.ok(), status.ToString());
    benchmark::DoNotOptimize(json);
  }
}
BENCHMARK(bmValueTreeToJsonText);

// Stage 4 only: the final, load-bearing JSON parse into the real destination
// message. This is the ONLY stage a JSON-sourced config pays for.
static void bmJsonTextToMessage(benchmark::State& state) {
  const std::string json = readFile(fixtureDir() + "/listener_prod_scale.json");
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    envoy::config::listener::v3::Listener listener;
    MessageUtil::loadFromJson(json, listener, ProtobufMessage::getStrictValidationVisitor());
    benchmark::DoNotOptimize(listener);
  }
}
BENCHMARK(bmJsonTextToMessage);

// End to end: today's MessageUtil::loadFromYaml, i.e. stages 1+2+3+4 combined,
// on the exact same content as bmJsonTextToMessage above (just YAML-syntax).
static void bmLoadFromYamlEndToEnd(benchmark::State& state) {
  const std::string yaml = readFile(fixtureDir() + "/listener_prod_scale.yaml");
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    envoy::config::listener::v3::Listener listener;
    MessageUtil::loadFromYaml(yaml, listener, ProtobufMessage::getStrictValidationVisitor());
    benchmark::DoNotOptimize(listener);
  }
}
BENCHMARK(bmLoadFromYamlEndToEnd);

// End to end: MessageUtil::loadFromJson on the same content, i.e. what
// loadFromFile would do today if the file had a .json extension instead --
// the speed a direct YAML::Node -> JSON-text emitter (skipping the
// Protobuf::Value tree entirely) should be able to approach.
static void bmLoadFromJsonEndToEnd(benchmark::State& state) {
  const std::string json = readFile(fixtureDir() + "/listener_prod_scale.json");
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    envoy::config::listener::v3::Listener listener;
    MessageUtil::loadFromJson(json, listener, ProtobufMessage::getStrictValidationVisitor());
    benchmark::DoNotOptimize(listener);
  }
}
BENCHMARK(bmLoadFromJsonEndToEnd);

} // namespace Envoy
