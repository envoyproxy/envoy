#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

namespace {

/**
 * @brief Resolves the new schema url when merging two resources.
 * This function implements the algorithm as defined in the OpenTelemetry Resource SDK
 * specification. @see
 * https://github.com/open-telemetry/opentelemetry-specification/blob/v1.24.0/specification/resource/sdk.md#merge
 *
 * @param old_schema_url The old resource's schema URL.
 * @param updating_schema_url The updating resource's schema URL.
 * @return std::string The calculated schema URL.
 */
std::string resolveSchemaUrl(const std::string& old_schema_url,
                             const std::string& updating_schema_url) {
  if (old_schema_url.empty()) {
    return updating_schema_url;
  }
  if (updating_schema_url.empty()) {
    return old_schema_url;
  }
  if (old_schema_url == updating_schema_url) {
    return old_schema_url;
  }
  // The OTel spec leaves this case (when both have value but are different) unspecified.
  ENVOY_LOG_MISC(warn, "Resource schemaUrl conflict. Fall-back to old schema url: {}",
                 old_schema_url);
  return old_schema_url;
}
} // namespace

void Resource::merge(const Resource& other) {
  schema_url_ = resolveSchemaUrl(schema_url_, other.schema_url_);
  if (other.attributes_.empty()) {
    return;
  }
  for (auto const& attr : other.attributes_) {
    attributes_.insert_or_assign(attr.first, attr.second);
  }
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
