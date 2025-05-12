#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include <dlfcn.h>

#include <string>

#include "envoy/common/exception.h"

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/abi_version.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

constexpr char DYNAMIC_MODULES_SEARCH_PATH[] = "ENVOY_DYNAMIC_MODULES_SEARCH_PATH";

absl::StatusOr<DynamicModulePtr>
newDynamicModule(const std::filesystem::path& object_file_absolute_path, const bool do_not_close) {
  // From the man page of dlopen(3):
  //
  // > This can be used to test if the object is already resident (dlopen() returns NULL if it
  // > is not, or the object's handle if it is resident).
  //
  // So we can use RTLD_NOLOAD to check if the module is already loaded to avoid the duplicate call
  // to the init function.
  void* handle = dlopen(object_file_absolute_path.c_str(), RTLD_NOLOAD | RTLD_LAZY);
  if (handle != nullptr) {
    // This means the module is already loaded, and the return value is the handle of the already
    // loaded module. We don't need to call the init function again.
    return std::make_unique<DynamicModule>(handle);
  }
  // RTLD_LOCAL is always needed to avoid collisions between multiple modules.
  // RTLD_LAZY is required for not only performance but also simply to load the module, otherwise
  // dlopen results in Invalid argument.
  int mode = RTLD_LOCAL | RTLD_LAZY;
  if (do_not_close) {
    mode |= RTLD_NODELETE;
  }
  handle = dlopen(object_file_absolute_path.c_str(), mode);
  if (handle == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to load dynamic module: ", object_file_absolute_path.c_str(), " : ", dlerror()));
  }

  DynamicModulePtr dynamic_module = std::make_unique<DynamicModule>(handle);

  const auto init_function =
      dynamic_module->getFunctionPointer<decltype(&envoy_dynamic_module_on_program_init)>(
          "envoy_dynamic_module_on_program_init");

  if (!init_function.ok()) {
    return init_function.status();
  }

  const char* abi_version = (*init_function.value())();
  if (abi_version == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to initialize dynamic module: ", object_file_absolute_path.c_str()));
  }
  // Checks the kAbiVersion and the version of the dynamic module.
  if (absl::string_view(abi_version) != absl::string_view(kAbiVersion)) {
    return absl::InvalidArgumentError(
        absl::StrCat("ABI version mismatch: got ", abi_version, ", but expected ", kAbiVersion));
  }
  return dynamic_module;
}

absl::StatusOr<DynamicModulePtr> newDynamicModuleByName(const absl::string_view module_name,
                                                        const bool do_not_close) {
  const char* module_search_path = getenv(DYNAMIC_MODULES_SEARCH_PATH);
  if (module_search_path == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat("Failed to load dynamic module: ", module_name,
                                                   " : ", DYNAMIC_MODULES_SEARCH_PATH,
                                                   " is not set"));
  }
  const std::filesystem::path file_path_absolute =
      std::filesystem::absolute(fmt::format("{}/lib{}.so", module_search_path, module_name));
  return newDynamicModule(file_path_absolute, do_not_close);
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
