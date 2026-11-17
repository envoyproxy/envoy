#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include "envoy/common/exception.h"
#include "envoy/common/optref.h"
#include "envoy/common/platform.h"
#include "envoy/extensions/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/init/manager.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/hex.h"
#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/background_fetch_manager.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "openssl/evp.h"
#include "openssl/sha.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

constexpr char DYNAMIC_MODULES_SEARCH_PATH[] = "ENVOY_DYNAMIC_MODULES_SEARCH_PATH";

namespace {

// Logged from the loader itself so that every extension type reports load failures, including the
// ones that have no scope to increment ``dynamic_modules.module_load_error`` on. It is also the
// only signal for a module named in the bootstrap config, since the server then exits before its
// statistics can be scraped.
void logModuleLoadFailure(absl::string_view module, absl::string_view reason) {
  ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), error,
                      "Unable to load dynamic module {}: {}", module, reason);
}

// Looks up an exported symbol in the main executable. This is the Windows counterpart of
// dlsym(RTLD_DEFAULT, ...) used for statically linked modules. The symbol must be exported from
// the executable (e.g. via the module definition file passed to the linker).
void* findSymbolInExecutable(const std::string& symbol) {
  HMODULE executable = ::GetModuleHandleW(nullptr);
  if (executable == nullptr) {
    return nullptr;
  }
  return reinterpret_cast<void*>(::GetProcAddress(executable, symbol.c_str()));
}

// Returns true if the file at the given path exists and its content is exactly the given bytes.
bool fileContentEquals(const std::filesystem::path& path, absl::string_view expected) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  const std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
  return !file.bad() && content == expected;
}

} // namespace

absl::StatusOr<DynamicModulePtr>
newDynamicModule(const std::filesystem::path& object_file_absolute_path, const bool do_not_close,
                 const bool load_globally) {
  // Windows has no equivalent of RTLD_GLOBAL: every DLL resolves its imports against the modules
  // named in its import table, so load_globally has no effect. Likewise, the loader always binds
  // imports at load time, so the RTLD_NOW / RTLD_LAZY runtime guard does not apply.
  UNREFERENCED_PARAMETER(load_globally);

  const std::wstring wide_path = object_file_absolute_path.wstring();
  const std::string path_string = object_file_absolute_path.string();

  // GetModuleHandleExW without GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT returns the handle of
  // an already resident module and increments its reference count, which mirrors
  // dlopen(RTLD_NOLOAD). This avoids the duplicate call to the init function.
  HMODULE handle = nullptr;
  if (::GetModuleHandleExW(0, wide_path.c_str(), &handle) && handle != nullptr) {
    return std::make_unique<DynamicModule>(handle);
  }

  // LOAD_WITH_ALTERED_SEARCH_PATH makes the loader resolve the module's own dependencies from the
  // module's directory. It is only defined for absolute paths; a bare module name is searched in
  // the standard DLL search order instead.
  const DWORD load_flags =
      object_file_absolute_path.is_absolute() ? LOAD_WITH_ALTERED_SEARCH_PATH : 0;
  // Suppress the system error dialog boxes that the loader may otherwise show for missing or
  // corrupt images, so that a failure is reported only through the returned status.
  DWORD previous_error_mode = 0;
  const bool error_mode_set =
      ::SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &previous_error_mode);
  handle = ::LoadLibraryExW(wide_path.c_str(), nullptr, load_flags);
  const DWORD load_error = ::GetLastError();
  if (error_mode_set) {
    ::SetThreadErrorMode(previous_error_mode, nullptr);
  }
  if (handle == nullptr) {
    const std::string reason = errorDetails(static_cast<int>(load_error));
    logModuleLoadFailure(path_string, reason);
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to load dynamic module: ", path_string, " : ", reason));
  }
  if (do_not_close) {
    // Pin the module so that it stays loaded until the process exits, regardless of FreeLibrary
    // calls. This is the equivalent of RTLD_NODELETE.
    HMODULE pinned = nullptr;
    if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                  GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(handle), &pinned)) {
      const std::string reason = errorDetails(static_cast<int>(::GetLastError()));
      ::FreeLibrary(handle);
      logModuleLoadFailure(path_string, reason);
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to pin dynamic module: ", path_string, " : ", reason));
    }
  }

  DynamicModulePtr dynamic_module = std::make_unique<DynamicModule>(handle);

  const auto init_function =
      dynamic_module->getFunctionPointer<decltype(&envoy_dynamic_module_on_program_init)>(
          "envoy_dynamic_module_on_program_init");

  if (!init_function.ok()) {
    logModuleLoadFailure(path_string, init_function.status().message());
    return init_function.status();
  }

  const char* abi_version = (*init_function.value())();
  if (abi_version == nullptr) {
    logModuleLoadFailure(path_string, "envoy_dynamic_module_on_program_init returned null");
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to initialize dynamic module: ", path_string));
  }
  // Copy the version out of module memory right away, since the returned pointer is only
  // guaranteed valid immediately after on_program_init returns.
  const std::string module_abi_version(abi_version);
  // We log a warning if the ABI version does not match exactly.
  if (module_abi_version != ENVOY_DYNAMIC_MODULES_ABI_VERSION) {
    ENVOY_LOG_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), warn,
        "Dynamic module ABI version {} is deprecated. Please recompile the module against the "
        "SDK with the exact Envoy version used by the main program.",
        module_abi_version);
  } else {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), info,
                        "Dynamic module ABI version {} matched.", module_abi_version);
  }
  return dynamic_module;
}

