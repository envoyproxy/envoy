#include "library/common/common/default_system_helper.h"

namespace Envoy {

bool DefaultSystemHelper::isCleartextPermitted(absl::string_view /*hostname*/) { return true; }

} // namespace Envoy
