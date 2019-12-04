#include "extensions/common/wasm/null/null_plugin.h"

#include <stdint.h>
#include <stdio.h>

#include <atomic>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/logger.h"

#include "extensions/common/wasm/null/null_vm.h"
#include "extensions/common/wasm/wasm.h"
#include "extensions/common/wasm/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Null {

void NullPlugin::getFunction(absl::string_view function_name, WasmCallVoid<0>* f) {
  if (function_name == "_start") {
    *f = nullptr;
  } else if (function_name == "__wasm_call_ctors") {
    *f = nullptr;
  } else {
    throw WasmVmException(fmt::format("Missing getFunction for: {}", function_name));
  }
}

void NullPlugin::getFunction(absl::string_view function_name, WasmCallVoid<1>* f) {
  auto plugin = this;
  if (function_name == "free") {
    *f = [](Common::Wasm::Context*, Word ptr) { return ::free(reinterpret_cast<void*>(ptr.u64_)); };
  } else if (function_name == "proxy_on_delete") {
    *f = [plugin](Common::Wasm::Context* context, Word context_id) {
      SaveRestoreContext saved_context(context);
      plugin->proxy_on_delete(context_id.u64_);
    };
  } else {
    throw WasmVmException(fmt::format("Missing getFunction for: {}", function_name));
  }
}

void NullPlugin::getFunction(absl::string_view function_name, WasmCallVoid<2>* f) {
  auto plugin = this;
  if (function_name == "proxy_on_create") {
    *f = [plugin](Common::Wasm::Context* context, Word context_id, Word root_context_id) {
      SaveRestoreContext saved_context(context);
      plugin->proxy_on_create(context_id.u64_, root_context_id.u64_);
    };
  } else {
    throw WasmVmException(fmt::format("Missing getFunction for: {}", function_name));
  }
}

void NullPlugin::getFunction(absl::string_view function_name, WasmCallVoid<3>* /* f */) {
  throw WasmVmException(fmt::format("Missing getFunction for: {}", function_name));
}

void NullPlugin::getFunction(absl::string_view function_name, WasmCallVoid<5>* /* f */) {
  throw WasmVmException(fmt::format("Missing getFunction for: {}", function_name));
}

void NullPlugin::getFunction(absl::string_view function_name, WasmCallWord<1>* f) {
  auto plugin = this;
  if (function_name == "malloc") {
    *f = [](Common::Wasm::Context*, Word size) -> Word {
      return Word(reinterpret_cast<uint64_t>(::malloc(size.u64_)));
    };
  } else if (function_name == "proxy_on_done") {
    *f = [plugin](Common::Wasm::Context* context, Word context_id) {
      SaveRestoreContext saved_context(context);
      return Word(plugin->proxy_on_done(context_id.u64_));
    };
  } else {
    throw WasmVmException(fmt::format("Missing getFunction for: {}", function_name));
  }
}

void NullPlugin::getFunction(absl::string_view function_name, WasmCallWord<2>* f) {
  auto plugin = this;
  if (function_name == "proxy_on_start") {
    *f = [plugin](Common::Wasm::Context* context, Word context_id, Word configuration_size) {
      SaveRestoreContext saved_context(context);
      return Word(plugin->proxy_on_start(context_id.u64_, configuration_size.u64_));
    };
  } else if (function_name == "proxy_on_configure") {
    *f = [plugin](Common::Wasm::Context* context, Word context_id, Word configuration_size) {
      SaveRestoreContext saved_context(context);
      return Word(plugin->proxy_on_configure(context_id.u64_, configuration_size.u64_));
    };
  } else if (function_name == "proxy_validate_configuration") {
    *f = [plugin](Common::Wasm::Context* context, Word context_id, Word configuration_size) {
      SaveRestoreContext saved_context(context);
      return Word(plugin->proxy_validate_configuration(context_id.u64_, configuration_size.u64_));
    };
  } else {
    throw WasmVmException(fmt::format("Missing getFunction for: {}", function_name));
  }
}

void NullPlugin::getFunction(absl::string_view function_name, WasmCallWord<3>* /* f */) {
  throw WasmVmException(fmt::format("Missing getFunction for: {}", function_name));
}

uint64_t NullPlugin::proxy_validate_configuration(uint64_t root_context_id,
                                                  uint64_t plugin_configuration_size_) {
  if (registry_->proxy_validate_configuration_) {
    return registry_->proxy_validate_configuration_(root_context_id, plugin_configuration_size_);
  }
  return 1;
}

uint64_t NullPlugin::proxy_on_start(uint64_t root_context_id, uint64_t vm_configuration_size) {
  if (registry_->proxy_on_start_) {
    return registry_->proxy_on_start_(root_context_id, vm_configuration_size);
  }
  return 1;
}

uint64_t NullPlugin::proxy_on_configure(uint64_t root_context_id,
                                        uint64_t plugin_configuration_size) {
  if (registry_->proxy_on_configure_) {
    return registry_->proxy_on_configure_(root_context_id, plugin_configuration_size);
  }
  return 1;
}

void NullPlugin::proxy_on_create(uint64_t context_id, uint64_t root_context_id) {
  if (registry_->proxy_on_create_) {
    registry_->proxy_on_create_(context_id, root_context_id);
  }
}

uint64_t NullPlugin::proxy_on_done(uint64_t context_id) {
  if (registry_->proxy_on_done_) {
    return registry_->proxy_on_done_(context_id);
  }
  return 1;
}

void NullPlugin::proxy_on_delete(uint64_t context_id) {
  if (registry_->proxy_on_delete_) {
    registry_->proxy_on_delete_(context_id);
  }
}

} // namespace Null
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
