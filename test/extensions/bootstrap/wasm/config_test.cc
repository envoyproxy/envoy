#include "envoy/common/exception.h"
#include "envoy/extensions/wasm/v3/wasm.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/bootstrap/wasm/config.h"

#include "test/extensions/common/wasm/wasm_runtime.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Wasm {

class WasmFactoryTest : public testing::TestWithParam<std::tuple<std::string, std::string>> {
protected:
  WasmFactoryTest() {
    config_.mutable_config()->mutable_vm_config()->set_runtime(
        absl::StrCat("envoy.wasm.runtime.", std::get<0>(GetParam())));
    if (std::get<0>(GetParam()) != "null") {
      config_.mutable_config()->mutable_vm_config()->mutable_code()->mutable_local()->set_filename(
          TestEnvironment::substitute(
              "{{ test_rundir }}/test/extensions/bootstrap/wasm/test_data/start_cpp.wasm"));
    } else {
      config_.mutable_config()
          ->mutable_vm_config()
          ->mutable_code()
          ->mutable_local()
          ->set_inline_bytes("WasmStartCpp");
    }
    config_.mutable_config()->set_name("test");
    config_.set_singleton(true);
  }

  void initializeWithConfig(const envoy::extensions::wasm::v3::WasmService& config) {
    auto factory =
        Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::getFactory(
            "envoy.bootstrap.wasm");
    ASSERT_NE(factory, nullptr);
    api_ = Api::createApiForTest(stats_store_);
    EXPECT_CALL(context_, api()).WillRepeatedly(testing::ReturnRef(*api_));
    EXPECT_CALL(context_, initManager()).WillRepeatedly(testing::ReturnRef(init_manager_));
    EXPECT_CALL(context_, lifecycleNotifier())
        .WillRepeatedly(testing::ReturnRef(lifecycle_notifier_));
    extension_ = factory->createBootstrapExtension(config, context_);
    extension_->onServerInitialized();
    static_cast<Bootstrap::Wasm::WasmServiceExtension*>(extension_.get())->wasmService();
    EXPECT_CALL(init_watcher_, ready());
    init_manager_.initialize(init_watcher_);
  }

  envoy::extensions::wasm::v3::WasmService config_;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  testing::NiceMock<Server::MockServerLifecycleNotifier> lifecycle_notifier_;
  Init::ExpectableWatcherImpl init_watcher_;
  Stats::IsolatedStoreImpl stats_store_;
  Api::ApiPtr api_;
  Init::ManagerImpl init_manager_{"init_manager"};
  Server::BootstrapExtensionPtr extension_;
};

INSTANTIATE_TEST_SUITE_P(Runtimes, WasmFactoryTest,
                         Envoy::Extensions::Common::Wasm::runtime_and_cpp_values,
                         Envoy::Extensions::Common::Wasm::wasmTestParamsToString);

TEST_P(WasmFactoryTest, CreateWasmFromWasm) {
  auto factory = std::make_unique<Bootstrap::Wasm::WasmFactory>();
  auto empty_config = factory->createEmptyConfigProto();

  initializeWithConfig(config_);

  EXPECT_NE(extension_, nullptr);

  // Check if the custom stat namespace is registered during the initialization.
  EXPECT_TRUE(api_->customStatNamespaces().registered("wasmcustom"));
}

TEST_P(WasmFactoryTest, CreateWasmFromWasmPerThread) {
  config_.set_singleton(false);
  initializeWithConfig(config_);

  EXPECT_NE(extension_, nullptr);
  extension_.reset();
  context_.threadLocal().shutdownThread();
}

TEST_P(WasmFactoryTest, MissingImport) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  config_.mutable_config()->mutable_vm_config()->mutable_code()->mutable_local()->set_filename(
      TestEnvironment::substitute(
          "{{ test_rundir }}/test/extensions/bootstrap/wasm/test_data/missing_cpp.wasm"));
  EXPECT_THROW_WITH_MESSAGE(initializeWithConfig(config_), Extensions::Common::Wasm::WasmException,
                            "Unable to create Wasm plugin test");
}

TEST_P(WasmFactoryTest, UnspecifiedRuntime) {
  if (std::get<0>(GetParam()) == "null") {
    return;
  }
  config_.mutable_config()->mutable_vm_config()->set_runtime("");

  EXPECT_NO_THROW(initializeWithConfig(config_));
}

TEST_P(WasmFactoryTest, UnknownRuntime) {
  config_.mutable_config()->mutable_vm_config()->set_runtime("envoy.wasm.runtime.invalid");

  EXPECT_THROW_WITH_MESSAGE(initializeWithConfig(config_), Extensions::Common::Wasm::WasmException,
                            "Unable to create Wasm plugin test");
}

TEST_P(WasmFactoryTest, StartFailed) {
  ProtobufWkt::StringValue plugin_configuration;
  plugin_configuration.set_value("bad");
  config_.mutable_config()->mutable_vm_config()->mutable_configuration()->PackFrom(
      plugin_configuration);

  EXPECT_THROW_WITH_MESSAGE(initializeWithConfig(config_), Extensions::Common::Wasm::WasmException,
                            "Unable to create Wasm plugin test");
}

TEST_P(WasmFactoryTest, StartFailedOpen) {
  ProtobufWkt::StringValue plugin_configuration;
  plugin_configuration.set_value("bad");
  config_.mutable_config()->mutable_vm_config()->mutable_configuration()->PackFrom(
      plugin_configuration);
  config_.mutable_config()->set_fail_open(true);

  EXPECT_THROW_WITH_MESSAGE(initializeWithConfig(config_), Extensions::Common::Wasm::WasmException,
                            "Unable to create Wasm plugin test");
}

TEST_P(WasmFactoryTest, ConfigureFailed) {
  ProtobufWkt::StringValue plugin_configuration;
  plugin_configuration.set_value("bad");
  config_.mutable_config()->mutable_configuration()->PackFrom(plugin_configuration);

  EXPECT_THROW_WITH_MESSAGE(initializeWithConfig(config_), Extensions::Common::Wasm::WasmException,
                            "Unable to create Wasm plugin test");
}

} // namespace Wasm
} // namespace Extensions
} // namespace Envoy