absl::StatusOr<DynamicModulePtr> newDynamicModuleByNameImpl(const absl::string_view module_name,
                                                            const bool do_not_close,
                                                            const bool load_globally) {
  // Probe for the module's init symbol with the module name as a prefix. If the symbol is exported
  // from the executable, treat this as a statically linked module.
  const std::string static_init_symbol =
      absl::StrCat(module_name, "_envoy_dynamic_module_on_program_init");
  if (findSymbolInExecutable(static_init_symbol) != nullptr) {
    return newStaticModule(module_name);
  }
  // First, try ENVOY_DYNAMIC_MODULES_SEARCH_PATH which falls back to the current directory.
  const char* module_search_path = std::getenv(DYNAMIC_MODULES_SEARCH_PATH);
  if (!module_search_path) {
    module_search_path = ".";
  }
  // Modules keep the same file name as on other platforms so that the same build outputs and
  // configuration work everywhere. LoadLibraryExW accepts any extension when one is present.
  const std::string file_name = fmt::format("lib{}.so", module_name);
  const std::filesystem::path file_path = std::filesystem::path(module_search_path) / file_name;
  const std::filesystem::path file_path_absolute = std::filesystem::absolute(file_path);
  if (std::filesystem::exists(file_path_absolute)) {
    absl::StatusOr<DynamicModulePtr> dynamic_module =
        newDynamicModule(file_path_absolute, do_not_close, load_globally);
    // If the file exists but failed to load, return the error without trying other paths.
    // This allows the user to get the detailed error message such as missing dependencies, ABI
    // mismatch, etc.
    return dynamic_module;
  }

  // If not found, pass only the library name to LoadLibraryExW so that the standard DLL search
  // order is used (the application directory, the system directories, the current directory and
  // the directories listed in PATH).
  absl::StatusOr<DynamicModulePtr> dynamic_module =
      newDynamicModule(file_name, do_not_close, load_globally);
  if (dynamic_module.ok()) {
    return dynamic_module;
  }

  return absl::InvalidArgumentError(absl::StrCat(
      "Failed to load dynamic module: ", file_name,
      " not found in any search path: ", file_path_absolute.string(),
      " or standard DLL search paths such as the application directory, system directories, PATH, "
      "etc. or failed to initialize: ",
      dynamic_module.status().message()));
}

absl::StatusOr<DynamicModulePtr> newDynamicModuleByName(
    const absl::string_view module_name, const bool do_not_close, const bool load_globally,
    OptRef<Server::Configuration::CommonFactoryContext> context, absl::string_view stat_name) {
  absl::StatusOr<DynamicModulePtr> result =
      newDynamicModuleByNameImpl(module_name, do_not_close, load_globally);
  if (!result.ok()) {
    incrementLoadFailure(context, stat_name, ModuleLoadErrorStat);
  }
  return result;
}

DynamicModule::~DynamicModule() {
  if (!static_module_name_.empty()) {
    // Static modules have no module handle to free.
    return;
  }
  if (handle_ != nullptr) {
    ::FreeLibrary(static_cast<HMODULE>(handle_));
  }
}

void* DynamicModule::getSymbol(const absl::string_view symbol_ref) const {
  if (!static_module_name_.empty()) {
    // For statically linked modules, look up the prefixed symbol in the executable.
    return findSymbolInExecutable(absl::StrCat(static_module_name_, "_", symbol_ref));
  }
  return reinterpret_cast<void*>(
      ::GetProcAddress(static_cast<HMODULE>(handle_), std::string(symbol_ref).c_str()));
}

std::filesystem::path moduleTempPath(const absl::string_view sha256) {
  return std::filesystem::temp_directory_path() / fmt::format("envoy_dynamic_module_{}.so", sha256);
}

