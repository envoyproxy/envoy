#pragma once

#include <array>
#include <string>

#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/dynatrace_resource_detector.pb.h"

#include "source/common/common/logger.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/dynatrace/dynatrace_metadata_file_reader.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * @brief A resource detector that reads the content of the Dynatrace metadata enrichment files.
 * When OneAgent is monitoring your application, it provides access to the enrichment files.
 * These files do not physically exists in your file system, but are provided by the OneAgent on
 * demand. This allows obtaining not only information about the host, but also about process.
 *
 * Dynatrace can be deployed in multiple ways and flavors, depending on the environment.
 * The available Dynatrace enrichment files vary depending on how it is deployed. E.g. In k8s via
 * the Dynatrace operator.
 *
 * Since the resource detector is not aware how Dynatrace is deployed, the detector attempts to
 * read all Dynatrace enrichment files. In such cases, reading some of these files may fail but
 * this is expected and does not classify as a problem with the detector. The detector may also not
 * detect any attributes, for example when a Dynatrace deployment is not successful. In such cases,
 * Envoy will be started with no enrichment but Dynatrace users have the means to find out which
 * Dynatrace deployment is failing.
 *
 * @see
 * https://docs.dynatrace.com/docs/shortlink/enrichment-files
 *
 */
class DynatraceResourceDetector : public ResourceDetector, Logger::Loggable<Logger::Id::tracing> {
public:
  DynatraceResourceDetector(const envoy::extensions::tracers::opentelemetry::resource_detectors::
                                v3::DynatraceResourceDetectorConfig& config,
                            DynatraceMetadataFileReaderPtr dynatrace_file_reader)
      : config_(config), dynatrace_file_reader_(std::move(dynatrace_file_reader)) {}
  Resource detect() override;

  static const std::vector<std::string>& dynatraceMetadataFiles() {
    CONSTRUCT_ON_FIRST_USE(std::vector<std::string>,
                           {
                               "dt_metadata_e617c525669e072eebe3d0f08212e8f2.properties",
                               "/var/lib/dynatrace/enrichment/dt_metadata.properties",
                               "/var/lib/dynatrace/enrichment/dt_host_metadata.properties",
                           });
  }

private:
  const envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
      DynatraceResourceDetectorConfig config_;
  DynatraceMetadataFileReaderPtr dynatrace_file_reader_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
