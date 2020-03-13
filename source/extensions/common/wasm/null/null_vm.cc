#include "extensions/common/wasm/null/null_vm.h"

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

WasmVmPtr NullVm::clone() {
  auto cloned_null_vm = std::make_unique<NullVm>(*this);
  cloned_null_vm->load(plugin_name_, false /* unused */);
  return cloned_null_vm;
}

// "Load" the plugin by obtaining a pointer to it from the factory.
bool NullVm::load(const std::string& name, bool /* allow_precompiled */) {
  auto factory = Registry::FactoryRegistry<NullVmPluginFactory>::getFactory(name);
  if (!factory) {
    return false;
  }
  plugin_name_ = name;
  plugin_ = factory->create();
  return true;
}

void NullVm::link(absl::string_view /* name */) {}

uint64_t NullVm::getMemorySize() { return std::numeric_limits<uint64_t>::max(); }

// NulVm pointers are just native pointers.
absl::optional<absl::string_view> NullVm::getMemory(uint64_t pointer, uint64_t size) {
  if (pointer == 0 && size != 0) {
    return absl::nullopt;
  }
  return absl::string_view(reinterpret_cast<char*>(pointer), static_cast<size_t>(size));
}

bool NullVm::setMemory(uint64_t pointer, uint64_t size, const void* data) {
  if ((pointer == 0 || data == nullptr)) {
    if (size != 0) {
      return false;
    } else {
      return true;
    }
  }
  auto p = reinterpret_cast<char*>(pointer);
  memcpy(p, data, size);
  return true;
}

bool NullVm::setWord(uint64_t pointer, Word data) {
  if (pointer == 0) {
    return false;
  }
  auto p = reinterpret_cast<char*>(pointer);
  memcpy(p, &data.u64_, sizeof(data.u64_));
  return true;
}

bool NullVm::getWord(uint64_t pointer, Word* data) {
  if (pointer == 0) {
    return false;
  }
  auto p = reinterpret_cast<char*>(pointer);
  memcpy(&data->u64_, p, sizeof(data->u64_));
  return true;
}

absl::string_view NullVm::getCustomSection(absl::string_view /* name */) {
  // Return nothing: there is no WASM file.
  return {};
}

absl::string_view NullVm::getPrecompiledSectionName() {
  // Return nothing: there is no WASM file.
  return {};
}

} // namespace Null
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
