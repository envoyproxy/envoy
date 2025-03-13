#include <memory>
#include <string>

#include "envoy/extensions/tracers/opentelemetry/resource_detectors/v3/dynatrace_resource_detector.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/resource_detectors/dynatrace/dynatrace_metadata_file_reader.h"
#include "source/extensions/tracers/opentelemetry/resource_detectors/dynatrace/dynatrace_resource_detector.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

class MockDynatraceFileReader : public DynatraceMetadataFileReader {
public:
  MOCK_METHOD(std::string, readEnrichmentFile, (const std::string& file_path));
};

TEST(DynatraceResourceDetectorTest, DynatraceNotDeployed) {
  auto dt_file_reader = std::make_unique<NiceMock<MockDynatraceFileReader>>();
  EXPECT_CALL(*dt_file_reader, readEnrichmentFile(_)).WillRepeatedly(Return(""));

  envoy::extensions::tracers::opentelemetry::resource_detectors::v3::DynatraceResourceDetectorConfig
      config;

  auto detector = std::make_shared<DynatraceResourceDetector>(config, std::move(dt_file_reader));
  Resource resource = detector->detect();

  EXPECT_EQ(resource.schema_url_, "");
  EXPECT_EQ(0, resource.attributes_.size());
}

TEST(DynatraceResourceDetectorTest, OnlyOneAgentInstalled) {
  ResourceAttributes expected_attributes = {
      {"dt.entity.host", "HOST-abc"},
      {"dt.entity.process_group_instance", "PROCESS_GROUP_INSTANCE-abc"}};

  auto dt_file_reader = std::make_unique<NiceMock<MockDynatraceFileReader>>();

  std::string host_attrs = fmt::format(R"EOF(
dt.entity.host=HOST-abc
dt.host_group.id=
)EOF");

  std::string process_attrs = fmt::format(R"EOF(
dt.entity.process_group_instance=PROCESS_GROUP_INSTANCE-abc
)EOF");

  EXPECT_CALL(*dt_file_reader,
              readEnrichmentFile("dt_metadata_e617c525669e072eebe3d0f08212e8f2.properties"))
      .WillRepeatedly(Return(process_attrs));
  EXPECT_CALL(*dt_file_reader,
              readEnrichmentFile("/var/lib/dynatrace/enrichment/dt_host_metadata.properties"))
      .WillRepeatedly(Return(host_attrs));

  EXPECT_CALL(*dt_file_reader,
              readEnrichmentFile("/var/lib/dynatrace/enrichment/dt_metadata.properties"))
      .WillRepeatedly(Return(""));

  envoy::extensions::tracers::opentelemetry::resource_detectors::v3::DynatraceResourceDetectorConfig
      config;

  auto detector = std::make_shared<DynatraceResourceDetector>(config, std::move(dt_file_reader));
  Resource resource = detector->detect();

  EXPECT_EQ(resource.schema_url_, "");
  EXPECT_EQ(2, resource.attributes_.size());

  for (auto& actual : resource.attributes_) {
    auto expected = expected_attributes.find(actual.first);

    EXPECT_TRUE(expected != expected_attributes.end());
    EXPECT_EQ(expected->second, actual.second);
  }
}

TEST(DynatraceResourceDetectorTest, Dynatracek8sOperator) {
  ResourceAttributes expected_attributes = {{"k8s.pod.uid", "123"}, {"k8s.pod.name", "envoy"}};

  auto dt_file_reader = std::make_unique<NiceMock<MockDynatraceFileReader>>();

  std::string k8s_attrs = fmt::format(R"EOF(
k8s.pod.uid=123
k8s.pod.name=envoy
)EOF");

  EXPECT_CALL(*dt_file_reader,
              readEnrichmentFile("dt_metadata_e617c525669e072eebe3d0f08212e8f2.properties"))
      .WillRepeatedly(Return(""));
  EXPECT_CALL(*dt_file_reader,
              readEnrichmentFile("/var/lib/dynatrace/enrichment/dt_host_metadata.properties"))
      .WillRepeatedly(Return(""));
  EXPECT_CALL(*dt_file_reader,
              readEnrichmentFile("/var/lib/dynatrace/enrichment/dt_metadata.properties"))
      .WillRepeatedly(Return(k8s_attrs));

  envoy::extensions::tracers::opentelemetry::resource_detectors::v3::DynatraceResourceDetectorConfig
      config;

  auto detector = std::make_shared<DynatraceResourceDetector>(config, std::move(dt_file_reader));
  Resource resource = detector->detect();

  EXPECT_EQ(resource.schema_url_, "");
  EXPECT_EQ(2, resource.attributes_.size());

  for (auto& actual : resource.attributes_) {
    auto expected = expected_attributes.find(actual.first);

    EXPECT_TRUE(expected != expected_attributes.end());
    EXPECT_EQ(expected->second, actual.second);
  }
}

TEST(DynatraceResourceDetectorTest, TestFailureDetectionLog) {
  // No enrichment file found, should log a line
  {
    auto dt_file_reader = std::make_unique<NiceMock<MockDynatraceFileReader>>();

    EXPECT_CALL(*dt_file_reader, readEnrichmentFile(_)).WillRepeatedly(Return(""));

    envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
        DynatraceResourceDetectorConfig config;

    auto detector = std::make_shared<DynatraceResourceDetector>(config, std::move(dt_file_reader));
    EXPECT_LOG_CONTAINS(
        "warn",
        "Dynatrace OpenTelemetry resource detector is configured but could not detect attributes.",
        detector->detect());
  }

  // At least one enrichment file found, should NOT log a line
  {
    auto dt_file_reader = std::make_unique<NiceMock<MockDynatraceFileReader>>();

    std::string k8s_attrs = fmt::format(R"EOF(
    k8s.pod.uid=123
    k8s.pod.name=envoy
    )EOF");

    EXPECT_CALL(*dt_file_reader,
                readEnrichmentFile("dt_metadata_e617c525669e072eebe3d0f08212e8f2.properties"))
        .WillRepeatedly(Return(""));
    EXPECT_CALL(*dt_file_reader,
                readEnrichmentFile("/var/lib/dynatrace/enrichment/dt_host_metadata.properties"))
        .WillRepeatedly(Return(""));
    EXPECT_CALL(*dt_file_reader,
                readEnrichmentFile("/var/lib/dynatrace/enrichment/dt_metadata.properties"))
        .WillRepeatedly(Return(k8s_attrs));

    envoy::extensions::tracers::opentelemetry::resource_detectors::v3::
        DynatraceResourceDetectorConfig config;

    auto detector = std::make_shared<DynatraceResourceDetector>(config, std::move(dt_file_reader));
    EXPECT_LOG_NOT_CONTAINS(
        "warn",
        "Dynatrace OpenTelemetry resource detector is configured but could not detect attributes.",
        detector->detect());
  }
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
