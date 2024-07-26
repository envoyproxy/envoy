#include <limits>

#include "source/common/common/base64.h"
#include "source/common/orca/orca_parser.h"

#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "xds/data/orca/v3/orca_load_report.pb.h"

namespace Envoy {
namespace Orca {
namespace {

// Returns an example OrcaLoadReport proto with all fields populated.
static xds::data::orca::v3::OrcaLoadReport ExampleOrcaLoadReport() {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.set_cpu_utilization(0.7);
  orca_load_report.set_application_utilization(0.8);
  orca_load_report.set_mem_utilization(0.9);
  orca_load_report.set_eps(2);
  orca_load_report.set_rps_fractional(1000);
  orca_load_report.mutable_named_metrics()->insert({"foo", 123});
  orca_load_report.mutable_named_metrics()->insert({"bar", 0.2});
  return orca_load_report;
}

TEST(OrcaParserUtilTest, NoHeaders) {
  Http::TestRequestHeaderMapImpl headers{};
  // parseOrcaLoadReport returns error when no ORCA data is sent from
  // the backend.
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(absl::NotFoundError("no ORCA data sent from the backend")));
}

TEST(OrcaParserUtilTest, MissingOrcaHeaders) {
  Http::TestRequestHeaderMapImpl headers{{"wrong-header", "wrong-value"}};
  // parseOrcaLoadReport returns error when no ORCA data is sent from
  // the backend.
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(absl::NotFoundError("no ORCA data sent from the backend")));
}

TEST(OrcaParserUtilTest, MultipleOrcaHeaders) {
  Http::TestRequestHeaderMapImpl headers{{kEndpointLoadMetricsHeader, "value-not-processed"},
                                         {kEndpointLoadMetricsHeaderBin, "value-not-processed"}};
  EXPECT_THAT(
      parseOrcaLoadReportHeaders(headers),
      StatusHelpers::HasStatus(absl::InvalidArgumentError("more than one ORCA header found")));
}

TEST(OrcaParserUtilTest, NativeHttpEncodedHeader) {
  Http::TestRequestHeaderMapImpl headers{
      {kEndpointLoadMetricsHeader,
       "cpu_utilization:0.7,application_utilization:0.8,mem_utilization:0.9,"
       "rps_fractional:1000,eps:2,"
       "named_metrics.foo:123,named_metrics.bar:0.2"}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::IsOkAndHolds(ProtoEq(ExampleOrcaLoadReport())));
}

TEST(OrcaParserUtilTest, NativeHttpEncodedHeaderIncorrectFieldType) {
  Http::TestRequestHeaderMapImpl headers{{kEndpointLoadMetricsHeader, "cpu_utilization:\"0.7\""}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(
                  absl::InvalidArgumentError("unable to parse custom backend load metric "
                                             "value(cpu_utilization): \"0.7\"")));
}

TEST(OrcaParserUtilTest, NativeHttpEncodedHeaderNanMetricValue) {
  Http::TestRequestHeaderMapImpl headers{
      {kEndpointLoadMetricsHeader,
       absl::StrCat("cpu_utilization:", std::numeric_limits<double>::quiet_NaN())}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(absl::InvalidArgumentError(
                  "custom backend load metric value(cpu_utilization) cannot be NaN.")));
}

TEST(OrcaParserUtilTest, NativeHttpEncodedHeaderInfinityMetricValue) {
  Http::TestRequestHeaderMapImpl headers{
      {kEndpointLoadMetricsHeader,
       absl::StrCat("cpu_utilization:", std::numeric_limits<double>::infinity())}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(absl::InvalidArgumentError(
                  "custom backend load metric value(cpu_utilization) cannot be "
                  "infinity.")));
}

TEST(OrcaParserUtilTest, NativeHttpEncodedHeaderContainsDuplicateMetric) {
  Http::TestRequestHeaderMapImpl headers{
      {kEndpointLoadMetricsHeader, "cpu_utilization:0.7,cpu_utilization:0.8"}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(absl::AlreadyExistsError(absl::StrCat(
                  kEndpointLoadMetricsHeader, " contains duplicate metric: cpu_utilization"))));
}

TEST(OrcaParserUtilTest, NativeHttpEncodedHeaderUnsupportedMetric) {
  Http::TestRequestHeaderMapImpl headers{
      {kEndpointLoadMetricsHeader, "cpu_utilization:0.7,unsupported_metric:0.8"}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(
                  absl::InvalidArgumentError("unsupported metric name: unsupported_metric")));
}

TEST(OrcaParserUtilTest, NativeHttpEncodedHeaderContainsDuplicateNamedMetric) {
  Http::TestRequestHeaderMapImpl headers{{kEndpointLoadMetricsHeader,
                                          "named_metrics.foo:123,named_metrics.duplicate:123,"
                                          "named_metrics.duplicate:0.2"}};
  EXPECT_THAT(
      parseOrcaLoadReportHeaders(headers),
      StatusHelpers::HasStatus(absl::AlreadyExistsError(absl::StrCat(
          kEndpointLoadMetricsHeader, " contains duplicate metric: named_metrics.duplicate"))));
}

TEST(OrcaParserUtilTest, NativeHttpEncodedHeaderContainsEmptyNamedMetricKey) {
  Http::TestRequestHeaderMapImpl headers{{kEndpointLoadMetricsHeader, "named_metrics.:123"}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(absl::InvalidArgumentError("named metric key is empty.")));
}

TEST(OrcaParserUtilTest, InvalidNativeHttpEncodedHeader) {
  Http::TestRequestHeaderMapImpl headers{
      {kEndpointLoadMetricsHeader, "not-a-list-of-key-value-pairs"}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(
                  absl::InvalidArgumentError("metric values cannot be empty strings")));
}

TEST(OrcaParserUtilTest, JsonHeader) {
  Http::TestRequestHeaderMapImpl headers{
      {kEndpointLoadMetricsHeaderJson,
       "{\"cpu_utilization\": 0.7, \"application_utilization\": 0.8, "
       "\"mem_utilization\": 0.9, \"rps_fractional\": 1000, \"eps\": 2, "
       "\"named_metrics\": {\"foo\": 123,\"bar\": 0.2}}"}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::IsOkAndHolds(ProtoEq(ExampleOrcaLoadReport())));
}

TEST(OrcaParserUtilTest, InvalidJsonHeader) {
  Http::TestRequestHeaderMapImpl headers{
      {kEndpointLoadMetricsHeaderJson, "not-a-valid-json-string"}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                       testing::HasSubstr("invalid JSON")));
}

