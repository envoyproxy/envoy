#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "envoy/common/optref.h"
#include "envoy/extensions/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/init/manager.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"

#include "source/extensions/common/wasm/remote_async_datasource.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

using ProtoDynamicModuleConfig = envoy::extensions::dynamic_modules::v3::DynamicModuleConfig;

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
 * @param context optional server-wide factory context. When provided and the module fails to load,
 * the shared ``dynamic_modules.module_load_error`` counter (tagged with ``config_name``) is
 * incremented, so every dynamic-module extension that loads by name reports load failures
 * consistently without repeating the bookkeeping at each call site. Pass the server context
 * (``ServerFactoryContext``), NOT a listener context — see incrementLoadFailure().
 * @param stat_name the configured name of the extension instance using the module (e.g.
 * ``filter_name``, ``transport_socket_name``); used as the ``config_name`` tag. Falls back to
 * ``default`` if empty. Only used when ``context`` is provided.
 */
absl::StatusOr<DynamicModulePtr>
newDynamicModuleByName(const absl::string_view module_name, const bool do_not_close,
                       const bool load_globally = false,
                       OptRef<Server::Configuration::CommonFactoryContext> context = {},
                       absl::string_view stat_name = {});

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

/**
 * Verifies that the file at ``path`` has SHA256 digest equal to ``expected_sha256_hex``.
 *
 * This is used to defend the cache-hit fast path in remote-module loading: ``moduleTempPath``
 * is a deterministic location under ``/tmp`` derived from the expected hash. Without this
 * check, an attacker who can write to ``/tmp`` (a co-tenant container or shared host) can
 * pre-populate the path with a malicious shared object that Envoy will then dlopen, yielding
 * attacker-controlled code execution.
 *
 * @param path absolute path to the file to verify.
 * @param expected_sha256_hex the hex-encoded expected SHA256 digest (64 chars). The
 *        comparison is case-insensitive.
 * @return OkStatus when the on-disk SHA256 matches; FailedPreconditionError on mismatch;
 *         InternalError on any I/O failure.
 */
absl::Status verifyFileSha256(const std::filesystem::path& path,
                              absl::string_view expected_sha256_hex);

/**
 * Holds the state required to keep an in-flight asynchronous remote module fetch alive. This is
 * returned (inside DynamicModuleLoadResult) when newDynamicModuleByConfig() needs to fetch the
 * module from a remote source. The caller must keep this object alive until the fetch completes.
 */
struct AsyncLoadingState {
  // Keeps the remote data provider alive for the duration of the fetch (including retries).
  RemoteAsyncDataProviderPtr remote_provider;
  // Invoked on the main thread with the loaded module once the remote fetch succeeds. This is the
  // on_loaded callback that was passed to newDynamicModuleByConfig().
  std::function<void(DynamicModulePtr)> on_loaded;
};
using AsyncLoadingStateSharedPtr = std::shared_ptr<AsyncLoadingState>;

/**
 * The result of newDynamicModuleByConfig(). Exactly one of the two members is populated:
 *  - loaded is set when the module was loaded synchronously (local file, by name, or remote
 *    cache hit). The caller takes ownership of the module and can use it immediately.
 *  - async is set when the module is being fetched asynchronously from a remote source. The
 *    on_loaded callback supplied to newDynamicModuleByConfig() is invoked once the fetch
 *    completes; the caller must keep async alive until then.
 */
struct DynamicModuleLoadResult {
  DynamicModulePtr loaded;
  AsyncLoadingStateSharedPtr async;
};

/**
 * Loads a dynamic module described by the given configuration. This consolidates all the module
 * sourcing strategies (local file, statically linked by name, and remote HTTP source with on-disk
 * caching and optional asynchronous fetch) behind a single entry point.
 *
 * The local-file and by-name paths are fully synchronous and require neither a factory context nor
 * an init manager, so extension points that have no access to a factory context (such as the
 * upstream HTTP conn-pool factory) can call this with only the config. The remote HTTP source path
 * requires a context, and the asynchronous fetch additionally requires an init manager and an
 * on_loaded callback; if any of those is missing, a remote source is rejected with an error.
 *
 * @param config the dynamic module configuration describing where to source the module from.
 * @param stat_name the configured name of the extension instance using the module (e.g.
 * ``filter_name``, ``transport_socket_name``, ``lb_policy_name``). Falls back to ``default`` if
 * empty.
 * @param context the factory context, used to access the singleton, cluster, dispatcher and random
 * generator needed for remote fetches and background caching. Only the members shared by all
 * extension points (CommonFactoryContext) are required. May be absent; in that case a remote source
 * is rejected with an error while local-file and by-name sources still load.
 * @param init_manager the init manager used to register the asynchronous remote fetch target. May
 * be absent; in that case a remote source that is not already cached is rejected with an error.
 * @param on_loaded invoked on the main thread with the loaded module once an asynchronous remote
 * fetch completes successfully. Only used for the asynchronous remote path; ignored for the
 * synchronous paths (where the module is returned directly via DynamicModuleLoadResult::loaded).
 * May be empty if the caller does not support asynchronous loading.
 * @return the load result on success, or an error status if the configuration is invalid or the
 * module failed to load.
 */
absl::StatusOr<DynamicModuleLoadResult>
newDynamicModuleByConfig(const ProtoDynamicModuleConfig& config, absl::string_view stat_name,
                         OptRef<Server::Configuration::CommonFactoryContext> context = {},
                         OptRef<Init::Manager> init_manager = {},
                         std::function<void(DynamicModulePtr)> on_loaded = nullptr);

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
