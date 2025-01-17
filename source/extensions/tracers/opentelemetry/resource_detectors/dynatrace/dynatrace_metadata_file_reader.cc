#include "source/extensions/tracers/opentelemetry/resource_detectors/dynatrace/dynatrace_metadata_file_reader.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "envoy/common/exception.h"

#include "source/common/common/logger.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

namespace {

bool isIndirectionFile(const std::string& file_name) {
  return absl::StartsWith(file_name, "dt_metadata_");
}

std::string readFile(const std::string& file_name) {
  if (file_name.empty()) {
    return "";
  }

  std::ifstream file(file_name);
  if (file.fail()) {
    throw EnvoyException(absl::StrCat("Unable to read Dynatrace enrichment file: ", file_name));
  }

  std::stringstream file_string;
  file_string << file.rdbuf();

  return file_string.str();
}

} // namespace

std::string DynatraceMetadataFileReaderImpl::readEnrichmentFile(const std::string& file_name) {
  if (const bool indirection_file = isIndirectionFile(file_name); indirection_file) {
    return readFile(readFile(file_name));
  } else {
    return readFile(file_name);
  }
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
