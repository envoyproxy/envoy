#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include <dlfcn.h>
#include <unistd.h>

#include <cerrno>
#include <fstream>
#include <string>
#include <system_error>

#include "envoy/common/exception.h"

#include "source/common/common/hex.h"
#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/background_fetch_manager.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "openssl/evp.h"
#include "openssl/sha.h"

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

absl::StatusOr<DynamicModulePtr> newDynamicModuleByNameImpl(const absl::string_view module_name,
                                                            const bool do_not_close,
                                                            const bool load_globally) {
  // Probe for the module's init symbol with the module name as a prefix. If the symbol is found
  // in the process binary (via dlsym(RTLD_DEFAULT)), treat this as a statically linked module.
  const std::string static_init_symbol =
      absl::StrCat(module_name, "_envoy_dynamic_module_on_program_init");
  if (dlsym(RTLD_DEFAULT, static_init_symbol.c_str()) != nullptr) {
    return newStaticModule(module_name);
  }
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
    // Static modules have no dlopen handle to close.
    return;
  }
  dlclose(handle_);
}

void* DynamicModule::getSymbol(const absl::string_view symbol_ref) const {
  if (!static_module_name_.empty()) {
    // For statically linked modules, look up the prefixed symbol in the process binary.
    const std::string prefixed = absl::StrCat(static_module_name_, "_", symbol_ref);
    return dlsym(RTLD_DEFAULT, prefixed.c_str());
  }
  // TODO(mathetake): maybe we should accept null-terminated const char* instead of string_view to
  // avoid unnecessary copy because it is likely that this is only called for a constant string,
  // though this is not a performance critical path.
  return dlsym(handle_, std::string(symbol_ref).c_str());
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
  std::string actual_hex = Hex::encode(digest.data(), digest.size());
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
  std::filesystem::path temp_file_path = moduleTempPath(sha256);

  // Write the (already SHA256-verified) bytes to a staging file, then atomically rename.
  std::string staging_template = temp_file_path.string() + ".XXXXXX";
  int fd = mkstemp(staging_template.data());
  if (fd == -1) {
    return absl::InternalError(absl::StrCat(
        "Failed to create temporary staging file for dynamic module: ", staging_template, ": ",
        errorDetails(errno)));
  }

  size_t total_written = 0;
  while (total_written < module_bytes.size()) {
    ssize_t written =
        write(fd, module_bytes.data() + total_written, module_bytes.size() - total_written);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      close(fd);
      std::error_code cleanup_ec;
      std::filesystem::remove(staging_template, cleanup_ec);
      return absl::InternalError(
          absl::StrCat("Failed to write to staging file for dynamic module: ", staging_template));
    }
    total_written += written;
  }
  close(fd);

  std::filesystem::path staging_path(staging_template);
  std::error_code ec;
  std::filesystem::permissions(staging_path, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace, ec);
  if (ec) {
    std::error_code cleanup_ec;
    std::filesystem::remove(staging_path, cleanup_ec);
    return absl::InternalError(absl::StrCat(
        "Failed to set permissions for dynamic module staging file: ", staging_path.string(), ": ",
        ec.message()));
  }
  std::filesystem::rename(staging_path, temp_file_path, ec);
  if (ec) {
    std::error_code cleanup_ec;
    std::filesystem::remove(staging_path, cleanup_ec);
    return absl::InternalError(absl::StrCat(
        "Failed to move dynamic module staging file to cache path: ", temp_file_path.string(), ": ",
        ec.message()));
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
  // If the module was already loaded at this path, newDynamicModule's RTLD_NOLOAD check
  // returns the existing handle without re-init.
  auto result = newDynamicModule(temp_file_path, do_not_close, load_globally);
  if (!result.ok()) {
    // Clean up the invalid file.
    std::filesystem::remove(temp_file_path);
  }
  return result;
}

absl::StatusOr<DynamicModulePtr> newStaticModule(const absl::string_view module_name) {
  auto dynamic_module = std::make_unique<DynamicModule>(module_name);

  const auto init_function =
      dynamic_module->getFunctionPointer<decltype(&envoy_dynamic_module_on_program_init)>(
          "envoy_dynamic_module_on_program_init");
  if (!init_function.ok()) {
    return init_function.status();
  }

  const char* abi_version = (*init_function.value())();
  if (abi_version == nullptr) {
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
    // Re-verify SHA256 of the cached file before dlopen. The cache path is in /tmp, which
    // may be writable by other processes (co-tenant containers, shared hosts); without this
    // check, an attacker who pre-populates ``/tmp/envoy_dynamic_module_<expected_sha>.so``
    // with a malicious shared object turns the cache-hit fast path into arbitrary code
    // execution. The fetch path that wrote the cached file already verified the hash, but
    // we cannot trust that the file was not replaced between writes.
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
