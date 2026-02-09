#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include <dlfcn.h>

#include <fstream>
#include <string>

#include "envoy/common/exception.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/extensions/dynamic_modules/abi/abi.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

constexpr char DYNAMIC_MODULES_SEARCH_PATH[] = "ENVOY_DYNAMIC_MODULES_SEARCH_PATH";

absl::StatusOr<DynamicModulePtr>
newDynamicModule(const std::filesystem::path& object_file_absolute_path, const bool do_not_close,
                 const bool load_globally) {
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
  // RTLD_LAZY is required for not only performance but also simply to load the module, otherwise
  // dlopen results in Invalid argument.
  int mode = RTLD_LAZY;
  if (load_globally) {
    mode |= RTLD_GLOBAL;
  } else {
    // RTLD_LOCAL is used by default to avoid collisions between multiple modules.
    mode |= RTLD_LOCAL;
  }
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
  // We log a warning if the ABI version does not match exactly.
  if (absl::string_view(abi_version) != absl::string_view(ENVOY_DYNAMIC_MODULES_ABI_VERSION)) {
    ENVOY_LOG_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), warn,
        "Dynamic module ABI version {} is deprecated. Please recompile the module against the "
        "SDK with the exact Envoy version used by the main program.",
        abi_version);
  } else {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), info,
                        "Dynamic module ABI version {} matched.", abi_version);
  }
  return dynamic_module;
}

absl::StatusOr<DynamicModulePtr> newDynamicModuleByName(const absl::string_view module_name,
                                                        const bool do_not_close,
                                                        const bool load_globally) {
  // First, try ENVOY_DYNAMIC_MODULES_SEARCH_PATH which falls back to the current directory.
  const char* module_search_path = getenv(DYNAMIC_MODULES_SEARCH_PATH);
  if (!module_search_path) {
    module_search_path = ".";
  }
  const std::filesystem::path file_path =
      std::filesystem::path(module_search_path) / fmt::format("lib{}.so", module_name);
  const std::filesystem::path file_path_absolute = std::filesystem::absolute(file_path);
  if (std::filesystem::exists(file_path_absolute)) {
    absl::StatusOr<DynamicModulePtr> dynamic_module =
        newDynamicModule(file_path_absolute, do_not_close, load_globally);
    // If the file exists but failed to load, return the error without trying other paths.
    // This allows the user to get the detailed error message such as missing dependencies, ABI
    // mismatch, etc.
    return dynamic_module;
  }

  // If not found, pass only the library name to dlopen to search in the standard library paths.
  // The man page of dlopen(3) says:
  //
  // > If path contains a slash ("/"), then it is interpreted as a
  // > (relative or absolute) pathname. Otherwise, the dynamic linker
  // > searches for the object ...
  //
  // which basically says dlopen searches for LD_LIBRARY_PATH and /usr/lib, etc.
  absl::StatusOr<DynamicModulePtr> dynamic_module =
      newDynamicModule(fmt::format("lib{}.so", module_name), do_not_close, load_globally);
  if (dynamic_module.ok()) {
    return dynamic_module;
  }

  return absl::InvalidArgumentError(
      absl::StrCat("Failed to load dynamic module: lib", module_name,
                   ".so not found in any search path: ", file_path_absolute.c_str(),
                   " or standard library paths such as LD_LIBRARY_PATH, /usr/lib, etc. or failed "
                   "to initialize: ",
                   dynamic_module.status().message()));
}

DynamicModule::~DynamicModule() { dlclose(handle_); }

void* DynamicModule::getSymbol(const absl::string_view symbol_ref) const {
  // TODO(mathetake): maybe we should accept null-terminated const char* instead of string_view to
  // avoid unnecessary copy because it is likely that this is only called for a constant string,
  // though this is not a performance critical path.
  return dlsym(handle_, std::string(symbol_ref).c_str());
}

absl::StatusOr<DynamicModulePtr> newDynamicModuleFromBytes(absl::string_view module_bytes,
                                                           absl::string_view sha256_hash,
                                                           bool do_not_close, bool load_globally) {
  if (module_bytes.empty()) {
    return absl::InvalidArgumentError("Module bytes cannot be empty");
  }

  // Compute SHA256 hash of the module bytes.
  auto& crypto_util = Common::Crypto::UtilitySingleton::get();
  Buffer::OwnedImpl buffer{std::string{module_bytes}};
  const std::string computed_hash = Hex::encode(crypto_util.getSha256Digest(buffer));

  // Verify SHA256 hash if provided.
  if (!sha256_hash.empty() && computed_hash != sha256_hash) {
    return absl::InvalidArgumentError(
        absl::StrCat("SHA256 hash mismatch: expected ", sha256_hash, ", got ", computed_hash));
  }

  // Use computed_hash for the temp file name (if sha256_hash was provided and matched,
  // computed_hash == sha256_hash at this point).
  const std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
  const std::filesystem::path temp_file_path =
      temp_dir / fmt::format("envoy_dynmod_{}.so", computed_hash);

  if (!std::filesystem::exists(temp_file_path)) {
    // Write to a pid-suffixed temp file first, then atomically rename to avoid partial reads.
    const std::filesystem::path temp_file_writing =
        temp_dir / fmt::format("envoy_dynmod_{}.so.tmp.{}", computed_hash, getpid());

    // Write the module bytes to the temp file with secure permissions.
    std::ofstream ofs(temp_file_writing, std::ios::binary | std::ios::trunc);
    if (!ofs) {
      return absl::InternalError(
          absl::StrCat("Failed to create temp file: ", temp_file_writing.string()));
    }
    ofs.write(module_bytes.data(), module_bytes.size());
    if (!ofs) {
      std::filesystem::remove(temp_file_writing);
      return absl::InternalError(
          absl::StrCat("Failed to write module bytes to temp file: ", temp_file_writing.string()));
    }
    ofs.close();

    // Set file permissions to 0600 (owner read/write only).
    std::error_code ec;
    std::filesystem::permissions(
        temp_file_writing, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        ec);
    if (ec) {
      std::filesystem::remove(temp_file_writing);
      return absl::InternalError(
          absl::StrCat("Failed to set permissions on temp file: ", ec.message()));
    }

    // Atomically rename the temp file to the final path.
    std::filesystem::rename(temp_file_writing, temp_file_path, ec);
    if (ec) {
      // If rename fails (e.g., another process created the file), that's OK - use the existing
      // file.
      std::filesystem::remove(temp_file_writing);
      if (!std::filesystem::exists(temp_file_path)) {
        return absl::InternalError(absl::StrCat("Failed to rename temp file: ", ec.message()));
      }
    }
  }

  // Load the module from the temp file.
  return newDynamicModule(temp_file_path, do_not_close, load_globally);
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
