#include "extensions/common/wasm/null/null.h"

#include <memory>
#include <utility>
#include <vector>

#include "envoy/registry/registry.h"

#include "common/common/assert.h"

#include "extensions/common/wasm/null/null_vm.h"
#include "extensions/common/wasm/null/null_vm_plugin.h"
#include "extensions/common/wasm/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Null {

WasmVmPtr createVm() { return std::make_unique<NullVm>(); }

} // namespace Null
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
