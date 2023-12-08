#include <filesystem>
#include <string>

#include "source/extensions/tracers/opentelemetry/resource_detectors/dynatrace/dynatrace_metadata_file_reader.h"

#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

TEST(DynatraceMetadataFileReaderTest, DynatraceNotDeployed) {
  const std::string indirection_file = "dt_metadata_e617c525669e072eebe3d0f08212e8f2.properties";

  DynatraceMetadataFileReaderPtr reader = std::make_unique<DynatraceMetadataFileReaderImpl>();

  EXPECT_THROW(reader->readEnrichmentFile(indirection_file), EnvoyException);
}

TEST(DynatraceMetadataFileReaderTest, DtMetadataFile) {
  const std::string expected_data = "attribute=value";
  const std::string metadata_file = "dt_metadata.properties";

  TestEnvironment::writeStringToFileForTest(metadata_file, expected_data);

  DynatraceMetadataFileReaderPtr reader = std::make_unique<DynatraceMetadataFileReaderImpl>();

  std::string actual_data =
      reader->readEnrichmentFile(TestEnvironment::temporaryPath(metadata_file));

  EXPECT_EQ(actual_data, expected_data);
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
