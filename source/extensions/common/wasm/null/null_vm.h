#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "envoy/registry/registry.h"

#include "common/common/assert.h"

#include "extensions/common/wasm/null/null_vm_plugin.h"
#include "extensions/common/wasm/wasm_vm_base.h"
#include "extensions/common/wasm/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Null {

// The NullVm wraps a C++ WASM plugin which has been compiled with the WASM API
// and linked directly into the Envoy process. This is useful for development
// in that it permits the debugger to set breakpoints in both Envoy and the plugin.
struct NullVm : public WasmVmBase {
  NullVm(const Stats::ScopeSharedPtr& scope) : WasmVmBase(scope, WasmRuntimeNames::get().Null) {}
  NullVm(const NullVm& other)
      : WasmVmBase(other.scope_, WasmRuntimeNames::get().Null), plugin_name_(other.plugin_name_) {}

  // WasmVm
  absl::string_view runtime() override { return WasmRuntimeNames::get().Null; }
  Cloneable cloneable() override { return Cloneable::InstantiatedModule; };
  WasmVmPtr clone() override;
  bool load(const std::string& code, bool allow_precompiled) override;
  void link(absl::string_view debug_name) override;
  uint64_t getMemorySize() override;
  absl::optional<absl::string_view> getMemory(uint64_t pointer, uint64_t size) override;
  bool setMemory(uint64_t pointer, uint64_t size, const void* data) override;
  bool setWord(uint64_t pointer, Word data) override;
  bool getWord(uint64_t pointer, Word* data) override;
  absl::string_view getCustomSection(absl::string_view name) override;
  absl::string_view getPrecompiledSectionName() override;

#define _FORWARD_GET_FUNCTION(_T)                                                                  \
  void getFunction(absl::string_view function_name, _T* f) override {                              \
    plugin_->getFunction(function_name, f);                                                        \
  }
  FOR_ALL_WASM_VM_EXPORTS(_FORWARD_GET_FUNCTION)
#undef _FORWARD_GET_FUNCTION

  // These are not needed for NullVm which invokes the handlers directly.
#define _REGISTER_CALLBACK(_T)                                                                     \
  void registerCallback(absl::string_view, absl::string_view, _T,                                  \
                        typename ConvertFunctionTypeWordToUint32<_T>::type) override{};
  FOR_ALL_WASM_VM_IMPORTS(_REGISTER_CALLBACK)
#undef _REGISTER_CALLBACK

  std::string plugin_name_;
  NullVmPluginPtr plugin_;
};

} // namespace Null
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
