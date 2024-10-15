#pragma once

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

/**
 * A class for loading and managing dynamic modules. This corresponds to a single dlopen handle.
 * When the DynamicModule object is destroyed, the dlopen handle is closed.
 *
 * This class is supposed to be initialized once in the main thread and can be shared with other
 * threads.
 */
class DynamicModule {
public:
  DynamicModule(void* handle) : handle_(handle) {}
  ~DynamicModule();

  /**
   * Get a function pointer from the dynamic module with a specific type.
   * @param T the function pointer type to cast the symbol to.
   * @param symbol_ref the symbol to look up.
   * @return the symbol if found, otherwise nullptr.
   */
  template <typename T> T getFunctionPointer(const absl::string_view symbol_ref) const {
    static_assert(std::is_pointer<T>::value &&
                      std::is_function<typename std::remove_pointer<T>::type>::value,
                  "T must be a function pointer type");
    return reinterpret_cast<T>(getSymbol(symbol_ref));
  }

private:
  /**
   * Get a symbol from the dynamic module.
   * @param symbol_ref the symbol to look up.
   * @return the symbol if found, otherwise nullptr.
   */
  void* getSymbol(const absl::string_view symbol_ref) const;

  // The raw dlopen handle that can be used to look up symbols.
  void* handle_;
};

using DynamicModuleSharedPtr = std::shared_ptr<DynamicModule>;

/**
 * Creates a new DynamicModule.
 * @param object_file_path the path to the object file to load.
 * @param do_not_close if true, the dlopen will be called with RTLD_NODELETE, so the loaded object
 * will not be destroyed. This is useful when an object has some global state that should not be
 * terminated. For example, c-shared objects compiled by Go doesn't support dlclose
 * https://github.com/golang/go/issues/11100.
 */
absl::StatusOr<DynamicModuleSharedPtr> newDynamicModule(const absl::string_view object_file_path,
                                                        const bool do_not_close);

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
