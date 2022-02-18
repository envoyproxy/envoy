#include "source/common/common/thread.h"
#include "source/common/common/thread_synchronizer.h"
#include "source/extensions/common/wasm/wasm.h"

#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/thread_factory_for_test.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tools/cpp/runfiles/runfiles.h"

using bazel::tools::cpp::runfiles::Runfiles;

namespace Envoy {

void bmWasmSpeedTest(benchmark::State& state) {
  Envoy::Thread::MutexBasicLockable lock;
  Envoy::Logger::Context logging_state(spdlog::level::warn,
                                       Envoy::Logger::Logger::DEFAULT_LOG_FORMAT, lock, false);
  Envoy::Logger::Registry::getLog(Envoy::Logger::Id::wasm).set_level(spdlog::level::off);
  Envoy::Stats::IsolatedStoreImpl stats_store;
  Envoy::Api::ApiPtr api = Envoy::Api::createApiForTest(stats_store);
  Envoy::Upstream::MockClusterManager cluster_manager;
  Envoy::Event::DispatcherPtr dispatcher(api->allocateDispatcher("wasm_test"));
  auto scope = Envoy::Stats::ScopeSharedPtr(stats_store.createScope("wasm."));
  proxy_wasm::AllowedCapabilitiesMap allowed_capabilities;

  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  *plugin_config.mutable_vm_config()->mutable_runtime() = "envoy.wasm.runtime.null";
  auto config = Envoy::Extensions::Common::Wasm::WasmConfig(plugin_config);
  auto wasm = std::make_unique<Envoy::Extensions::Common::Wasm::Wasm>(config, "", scope, *api,
                                                                      cluster_manager, *dispatcher);

  auto context = std::make_shared<Envoy::Extensions::Common::Wasm::Context>(wasm.get());
  Envoy::Thread::ThreadFactory& thread_factory{Envoy::Thread::threadFactoryForTest()};
  std::pair<std::string, uint32_t> data;
  int n_threads = 10;

  for (__attribute__((unused)) auto _ : state) {
    auto thread_fn = [&]() {
      for (int i = 0; i < 1000000; i++) {
        context->getSharedData("foo", &data);
        context->setSharedData("foo", "bar", 1);
      }
      return new uint32_t(42);
    };
    std::vector<Envoy::Thread::ThreadPtr> threads;
    for (int i = 0; i < n_threads; ++i) {
      std::string name = absl::StrCat("thread", i);
      threads.emplace_back(thread_factory.createThread(thread_fn, Envoy::Thread::Options{name}));
    }
    for (auto& thread : threads) {
      thread->join();
    }
  }
}

BENCHMARK(bmWasmSpeedTest);

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
