#pragma once

#include <memory>

#include "envoy/config/typed_config.h"

#include "extensions/common/wasm/wasm_vm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Null {

// A wrapper for the natively compiled NullVm plugin which implements the WASM ABI.
class NullVmPlugin {
public:
  NullVmPlugin() = default;
  virtual ~NullVmPlugin() = default;

  // NB: These are defined rather than declared PURE because gmock uses __LINE__ internally for
  // uniqueness, making it impossible to use FOR_ALL_WASM_VM_EXPORTS with MOCK_METHOD.
#define _DEFINE_GET_FUNCTION(_T)                                                                   \
  virtual void getFunction(absl::string_view, _T* f) { *f = nullptr; }
  FOR_ALL_WASM_VM_EXPORTS(_DEFINE_GET_FUNCTION)
#undef _DEFIN_GET_FUNCTIONE
};

using NullVmPluginPtr = std::unique_ptr<NullVmPlugin>;

/**
 * Pseudo-WASM plugins using the NullVM should implement this factory and register via
 * Registry::registerFactory or the convenience class RegisterFactory.
 */
class NullVmPluginFactory : public Config::UntypedFactory {
public:
  ~NullVmPluginFactory() override = default;

  std::string category() const override { return "envoy.wasm.null_vms"; }

  /**
   * Create an instance of the plugin.
   */
  virtual NullVmPluginPtr create() const PURE;
};

} // namespace Null
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
