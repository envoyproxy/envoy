#include "envoy/extensions/stat_sinks/wasm/v3/wasm.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/common/wasm/wasm.h"
#include "source/extensions/stat_sinks/wasm/config.h"
#include "source/extensions/stat_sinks/wasm/wasm_stat_sink_impl.h"

#include "test/extensions/common/wasm/wasm_runtime.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Wasm {

class WasmStatSinkConfigTest : public testing::TestWithParam<std::string> {
protected:
  WasmStatSinkConfigTest() {
    config_.mutable_config()->mutable_vm_config()->set_runtime(
        absl::StrCat("envoy.wasm.runtime.", GetParam()));
    if (GetParam() != "null") {
      config_.mutable_config()->mutable_vm_config()->mutable_code()->mutable_local()->set_filename(
          TestEnvironment::substitute(
              "{{ test_rundir "
              "}}/test/extensions/stats_sinks/wasm/test_data/test_context_cpp.wasm"));
    } else {
      config_.mutable_config()
          ->mutable_vm_config()
          ->mutable_code()
          ->mutable_local()
          ->set_inline_bytes("CommonWasmTestContextCpp");
    }
    config_.mutable_config()->set_name("test");
    // Allow restarts.
    config_.mutable_config()
        ->mutable_vm_config()
        ->mutable_restart_config()
        ->set_max_restart_per_minute(10);
  }

  void initializeWithConfig(const envoy::extensions::stat_sinks::wasm::v3::Wasm& config) {
    auto factory =
        Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(WasmName);
    ASSERT_NE(factory, nullptr);
    api_ = Api::createApiForTest(stats_store_);
    EXPECT_CALL(context_, api()).WillRepeatedly(testing::ReturnRef(*api_));
    EXPECT_CALL(context_, initManager()).WillRepeatedly(testing::ReturnRef(init_manager_));
    EXPECT_CALL(context_, lifecycleNotifier())
        .WillRepeatedly(testing::ReturnRef(lifecycle_notifier_));
    sink_ = factory->createStatsSink(config, context_);
    EXPECT_CALL(init_watcher_, ready());
    init_manager_.initialize(init_watcher_);
  }

  envoy::extensions::stat_sinks::wasm::v3::Wasm config_;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  testing::NiceMock<Server::MockServerLifecycleNotifier> lifecycle_notifier_;
  Init::ExpectableWatcherImpl init_watcher_;
  Stats::IsolatedStoreImpl stats_store_;
  Api::ApiPtr api_;
  Init::ManagerImpl init_manager_{"init_manager"};
  Stats::SinkPtr sink_;
};

INSTANTIATE_TEST_SUITE_P(Runtimes, WasmStatSinkConfigTest,
                         Envoy::Extensions::Common::Wasm::runtime_values);

TEST_P(WasmStatSinkConfigTest, CreateWasmFromEmpty) {
  envoy::extensions::stat_sinks::wasm::v3::Wasm config;
  EXPECT_THROW_WITH_MESSAGE(initializeWithConfig(config), Extensions::Common::Wasm::WasmException,
                            "Unable to create Wasm Stat Sink ");
}

TEST_P(WasmStatSinkConfigTest, CreateWasmFailOpen) {
  envoy::extensions::stat_sinks::wasm::v3::Wasm config;
  config.mutable_config()->set_fail_open(true);
  EXPECT_THROW_WITH_MESSAGE(initializeWithConfig(config), Extensions::Common::Wasm::WasmException,
                            "Unable to create Wasm Stat Sink ");
}

TEST_P(WasmStatSinkConfigTest, CreateWasmFromWASM) {
#if defined(__aarch64__)
  // TODO(PiotrSikora): There are no Emscripten releases for arm64.
  if (GetParam() != "null") {
    return;
  }
#endif
  initializeWithConfig(config_);

  EXPECT_NE(sink_, nullptr);
  NiceMock<Stats::MockMetricSnapshot> snapshot;
  sink_->flush(snapshot);
  NiceMock<Stats::MockHistogram> histogram;
  sink_->onHistogramComplete(histogram, 0);
}

TEST_P(WasmStatSinkConfigTest, PanicAndRestart) {
#if defined(__aarch64__)
  // TODO(PiotrSikora): There are no Emscripten releases for arm64.
  return;
#endif
  if (GetParam() == "null") {
    // NullVm cannot restart.
    return;
  } else if (GetParam() == "wamr") {
    // Somehow only WAMR fails in the restarts.
    // TODO: investigate the cause and enable.
    return;
  }
  initializeWithConfig(config_);
  ASSERT_NE(sink_, nullptr);

  auto wasm_sink = static_cast<WasmStatSink*>(sink_.get());
  auto plugin_handle_manager = wasm_sink->singletonForTesting();
  ASSERT_NE(plugin_handle_manager, nullptr);
  NiceMock<Stats::MockMetricSnapshot> snapshot;
  sink_->flush(snapshot);
  ASSERT_NE(plugin_handle_manager->getHealthyPluginHandle(), nullptr);
  // Cause panic.
  sink_->flush(snapshot);
  // The current handle should be in the failed condition - getHealthyPluginHandle gives nullptr
  ASSERT_EQ(plugin_handle_manager->getHealthyPluginHandle(), nullptr);
  // Call flush again - internally tryRestartPlugin is called and the new VM is used.
  sink_->flush(snapshot);
  // The current handle actually should not be in the failed state.
  ASSERT_NE(plugin_handle_manager->getHealthyPluginHandle(), nullptr);
}

} // namespace Wasm
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
