#include "source/extensions/tracers/opentelemetry/resource_detectors/environment/environment_resource_detector.h"

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
 * @brief Detects a resource from the OTEL_RESOURCE_ATTRIBUTES environment
 * variable Based on the OTel C++ SDK:
 * https://github.com/open-telemetry/opentelemetry-cpp/blob/v1.11.0/sdk/src/resource/resource_detector.cc
 *
 * @return Resource A resource with the attributes from the
 * OTEL_RESOURCE_ATTRIBUTES environment variable.
 */
ResourceConstSharedPtr EnvironmentResourceDetector::detect() const { return resource_; }

EnvironmentResourceDetector::EnvironmentResourceDetector(
    const envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
        EnvironmentResourceDetectorConfig& config,
    Server::Configuration::ServerFactoryContext& context)
    : config_(config), context_(context), resource_(std::make_shared<Resource>()) {
  envoy::config::core::v3::DataSource ds;
  ds.set_environment_variable(kOtelResourceAttributesEnv);
  auto resource = std::make_shared<Resource>();
  resource->schema_url_ = "";
  std::string attributes_str = "";

  TRY_NEEDS_AUDIT {
    attributes_str =
        THROW_OR_RETURN_VALUE(Config::DataSource::read(ds, true, context_.api()), std::string);
  }
  END_TRY catch (const EnvoyException& e) {
    ENVOY_LOG(warn,
              "Failed to detect resource attributes from the "
              "environment: {}.",
              e.what());
  }

  for (const auto& pair : StringUtil::splitToken(attributes_str, ",", false)) {
    const auto keyValue = StringUtil::splitToken(pair, "=");
    if (keyValue.size() != 2) {
      ENVOY_LOG(warn,
                "Invalid resource format from the environment, invalid "
                "text: {}.",
                pair);
      continue;
    }

    const std::string key = std::string(keyValue[0]);
    const std::string value = std::string(keyValue[1]);
    resource->attributes_[key] = value;
  }
  resource_ = resource;
}
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
