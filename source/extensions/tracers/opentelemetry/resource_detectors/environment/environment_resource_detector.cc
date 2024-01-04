#include "environment_resource_detector.h"

#include <sstream>
#include <string>

#include "source/common/config/datasource.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

constexpr absl::string_view kOtelResourceAttributesEnv = "OTEL_RESOURCE_ATTRIBUTES";

/**
 * @brief Detects a resource from the OTEL_RESOURCE_ATTRIBUTES environment variable
 * Based on the OTel C++ SDK:
 * https://github.com/open-telemetry/opentelemetry-cpp/blob/v1.11.0/sdk/src/resource/resource_detector.cc
 *
 * @return Resource A resource with the attributes from the OTEL_RESOURCE_ATTRIBUTES environment
 * variable.
 */
Resource EnvironmentResourceDetector::detect() {
  envoy::config::core::v3::DataSource ds;
  ds.set_environment_variable(kOtelResourceAttributesEnv);

  Resource resource;
  resource.schema_url_ = "";
  std::string attributes_str = "";

  attributes_str = Config::DataSource::read(ds, true, context_.serverFactoryContext().api());

  if (attributes_str.empty()) {
    throw EnvoyException(
        fmt::format("The OpenTelemetry environment resource detector is configured but the '{}'"
                    " environment variable is empty.",
                    kOtelResourceAttributesEnv));
  }

  for (const auto& pair : StringUtil::splitToken(attributes_str, ",")) {
    const auto keyValue = StringUtil::splitToken(pair, "=");
    if (keyValue.size() != 2) {
      throw EnvoyException(
          fmt::format("The OpenTelemetry environment resource detector is configured but the '{}'"
                      " environment variable has an invalid format.",
                      kOtelResourceAttributesEnv));
    }

    const std::string key = std::string(keyValue[0]);
    const std::string value = std::string(keyValue[1]);
    resource.attributes_[key] = value;
  }
  return resource;
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
