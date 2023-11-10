#include "source/extensions/tracers/opentelemetry/resource_detectors/dynatrace/dynatrace_resource_detector.h"

#include <fstream>
#include <iostream>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {
namespace {

void addAttributes(const std::string& content, Resource& resource) {
  for (const auto& line : StringUtil::splitToken(content, "\n", false, true)) {
    const auto keyValue = StringUtil::splitToken(line, "=");
    if (keyValue.size() != 2) {
      continue;
    }

    const std::string key = std::string(keyValue[0]);
    const std::string value = std::string(keyValue[1]);
    resource.attributes_[key] = value;
  }
}

} // namespace

Resource DynatraceResourceDetector::detect() {
  Resource resource;
  resource.schemaUrl_ = "";
  int failureCount = 0;

  for (const auto& file_name : DT_METADATA_FILES) {
    TRY_NEEDS_AUDIT {
      std::string content = dynatrace_file_reader_->readEnrichmentFile(std::string(file_name));
      if (content.empty()) {
        failureCount++;
      } else {
        addAttributes(content, resource);
      }
    }
    END_TRY catch (const EnvoyException&) { failureCount++; }
  }

  if (failureCount > 0) {
    ENVOY_LOG(
        warn,
        "Dynatrace OpenTelemetry resource detector is configured but could not detect attributes. "
        "Check the Dynatrace deployment status to ensure it is correctly deployed.");
  }

  return resource;
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