absl::Status verifyFileSha256(const std::filesystem::path& path,
                              absl::string_view expected_sha256_hex) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return absl::InternalError(absl::StrCat(
        "Failed to open file for SHA256 verification: ", path.string(), ": ", errorDetails(errno)));
  }
  bssl::ScopedEVP_MD_CTX ctx;
  if (EVP_DigestInit(ctx.get(), EVP_sha256()) != 1) {
    return absl::InternalError("Failed to initialize SHA256 digest context");
  }
  // 64 KiB chunks: large enough to keep syscall overhead low, small enough to keep stack clean.
  std::array<char, 65536> buf;
  while (file) {
    file.read(buf.data(), buf.size());
    const std::streamsize got = file.gcount();
    if (got > 0 && EVP_DigestUpdate(ctx.get(), buf.data(), static_cast<size_t>(got)) != 1) {
      return absl::InternalError("Failed to update SHA256 digest");
    }
    if (file.bad()) {
      return absl::InternalError(
          absl::StrCat("I/O error reading file for SHA256 verification: ", path.string()));
    }
  }
  std::array<uint8_t, SHA256_DIGEST_LENGTH> digest;
  if (EVP_DigestFinal(ctx.get(), digest.data(), nullptr) != 1) {
    return absl::InternalError("Failed to finalize SHA256 digest");
  }
  std::string actual_hex = Hex::encode(absl::Span<const uint8_t>(digest.data(), digest.size()));
  // The expected hash is operator-supplied (proto config, not user input) and the actual digest
  // is computed from a file the attacker may control; the only information leaked by an
  // early-exit comparison is "wrong remote module", which carries no secret. A constant-time
  // compare is therefore not warranted here.
  std::string expected_normalised{expected_sha256_hex};
  absl::AsciiStrToLower(&expected_normalised);
  if (actual_hex != expected_normalised) {
    return absl::FailedPreconditionError(
        absl::StrCat("SHA256 mismatch for cached dynamic module at ", path.string(), ": expected ",
                     expected_normalised, " got ", actual_hex));
  }
  return absl::OkStatus();
}

absl::Status writeDynamicModuleBytesToDisk(const absl::string_view module_bytes,
                                           const absl::string_view sha256) {
  const std::filesystem::path temp_file_path = moduleTempPath(sha256);

  // Write the (already SHA256-verified) bytes to a uniquely named staging file in the same
  // directory, then atomically rename. GetTempFileNameW creates the staging file exclusively,
  // which is the equivalent of mkstemp. Files under the user's temp directory are private to the
  // user by the default ACL, so no explicit permission change is needed.
  const std::wstring staging_dir = temp_file_path.parent_path().wstring();
  std::array<wchar_t, MAX_PATH> staging_buffer{};
  if (::GetTempFileNameW(staging_dir.c_str(), L"edm", 0, staging_buffer.data()) == 0) {
    return absl::InternalError(
        absl::StrCat("Failed to create temporary staging file for dynamic module in ",
                     temp_file_path.parent_path().string(), ": ",
                     errorDetails(static_cast<int>(::GetLastError()))));
  }
  const std::filesystem::path staging_path(staging_buffer.data());

  {
    std::ofstream out(staging_path, std::ios::binary | std::ios::trunc);
    if (out.is_open()) {
      out.write(module_bytes.data(), static_cast<std::streamsize>(module_bytes.size()));
    }
    if (!out.is_open() || !out.good()) {
      out.close();
      std::error_code cleanup_ec;
      std::filesystem::remove(staging_path, cleanup_ec);
      return absl::InternalError(absl::StrCat(
          "Failed to write to staging file for dynamic module: ", staging_path.string()));
    }
  }

  if (!::MoveFileExW(staging_path.c_str(), temp_file_path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    const DWORD move_error = ::GetLastError();
    std::error_code cleanup_ec;
    std::filesystem::remove(staging_path, cleanup_ec);
    // Unlike POSIX, Windows does not allow replacing a file that is mapped as a loaded module. If
    // the cache path already holds the same bytes, e.g. because the same module is already
    // loaded from it, there is nothing left to do.
    if (fileContentEquals(temp_file_path, module_bytes)) {
      return absl::OkStatus();
    }
    return absl::InternalError(absl::StrCat(
        "Failed to move dynamic module staging file to cache path: ", temp_file_path.string(), ": ",
        errorDetails(static_cast<int>(move_error))));
  }
  return absl::OkStatus();
}

absl::StatusOr<DynamicModulePtr> newDynamicModuleFromBytes(const absl::string_view module_bytes,
                                                           const absl::string_view sha256,
                                                           const bool do_not_close,
                                                           const bool load_globally) {
  auto status = writeDynamicModuleBytesToDisk(module_bytes, sha256);
  if (!status.ok()) {
    return status;
  }
  auto temp_file_path = moduleTempPath(sha256);
  // If the module was already loaded at this path, newDynamicModule's GetModuleHandleExW check
  // returns the existing handle without re-init.
  auto result = newDynamicModule(temp_file_path, do_not_close, load_globally);
  if (!result.ok()) {
    // Clean up the invalid file. Removal fails if the file is still mapped by the process, which
    // is not fatal.
    std::error_code ec;
    std::filesystem::remove(temp_file_path, ec);
  }
  return result;
}

absl::StatusOr<DynamicModulePtr> newStaticModule(const absl::string_view module_name) {
  auto dynamic_module = std::make_unique<DynamicModule>(module_name);

  const auto init_function =
      dynamic_module->getFunctionPointer<decltype(&envoy_dynamic_module_on_program_init)>(
          "envoy_dynamic_module_on_program_init");
  if (!init_function.ok()) {
    logModuleLoadFailure(module_name, init_function.status().message());
    return init_function.status();
  }

  const char* abi_version = (*init_function.value())();
  if (abi_version == nullptr) {
    logModuleLoadFailure(module_name, "envoy_dynamic_module_on_program_init returned null");
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to initialize static module: ", module_name));
  }
  if (absl::string_view(abi_version) != absl::string_view(ENVOY_DYNAMIC_MODULES_ABI_VERSION)) {
    ENVOY_LOG_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), warn,
        "Static module ABI version {} is deprecated. Please recompile the module against the "
        "SDK with the exact Envoy version used by the main program.",
        abi_version);
  } else {
    ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), info,
                        "Static module ABI version {} matched.", abi_version);
  }
  return dynamic_module;
}

