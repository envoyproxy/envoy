#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "envoy/registry/registry.h"

#include "common/common/assert.h"

#include "extensions/common/wasm/null/null_vm_plugin.h"
#include "extensions/common/wasm/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Null {

struct NullVm : public WasmVm {
  NullVm() = default;
  NullVm(const NullVm& other) { load(other.plugin_name_, false /* unused */); }
  ~NullVm() override{};

  // WasmVm
  absl::string_view vm() override { return WasmVmNames::get().Null; }
  bool clonable() override { return true; };
  std::unique_ptr<WasmVm> clone() override;
  bool load(const std::string& code, bool allow_precompiled) override;
  void link(absl::string_view debug_name, bool needs_emscripten) override;
  void setMemoryLayout(uint64_t, uint64_t, uint64_t) override {}
  void start(Common::Wasm::Context* context) override;
  uint64_t getMemorySize() override;
  absl::optional<absl::string_view> getMemory(uint64_t pointer, uint64_t size) override;
  bool getMemoryOffset(void* host_pointer, uint64_t* vm_pointer) override;
  bool setMemory(uint64_t pointer, uint64_t size, const void* data) override;
  bool setWord(uint64_t pointer, Word data) override;
  void makeModule(absl::string_view name) override;
  absl::string_view getUserSection(absl::string_view name) override;

#define _FORWARD_GET_FUNCTION(_T)                                                                  \
  void getFunction(absl::string_view functionName, _T* f) override {                               \
    plugin_->getFunction(functionName, f);                                                         \
  }
  FOR_ALL_WASM_VM_EXPORTS(_FORWARD_GET_FUNCTION)
#undef _FORWARD_GET_FUNCTION

  // These are noops for NullVm.
#define _REGISTER_CALLBACK(_type)                                                                  \
  void registerCallback(absl::string_view, absl::string_view, _type,                               \
                        typename ConvertFunctionTypeWordToUint32<_type>::type) override{};
  FOR_ALL_WASM_VM_IMPORTS(_REGISTER_CALLBACK)
#undef _REGISTER_CALLBACK

  // NullVm does not advertize code as emscripten so this will not get called.
  std::unique_ptr<Global<double>> makeGlobal(absl::string_view, absl::string_view,
                                             double) override {
    NOT_REACHED_GCOVR_EXCL_LINE;
  };
  std::unique_ptr<Global<Word>> makeGlobal(absl::string_view, absl::string_view, Word) override {
    NOT_REACHED_GCOVR_EXCL_LINE;
  };

  std::string plugin_name_;
  std::unique_ptr<NullVmPlugin> plugin_;
};

} // namespace Null
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
