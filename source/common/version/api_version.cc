#include "common/version/api_version.h"

namespace Envoy {

const ApiVersion& ApiVersionInfo::apiVersion() { return api_version; }

const ApiVersion& ApiVersionInfo::oldestApiVersion() { return oldest_api_version; }

} // namespace Envoy
