#include "source/extensions/common/wasm/wasm_vm.h"

#include <algorithm>
#include <memory>

#include "source/extensions/common/wasm/context.h"
#include "source/extensions/common/wasm/ext/envoy_null_vm_wasm_api.h"
#include "source/extensions/common/wasm/stats_handler.h"
#include "source/extensions/common/wasm/wasm_runtime_factory.h"

#include "include/proxy-wasm/null_plugin.h"

using ContextBase = proxy_wasm::ContextBase;
using Word = proxy_wasm::Word;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

proxy_wasm::LogLevel EnvoyWasmVmIntegration::getLogLevel() {
  switch (ENVOY_LOGGER().level()) {
  case spdlog::level::trace:
    return proxy_wasm::LogLevel::trace;
  case spdlog::level::debug:
    return proxy_wasm::LogLevel::debug;
  case spdlog::level::info:
    return proxy_wasm::LogLevel::info;
  case spdlog::level::warn:
    return proxy_wasm::LogLevel::warn;
  case spdlog::level::err:
    return proxy_wasm::LogLevel::error;
  default:
    return proxy_wasm::LogLevel::critical;
  }
}

void EnvoyWasmVmIntegration::error(std::string_view message) { ENVOY_LOG(error, message); }
void EnvoyWasmVmIntegration::trace(std::string_view message) { ENVOY_LOG(trace, message); }

bool EnvoyWasmVmIntegration::getNullVmFunction(std::string_view function_name, bool returns_word,
                                               int number_of_arguments,
                                               proxy_wasm::NullPlugin* plugin,
                                               void* ptr_to_function_return) {
  if (function_name == "envoy_on_resolve_dns" && returns_word == false &&
      number_of_arguments == 3) {
    *reinterpret_cast<proxy_wasm::WasmCallVoid<3>*>(ptr_to_function_return) =
        [plugin](ContextBase* context, Word context_id, Word token, Word result_size) {
          proxy_wasm::SaveRestoreContext saved_context(context);
          // Need to add a new API header available to both .wasm and null vm targets.
          auto context_base = plugin->getContextBase(context_id);
          if (auto root = context_base->asRoot()) {
            static_cast<proxy_wasm::null_plugin::EnvoyRootContext*>(root)->onResolveDns(
                token, result_size);
          }
        };
    return true;
  } else if (function_name == "envoy_on_stats_update" && returns_word == false &&
             number_of_arguments == 2) {
    *reinterpret_cast<proxy_wasm::WasmCallVoid<2>*>(
        ptr_to_function_return) = [plugin](ContextBase* context, Word context_id,
                                           Word result_size) {
      proxy_wasm::SaveRestoreContext saved_context(context);
      // Need to add a new API header available to both .wasm and null vm targets.
      auto context_base = plugin->getContextBase(context_id);
      if (auto root = context_base->asRoot()) {
        static_cast<proxy_wasm::null_plugin::EnvoyRootContext*>(root)->onStatsUpdate(result_size);
      }
    };
    return true;
  }
  return false;
}

bool isWasmEngineAvailable(absl::string_view runtime) {
  auto runtime_factory = Registry::FactoryRegistry<WasmRuntimeFactory>::getFactory(runtime);
  return runtime_factory != nullptr;
}

absl::string_view getFirstAvailableWasmEngineName() {
  constexpr absl::string_view wasm_engines[] = {
      "envoy.wasm.runtime.v8", "envoy.wasm.runtime.wasmtime", "envoy.wasm.runtime.wamr",
      "envoy.wasm.runtime.wavm"};
  for (const auto wasm_engine : wasm_engines) {
    if (isWasmEngineAvailable(wasm_engine)) {
      return wasm_engine;
    }
  }
  return "";
}

WasmVmPtr createWasmVm(absl::string_view runtime) {
  // Set wasm runtime to built-in Wasm engine if it is not specified
  if (runtime.empty()) {
    runtime = getFirstAvailableWasmEngineName();
  }

  auto runtime_factory = Registry::FactoryRegistry<WasmRuntimeFactory>::getFactory(runtime);
  if (runtime_factory == nullptr) {
    ENVOY_LOG_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::wasm), warn,
        "Failed to create Wasm VM using {} runtime. Envoy was compiled without support for it",
        runtime);
    return nullptr;
  }

  auto wasm = runtime_factory->createWasmVm();
  wasm->integration() = std::make_unique<EnvoyWasmVmIntegration>();
  return wasm;
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
