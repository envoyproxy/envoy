#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include <dlfcn.h>

#include <filesystem>
#include <string>

#include "envoy/common/exception.h"

#include "source/extensions/dynamic_modules/abi.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

absl::StatusOr<DynamicModuleSharedPtr> newDynamicModule(const absl::string_view object_file_path,
                                                        const bool do_not_close) {
  // RTLD_LOCAL is always needed to avoid collisions between multiple modules.
  // RTLD_LAZY is required for not only performance but also simply to load the module, otherwise
  // dlopen results in Invalid argument.
  int mode = RTLD_LOCAL | RTLD_LAZY;
  if (do_not_close) {
    mode |= RTLD_NODELETE;
  }

  const std::filesystem::path file_path_absolute = std::filesystem::absolute(object_file_path);
  void* handle = dlopen(file_path_absolute.c_str(), mode);
  if (handle == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to load dynamic module: ", object_file_path, " : ", dlerror()));
  }

  DynamicModuleSharedPtr dynamic_module = std::make_shared<DynamicModule>(handle);

  const auto init_function = dynamic_module->getFunctionPointer<
      FunctionPointerTypeFromDeclaration<decltype(&envoy_dynamic_module_on_program_init)>>(
      "envoy_dynamic_module_on_program_init");

  if (init_function == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to resolve envoy_dynamic_module_on_program_init: ", dlerror()));
  }

  const size_t result = (*init_function)();
  if (result != 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("envoy_dynamic_module_on_program_init failed: ", object_file_path,
                     " : returned non-zero status: ", result));
  }
  return dynamic_module;
}

DynamicModule::~DynamicModule() { dlclose(handle_); }

void* DynamicModule::getSymbol(const absl::string_view symbol_ref) const {
  // TODO(mathetake): maybe we should accept null-terminated const char* instead of string_view to
  // avoid unnecessary copy because it is likely that this is only called for a constant string,
  // though this is not a performance critical path.
  return dlsym(handle_, std::string(symbol_ref).c_str());
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
