#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

/**
 * A class for loading and managing dynamic modules. This corresponds to a single dlopen handle
 * (for dynamically loaded modules) or a statically-linked symbol namespace (for static modules).
 * When the DynamicModule object is destroyed, the dlopen handle is closed (if applicable).
 *
 * This class is supposed to be initialized once in the main thread and can be shared with other
 * threads.
 */
class DynamicModule {
public:
  // Constructor for a dynamically loaded module (via dlopen).
  explicit DynamicModule(void* handle) : handle_(handle) {}
  // Constructor for a statically linked module. Symbols are resolved via dlsym(RTLD_DEFAULT)
  // with the module name used as a prefix: "<static_module_name>_<symbol>".
  explicit DynamicModule(absl::string_view static_module_name)
      : handle_(nullptr), static_module_name_(static_module_name) {}
  ~DynamicModule();

  /**
   * Get a function pointer from the dynamic module with a specific type.
   * @param T the function pointer type to cast the symbol to.
   * @param symbol_ref the symbol to look up.
   * @return the symbol if found, otherwise nullptr.
   */
  template <typename T>
  absl::StatusOr<T> getFunctionPointer(const absl::string_view symbol_ref) const {
    static_assert(std::is_pointer<T>::value &&
                      std::is_function<typename std::remove_pointer<T>::type>::value,
                  "T must be a function pointer type");
    auto symbol = getSymbol(symbol_ref);
    if (symbol == nullptr) {
      return absl::InvalidArgumentError("Failed to resolve symbol " + std::string(symbol_ref));
    }
    return reinterpret_cast<T>(symbol);
  }

private:
  /**
   * Get a symbol from the dynamic module.
   * @param symbol_ref the symbol to look up.
   * @return the symbol if found, otherwise nullptr.
   */
  void* getSymbol(const absl::string_view symbol_ref) const;

  // The raw dlopen handle that can be used to look up symbols. nullptr for static modules.
  void* handle_;
  // Non-empty for statically linked modules. When set, getSymbol() looks up
  // "<static_module_name>_<symbol>" via dlsym(RTLD_DEFAULT) instead of using handle_.
  const std::string static_module_name_;
};

using DynamicModulePtr = std::unique_ptr<DynamicModule>;

/**
 * Creates a new DynamicModule. This is mainly exposed for testing purposes. Use
 * newDynamicModuleByName in wiring up dynamic modules.
 * @param object_file_absolute_path the absolute path to the object file to load.
 * @param do_not_close if true, the dlopen will be called with RTLD_NODELETE, so the loaded object
 * will not be destroyed. This is useful when an object has some global state that should not be
 * terminated. For example, c-shared objects compiled by Go doesn't support dlclose
 * https://github.com/golang/go/issues/11100.
 * @param load_globally if true, the dlopen will be called with RTLD_GLOBAL, so the loaded object
 * can share symbols with other dynamically loaded modules. This is useful for modules that need to
 * share symbols with other modules.
 */
absl::StatusOr<DynamicModulePtr>
newDynamicModule(const std::filesystem::path& object_file_absolute_path, const bool do_not_close,
                 const bool load_globally = false);

/**
 * Creates a new DynamicModule by name under the search path specified by the environment variable
 * `DYNAMIC_MODULES_SEARCH_PATH`. The file name is assumed to be `lib<module_name>.so`.
 * This is mostly a wrapper around newDynamicModule.
 * @param module_name the name of the module to load. If the symbol
 * ``<module_name>_envoy_dynamic_module_on_program_init`` is found in the process binary, the
 * module is treated as statically linked and newStaticModule is called instead of loading a
 * shared object. In that case, do_not_close and load_globally are ignored.
 * @param do_not_close if true, the dlopen will be called with RTLD_NODELETE, so the loaded object
 * will not be destroyed. This is useful when an object has some global state that should not be
 * terminated.
 * @param load_globally if true, the dlopen will be called with RTLD_GLOBAL, so the loaded object
 * can share symbols with other dynamically loaded modules. This is useful for modules that need to
 * share symbols with other modules.
 */
absl::StatusOr<DynamicModulePtr> newDynamicModuleByName(const absl::string_view module_name,
                                                        const bool do_not_close,
                                                        const bool load_globally = false);

/**
 * Creates a new DynamicModule backed by symbols already present in the process binary (i.e.,
 * statically linked). No shared object file is loaded. Instead, symbols are resolved via
 * dlsym(RTLD_DEFAULT, ...) with the module name used as a prefix:
 * "<module_name>_<symbol_name>".
 * @param module_name the module name used to build the symbol prefix.
 */
absl::StatusOr<DynamicModulePtr> newStaticModule(const absl::string_view module_name);

/**
 * Creates a new DynamicModule from the given module bytes. The module is expected to be in ELF
 * format and the bytes should be exactly the same as the content of the shared object file. The
 * bytes are written to a temporary file and loaded via dlopen.
 * @param module_bytes the content of the shared object file.
 * @param sha256 the sha256 hash of the module bytes, used for temp file naming. The caller is
 * responsible for verifying the hash before calling this function.
 * @param do_not_close if true, the dlopen will be called with RTLD_NODELETE, so the loaded object
 * will not be destroyed. This is useful when an object has some global state that should not be
 * terminated.
 * @param load_globally if true, the dlopen will be called with RTLD_GLOBAL, so the loaded object
 * can share symbols with other dynamically loaded modules. This is useful for modules that need to
 * share symbols with other modules.
 */
absl::StatusOr<DynamicModulePtr> newDynamicModuleFromBytes(const absl::string_view module_bytes,
                                                           const absl::string_view sha256,
                                                           const bool do_not_close,
                                                           const bool load_globally = false);

/**
 * Writes module bytes to disk at the canonical cache path for the given SHA256.
 * Uses a staging file with atomic rename for crash safety.
 * The caller is responsible for verifying the hash before calling this function.
 * @param module_bytes the content of the shared object file.
 * @param sha256 the hex-encoded SHA256 hash of the module bytes.
 */
absl::Status writeDynamicModuleBytesToDisk(absl::string_view module_bytes,
                                           absl::string_view sha256);

/**
 * Returns the canonical temporary file path for a remote module identified by its SHA256 hash.
 * This is the path where newDynamicModuleFromBytes writes the module and where the cache looks
 * it up.
 * @param sha256 the hex-encoded SHA256 hash of the module bytes.
 */
std::filesystem::path moduleTempPath(absl::string_view sha256);

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