absl::StatusOr<DynamicModuleLoadResult>
newDynamicModuleByConfig(const ProtoDynamicModuleConfig& config, absl::string_view stat_name,
                         OptRef<Server::Configuration::CommonFactoryContext> context,
                         OptRef<Init::Manager> init_manager,
                         std::function<void(DynamicModulePtr)> on_loaded) {

  if (!config.has_module()) {
    // Name-based dynamic module loading: look up the module by name under the search path.
    if (config.name().empty()) {
      incrementLoadFailure(context, stat_name, ModuleLoadErrorStat);
      return absl::InvalidArgumentError(
          "Either 'name' or 'module' must be specified in dynamic_module_config");
    }
    // newDynamicModuleByName emits the module_load_error counter itself when given the scope, so we
    // do not double-count here.
    auto dynamic_module = newDynamicModuleByName(config.name(), config.do_not_close(),
                                                 config.load_globally(), context, stat_name);
    if (!dynamic_module.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to load dynamic module: ", dynamic_module.status().message()));
    }
    return DynamicModuleLoadResult{std::move(dynamic_module.value()), nullptr};
  }

  // Data source-based dynamic module loading: load the module from the specified data source, which
  // may be a local file path or a remote HTTP URL.

  if (!config.module().local().filename().empty()) {
    // Module specified by local file path.
    auto dynamic_module = newDynamicModule(config.module().local().filename(),
                                           config.do_not_close(), config.load_globally());
    if (!dynamic_module.ok()) {
      incrementLoadFailure(context, stat_name, ModuleLoadErrorStat);

      return absl::InvalidArgumentError(
          absl::StrCat("Failed to load dynamic module: ", dynamic_module.status().message()));
    }
    return DynamicModuleLoadResult{std::move(dynamic_module.value()), nullptr};
  }

  if (!config.module().has_remote()) {
    incrementLoadFailure(context, stat_name, ModuleLoadErrorStat);
    return absl::InvalidArgumentError(
        "Only local file path or remote HTTP source is supported for module sources");
  }

  // Remote HTTP source: every remaining path (cache management, NACK background fetch, and the
  // asynchronous fetch itself) needs the factory context, so reject remote sources for callers that
  // cannot supply one.
  if (!context.has_value()) {
    return absl::InvalidArgumentError("Remote module sources require a factory context");
  }

  const absl::string_view sha256 = config.module().remote().sha256();

  // Check if a previously fetched module with the same SHA256 already exists on disk.
  // newDynamicModuleFromBytes writes to a deterministic path based on SHA256, so the
  // filesystem itself acts as the cache.
  auto cached_path = moduleTempPath(sha256);
  if (std::filesystem::exists(cached_path)) {
    // Re-verify SHA256 of the cached file before loading it. The cache path is in the temp
    // directory, which may be writable by other processes; without this check, an attacker who
    // pre-populates ``envoy_dynamic_module_<expected_sha>.so`` with a malicious DLL turns the
    // cache-hit fast path into arbitrary code execution. The fetch path that wrote the cached
    // file already verified the hash, but we cannot trust that the file was not replaced between
    // writes.
    const auto verify_status = verifyFileSha256(cached_path, sha256);
    if (!verify_status.ok()) {
      // Tampered or corrupted cache entry — remove it and fall through to the fetch path
      // below so the legitimate remote source can re-supply correct bytes.
      std::error_code ec;
      std::filesystem::remove(cached_path, ec);
      ENVOY_LOG_MISC(warn, "dynamic_modules: removed cached file failing SHA256 verification: {}",
                     verify_status.message());
      // Fall through to the fetch logic below.
    } else {
      auto dynamic_module =
          newDynamicModule(cached_path, config.do_not_close(), config.load_globally());
      if (dynamic_module.ok()) {
        BackgroundFetchManager::singleton(context->singletonManager())->erase(sha256);
        return DynamicModuleLoadResult{std::move(dynamic_module.value()), nullptr};
      }
      // File exists, hash matches, but failed to load — re-fetching the same SHA256 would
      // produce identical bytes, so there is no point in falling through.
      incrementLoadFailure(context, stat_name, ModuleLoadErrorStat);
      return absl::InvalidArgumentError(
          absl::StrCat("Cached remote module failed to load: ", dynamic_module.status().message()));
    }
  }

  // In NACK mode, reject the config and kick off a background fetch. The control
  // plane will retry, and the next attempt picks up the cached file above.
  if (config.nack_on_cache_miss()) {
    BackgroundFetchManager::singleton(context->singletonManager())
        ->fetchIfNeeded(sha256, context->clusterManager(), config.module().remote());
    incrementLoadFailure(context, stat_name, RemoteFetchErrorStat);

    return absl::InvalidArgumentError(
        absl::StrCat("Remote module not cached; background fetch in progress. SHA256: ", sha256));
  }

  // No cached file — need async fetch, which requires init_manager.
  if (!init_manager.has_value()) {
    incrementLoadFailure(context, stat_name, RemoteFetchErrorStat);
    return absl::InvalidArgumentError("Remote module sources require an init manager");
  }

  // No on_loaded callback means the caller does not support asynchronous loading, so reject the
  // config rather than silently failing to load the module.
  if (!on_loaded) {
    incrementLoadFailure(context, stat_name, RemoteFetchErrorStat);
    return absl::InvalidArgumentError("Remote module sources require an on_loaded callback");
  }

  // Shared state holding the RemoteAsyncDataProvider (to keep the fetch alive for its duration,
  // including retries) and the on_loaded callback invoked once the module is loaded.
  auto async_state = std::make_shared<AsyncLoadingState>();
  async_state->on_loaded = std::move(on_loaded);

  // Use a weak_ptr in the callback to break the reference cycle:
  // async_state -> remote_provider -> callback -> async_state.
  std::weak_ptr<AsyncLoadingState> weak_state = async_state;

  async_state->remote_provider = std::make_unique<RemoteAsyncDataProvider>(
      context->clusterManager(), *init_manager, config.module().remote(),
      context->mainThreadDispatcher(), context->api().randomGenerator(),
      /*allow_empty=*/true,
      [stat_name = std::string(stat_name), sha256 = std::string(sha256), weak_state, context,
       do_not_close = config.do_not_close(),
       load_globally = config.load_globally()](const std::string& data) {
        auto state = weak_state.lock();
        if (!state) {
          return;
        }
        if (data.empty()) {
          incrementLoadFailure(context, stat_name, RemoteFetchErrorStat);
          ENVOY_LOG_TO_LOGGER(
              Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), error,
              "Remote dynamic module fetch returned empty data; module will not be loaded");
          return;
        }
        auto module_or_error = newDynamicModuleFromBytes(data, sha256, do_not_close, load_globally);
        if (!module_or_error.ok()) {
          incrementLoadFailure(context, stat_name, RemoteFetchErrorStat);
          ENVOY_LOG_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules),
                              error, "Failed to load remote dynamic module from bytes: {}",
                              module_or_error.status().message());
          return;
        }
        if (state->on_loaded) {
          state->on_loaded(std::move(module_or_error.value()));
        }
      });

  return DynamicModuleLoadResult{nullptr, std::move(async_state)};
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
