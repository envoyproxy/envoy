#include "source/extensions/common/opentelemetry/exporters/otlp/environment.h"

#include "source/common/common/macros.h"
#include "source/common/version/version.h"

namespace Envoy {
namespace Extensions {
namespace OpenTelemetry {
namespace Exporters {
namespace Otlp {

const std::string& GetUserAgent() {
  CONSTRUCT_ON_FIRST_USE(std::string, "OTel-OTLP-Exporter-Envoy/" + VersionInfo::version());
}

} // namespace Otlp
} // namespace Exporters
} // namespace OpenTelemetry
} // namespace Extensions
} // namespace Envoy
