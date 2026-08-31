#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include <filesystem>

#include "envoy/common/optref.h"
#include "envoy/extensions/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/init/manager.h"
#include "envoy/server/factory_context.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

DynamicModule::~DynamicModule() {}

void* DynamicModule::getSymbol(const absl::string_view symbol_ref) const { return nullptr; }

absl::StatusOr<DynamicModulePtr>
newDynamicModule(const std::filesystem::path& object_file_absolute_path, const bool do_not_close,
                 const bool load_globally) {
  return absl::UnimplementedError("Dynamic modules on Windows are not supported yet.");
}

absl::StatusOr<DynamicModulePtr> newDynamicModuleByNameImpl(const absl::string_view module_name,
                                                            const bool do_not_close,
                                                            const bool load_globally) {
  return absl::UnimplementedError("Dynamic modules on Windows are not supported yet.");
}

absl::StatusOr<DynamicModulePtr> newDynamicModuleByName(
    const absl::string_view module_name, const bool do_not_close, const bool load_globally,
    OptRef<Server::Configuration::CommonFactoryContext> context, absl::string_view stat_name) {
  return absl::UnimplementedError("Dynamic modules on Windows are not supported yet.");
}

absl::StatusOr<DynamicModuleLoadResult>
newDynamicModuleByConfig(const ProtoDynamicModuleConfig& config, absl::string_view stat_name,
                         OptRef<Server::Configuration::CommonFactoryContext> context,
                         OptRef<Init::Manager> init_manager,
                         std::function<void(DynamicModulePtr)> on_loaded) {
  return absl::UnimplementedError("Dynamic modules on Windows are not supported yet.");
}

absl::Status writeDynamicModuleBytesToDisk(absl::string_view module_bytes,
                                           absl::string_view sha256) {
  return absl::UnimplementedError("Dynamic modules on Windows are not supported yet.");
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
