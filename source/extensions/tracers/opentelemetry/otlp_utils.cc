#include "source/extensions/tracers/opentelemetry/otlp_utils.h"

#include <string>

#include "source/common/common/fmt.h"
#include "source/common/common/macros.h"
#include "source/common/version/version.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

const std::string& OtlpUtils::getOtlpUserAgentHeader() {
  CONSTRUCT_ON_FIRST_USE(std::string,
                         fmt::format("OTel-OTLP-Exporter-Envoy/{}", Envoy::VersionInfo::version()));
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