TEST(OrcaParserUtilTest, JsonHeaderUnknownField) {
  Http::TestRequestHeaderMapImpl headers{
      {kEndpointLoadMetricsHeaderJson,
       "{\"cpu_utilization\": 0.7, \"application_utilization\": 0.8, "
       "\"mem_utilization\": 0.9, \"rps_fractional\": 1000, \"eps\": 2,  "
       "\"unknown_field\": 2,"
       "\"named_metrics\": {\"foo\": 123,\"bar\": 0.2}}"}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                       testing::HasSubstr("invalid JSON")));
}

TEST(OrcaParserUtilTest, JsonHeaderIncorrectFieldType) {
  Http::TestRequestHeaderMapImpl headers{
      {kEndpointLoadMetricsHeaderJson, "{\"cpu_utilization\": \"0.7\""}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                       testing::HasSubstr("invalid JSON")));
}

TEST(OrcaParserUtilTest, BinaryHeader) {
  std::string proto_string =
      TestUtility::getProtobufBinaryStringFromMessage(ExampleOrcaLoadReport());
  auto orca_load_report_header_bin =
      Envoy::Base64::encode(proto_string.c_str(), proto_string.length());
  Http::TestRequestHeaderMapImpl headers{
      {kEndpointLoadMetricsHeaderBin, orca_load_report_header_bin}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::IsOkAndHolds(ProtoEq(ExampleOrcaLoadReport())));
}

TEST(OrcaParserUtilTest, InvalidBinaryHeader) {
  std::string proto_string =
      TestUtility::getProtobufBinaryStringFromMessage(ExampleOrcaLoadReport());
  // Force a bad base64 encoding by shortening the length of the output.
  auto orca_load_report_header_bin =
      Envoy::Base64::encode(proto_string.c_str(), proto_string.length() / 2);
  Http::TestRequestHeaderMapImpl headers{
      {kEndpointLoadMetricsHeaderBin, orca_load_report_header_bin}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(
                  absl::StatusCode::kInvalidArgument,
                  testing::HasSubstr("unable to parse binaryheader to OrcaLoadReport")));
}

} // namespace
} // namespace Orca
} // namespace Envoy
