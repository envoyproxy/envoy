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
    const auto key_value = StringUtil::splitToken(line, "=");
    if (key_value.size() != 2) {
      continue;
    }
    resource.attributes_[std::string(key_value[0])] = std::string(key_value[1]);
  }
}

} // namespace

Resource DynatraceResourceDetector::detect() {
  Resource resource;
  resource.schema_url_ = "";
  int failure_count = 0;

  for (const auto& file_name : DynatraceResourceDetector::dynatraceMetadataFiles()) {
    TRY_NEEDS_AUDIT {
      std::string content = dynatrace_file_reader_->readEnrichmentFile(file_name);
      if (content.empty()) {
        failure_count++;
      } else {
        addAttributes(content, resource);
      }
    }
    END_TRY catch (const EnvoyException&) { failure_count++; }
  }

  if (failure_count > 0) {
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
