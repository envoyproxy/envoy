/**
 * Simple WASM speed test.
 *
 * Run with:
 * `bazel run --config=libc++ -c opt //test/extensions/bootstrap/wasm:wasm_speed_test`
 */
#include "common/event/dispatcher_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "extensions/common/wasm/wasm.h"

#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/types/optional.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tools/cpp/runfiles/runfiles.h"

using bazel::tools::cpp::runfiles::Runfiles;

namespace Envoy {
namespace Extensions {
namespace Wasm {

class TestRoot : public Envoy::Extensions::Common::Wasm::Context {
public:
  TestRoot(Extensions::Common::Wasm::Wasm* wasm,
           const std::shared_ptr<Extensions::Common::Wasm::Plugin>& plugin)
      : Envoy::Extensions::Common::Wasm::Context(wasm, plugin) {}

  using Envoy::Extensions::Common::Wasm::Context::log;
  proxy_wasm::WasmResult log(uint32_t level, absl::string_view message) override {
    log_(static_cast<spdlog::level::level_enum>(level), message);
    return proxy_wasm::WasmResult::Ok;
  }
  MOCK_METHOD2(log_, void(spdlog::level::level_enum level, absl::string_view message));
};

static void bmWasmSimpleCallSpeedTest(benchmark::State& state, std::string test,
                                      std::string runtime) {
  Envoy::Logger::Registry::getLog(Logger::Id::wasm).set_level(spdlog::level::off);
  Stats::IsolatedStoreImpl stats_store;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  Upstream::MockClusterManager cluster_manager;
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  auto name = "";
  auto root_id = "some_long_root_id";
  auto vm_id = "";
  auto vm_configuration = test;
  auto vm_key = "";
  auto plugin_configuration = "";
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      name, root_id, vm_id, runtime, plugin_configuration, false,
      envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(
      absl::StrCat("envoy.wasm.runtime.", runtime), vm_id, vm_configuration, vm_key, scope,
      cluster_manager, *dispatcher);
  std::string code;
  if (runtime == "null") {
    code = "WasmSpeedCpp";
  } else {
    code = TestEnvironment::readFileToStringForTest(
        TestEnvironment::runfilesPath("test/extensions/bootstrap/wasm/test_data/speed_cpp.wasm"));
  }
  EXPECT_FALSE(code.empty());
  EXPECT_TRUE(wasm->initialize(code, false));
  wasm->setCreateContextForTesting(
      nullptr,
      [](Extensions::Common::Wasm::Wasm* wasm,
         const std::shared_ptr<Extensions::Common::Wasm::Plugin>& plugin)
          -> proxy_wasm::ContextBase* { return new TestRoot(wasm, plugin); });

  auto root_context = wasm->start(plugin);
  for (__attribute__((unused)) auto _ : state) {
    root_context->onTick(0);
  }
}

#if defined(ENVOY_WASM_WAVM)
#define B(_t)                                                                                      \
  BENCHMARK_CAPTURE(bmWasmSimpleCallSpeedTest, V8SpeedTest_##_t, std::string(#_t),                 \
                    std::string("v8"));                                                            \
  BENCHMARK_CAPTURE(bmWasmSimpleCallSpeedTest, NullSpeedTest_##_t, std::string(#_t),               \
                    std::string("null"));                                                          \
  BENCHMARK_CAPTURE(bmWasmSimpleCallSpeedTest, WavmSpeedTest_##_t, std::string(#_t),               \
                    std::string("wavm"));
#else
#define B(_t)                                                                                      \
  BENCHMARK_CAPTURE(bmWasmSimpleCallSpeedTest, V8SpeedTest_##_t, std::string(#_t),                 \
                    std::string("v8"));                                                            \
  BENCHMARK_CAPTURE(bmWasmSimpleCallSpeedTest, NullSpeedTest_##_t, std::string(#_t),               \
                    std::string("null"));
#endif

B(empty)
B(get_current_time)
B(small_string)
B(small_string1000)
B(small_string_check_compiler)
B(small_string_check_compiler1000)
B(large_string)
B(large_string1000)
B(get_property)
B(grpc_service)
B(grpc_service1000)
B(modify_metadata)
B(modify_metadata1000)
B(json_serialize)
B(json_serialize_arena)
B(json_deserialize)
B(json_deserialize_arena)
B(json_deserialize_empty)
B(json_serialize_deserialize)
B(convert_to_filter_state)

} // namespace Wasm
} // namespace Extensions
} // namespace Envoy

int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  Envoy::TestEnvironment::initializeOptions(argc, argv);
  // Create a Runfiles object for runfiles lookup.
  // https://github.com/bazelbuild/bazel/blob/master/tools/cpp/runfiles/runfiles_src.h#L32
  std::string error;
  std::unique_ptr<Runfiles> runfiles(Runfiles::Create(argv[0], &error));
  RELEASE_ASSERT(Envoy::TestEnvironment::getOptionalEnvVar("NORUNFILES").has_value() ||
                     runfiles != nullptr,
                 error);
  Envoy::TestEnvironment::setRunfiles(runfiles.get());
  Envoy::TestEnvironment::setEnvVar("ENVOY_IP_TEST_VERSIONS", "all", 0);
  Envoy::Event::Libevent::Global::initialize();
  if (::benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  ::benchmark::RunSpecifiedBenchmarks();
  return 0;
}
