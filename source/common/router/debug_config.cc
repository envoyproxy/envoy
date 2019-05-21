#include "common/router/debug_config.h"

#include "common/common/macros.h"

namespace Envoy {
namespace Router {

const std::string& DebugConfig::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.router.debug_config");
}

} // namespace Router
} // namespace Envoy
