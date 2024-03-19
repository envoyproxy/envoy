/**
 * Simple WASM speed test.
 *
 * Run with:
 * `bazel run --config=libc++ -c opt //test/extensions/bootstrap/wasm:wasm_speed_test`
 */
#include "source/common/event/dispatcher_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/common/wasm/wasm.h"

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
  proxy_wasm::WasmResult log(uint32_t level, std::string_view message) override {
    log_(static_cast<spdlog::level::level_enum>(level), toAbslStringView(message));
    return proxy_wasm::WasmResult::Ok;
  }
  MOCK_METHOD(void, log_, (spdlog::level::level_enum level, absl::string_view message));
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

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_root_id() = "some_long_root_id";
  plugin_config.mutable_vm_config()->mutable_configuration()->set_value(test);
  plugin_config.mutable_vm_config()->set_runtime(absl::StrCat("envoy.wasm.runtime.", runtime));
  auto plugin = std::make_shared<Extensions::Common::Wasm::Plugin>(
      plugin_config, envoy::config::core::v3::TrafficDirection::UNSPECIFIED, local_info, nullptr);
  auto wasm = std::make_unique<Extensions::Common::Wasm::Wasm>(
      plugin->wasmConfig(), "vm_key", scope, *api, cluster_manager, *dispatcher);
  std::string code;
  if (runtime == "null") {
    code = "WasmSpeedCpp";
  } else {
    code = TestEnvironment::readFileToStringForTest(
        TestEnvironment::runfilesPath("test/extensions/bootstrap/wasm/test_data/speed_cpp.wasm"));
  }
  EXPECT_FALSE(code.empty());
  EXPECT_TRUE(wasm->load(code, false));
  EXPECT_TRUE(wasm->initialize());
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

#if defined(PROXY_WASM_HAS_RUNTIME_V8)
#define B(_t)                                                                                      \
  BENCHMARK_CAPTURE(bmWasmSimpleCallSpeedTest, NullSpeedTest_##_t, std::string(#_t),               \
                    std::string("null"));                                                          \
  BENCHMARK_CAPTURE(bmWasmSimpleCallSpeedTest, WasmSpeedTest_##_t, std::string(#_t),               \
                    std::string("v8"));
#elif defined(PROXY_WASM_HAS_RUNTIME_WAMR)
#define B(_t)                                                                                      \
  BENCHMARK_CAPTURE(bmWasmSimpleCallSpeedTest, NullSpeedTest_##_t, std::string(#_t),               \
                    std::string("null"));                                                          \
  BENCHMARK_CAPTURE(bmWasmSimpleCallSpeedTest, WasmSpeedTest_##_t, std::string(#_t),               \
                    std::string("wamr"));
#elif defined(PROXY_WASM_HAS_RUNTIME_WASMTIME)
#define B(_t)                                                                                      \
  BENCHMARK_CAPTURE(bmWasmSimpleCallSpeedTest, NullSpeedTest_##_t, std::string(#_t),               \
                    std::string("null"));                                                          \
  BENCHMARK_CAPTURE(bmWasmSimpleCallSpeedTest, WasmSpeedTest_##_t, std::string(#_t),               \
                    std::string("wasmtime"));
#else
#define B(_t)                                                                                      \
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
B(json_deserialize)
B(json_deserialize_empty)
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
