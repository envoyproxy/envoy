#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

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

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
