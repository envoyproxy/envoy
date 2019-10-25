#pragma once

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
  // uniqueness, making it impossible to use FOR_ALL_WASM_VM_EXPORTS with MOCK_METHOD2.
#define _DEFINE_GET_FUNCTION(_T)                                                                   \
  virtual void getFunction(absl::string_view, _T* f) { *f = nullptr; }
  FOR_ALL_WASM_VM_EXPORTS(_DEFINE_GET_FUNCTION)
#undef _DEFIN_GET_FUNCTIONE
};

/**
 * Pseudo-WASM plugins using the NullVM should implement this factory and register via
 * Registry::registerFactory or the convenience class RegisterFactory.
 */
class NullVmPluginFactory {
public:
  virtual ~NullVmPluginFactory() = default;

  /**
   * Name of the plugin.
   */
  virtual const std::string name() const PURE;

  /**
   * Create an instance of the plugin.
   */
  virtual std::unique_ptr<NullVmPlugin> create() const PURE;
};

} // namespace Null
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
