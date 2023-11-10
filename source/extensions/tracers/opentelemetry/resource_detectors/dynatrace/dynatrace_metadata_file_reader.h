#pragma once

#include <array>
#include <memory>
#include <string>

#include "envoy/common/pure.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * @brief A file reader that reads the content of the Dynatrace metadata enrichment files.
 * When OneAgent is monitoring your application, it provides access to the enrichment files.
 * These files do not physically exists in your file system, but are provided by the OneAgent on
 * demand. This allows obtaining not only information about the host, but also about process.
 *
 * @see
 * https://docs.dynatrace.com/docs/shortlink/enrichment-files#oneagent-virtual-files
 *
 */
class DynatraceMetadataFileReader {
public:
  virtual ~DynatraceMetadataFileReader() = default;

  /**
   * @brief Reads the enrichment file and returns the enrichment metadata.
   *
   * @param file_name The file name.
   * @return const std::string String (java-like properties) containing the enrichment metadata.
   */
  virtual std::string readEnrichmentFile(const std::string& file_name) PURE;
};

using DynatraceMetadataFileReaderPtr = std::unique_ptr<DynatraceMetadataFileReader>;

class DynatraceMetadataFileReaderImpl : public DynatraceMetadataFileReader {
public:
  std::string readEnrichmentFile(const std::string& file_name) override;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
