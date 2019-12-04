#pragma once

#include <memory>

#include "extensions/common/wasm/null/null_vm_plugin.h"
#include "extensions/common/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Null {
namespace Plugin {
using LogLevel = Envoy::Logger::Logger::Levels;
using MetricType = Envoy::Extensions::Common::Wasm::Context::MetricType;
using WasmResult = Envoy::Extensions::Common::Wasm::WasmResult;
using StringView = absl::string_view;
template <typename T> using Optional = absl::optional<T>;
} // namespace Plugin
} // namespace Null
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy

#include "extensions/common/wasm/null/wasm_api_impl.h"
namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Null {

struct NullPluginRegistry {
  uint32_t (*proxy_validate_configuration_)(uint32_t root_context_id,
                                            uint32_t plugin_configuration_size) = nullptr;
  uint32_t (*proxy_on_start_)(uint32_t root_context_id, uint32_t vm_configuration_size) = nullptr;
  uint32_t (*proxy_on_configure_)(uint32_t root_context_id,
                                  uint32_t plugin_configuration_size) = nullptr;
  void (*proxy_on_create_)(uint32_t context_id, uint32_t root_context_id) = nullptr;
  uint32_t (*proxy_on_done_)(uint32_t context_id) = nullptr;
  void (*proxy_on_delete_)(uint32_t context_id) = nullptr;
};

class NullPlugin : public NullVmPlugin {
public:
  explicit NullPlugin(NullPluginRegistry* registry) : registry_(registry) {}
  NullPlugin(const NullPlugin& other) : registry_(other.registry_) {}

#define _DECLARE_OVERRIDE(_t) void getFunction(absl::string_view function_name, _t* f) override;
  FOR_ALL_WASM_VM_EXPORTS(_DECLARE_OVERRIDE)
#undef _DECLARE_OVERRIDE

  uint64_t proxy_validate_configuration(uint64_t root_context_id,
                                        uint64_t plugin_configuration_size);
  uint64_t proxy_on_start(uint64_t root_context_id, uint64_t vm_configuration_size);
  uint64_t proxy_on_configure(uint64_t root_context_id, uint64_t plugin_configuration_size);
  void proxy_on_create(uint64_t context_id, uint64_t root_context_id);
  uint64_t proxy_on_done(uint64_t context_id);
  void proxy_on_delete(uint64_t context_id);

private:
  NullPluginRegistry* registry_{};
};

#define START_WASM_PLUGIN(_name)                                                                   \
  namespace Envoy {                                                                                \
  namespace Extensions {                                                                           \
  namespace Common {                                                                               \
  namespace Wasm {                                                                                 \
  namespace Null {                                                                                 \
  namespace Plugin {                                                                               \
  namespace _name {                                                                                \
  extern ThreadSafeSingleton<Null::NullPluginRegistry> null_plugin_registry_;

#define END_WASM_PLUGIN                                                                            \
  }                                                                                                \
  }                                                                                                \
  }                                                                                                \
  }                                                                                                \
  }                                                                                                \
  }                                                                                                \
  }

#define WASM_EXPORT(_t, _f, _a)                                                                    \
  _t _f _a;                                                                                        \
  static int register_export_##_f() {                                                              \
    null_plugin_registry_.get()._f##_ = _f;                                                        \
    return 0;                                                                                      \
  };                                                                                               \
  static int register_export_##_f##_ = register_export_##_f();                                     \
  _t _f _a

} // namespace Null
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
