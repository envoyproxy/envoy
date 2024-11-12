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
static xds::data::orca::v3::OrcaLoadReport exampleOrcaLoadReport() {
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

TEST(OrcaParserUtilTest, InvalidOrcaHeaderPrefix) {
  // Verify that error is returned when unknown/invalid prefix is found in ORCA
  // header value.
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeader), "BAD random-value"}};
  EXPECT_THAT(
      parseOrcaLoadReportHeaders(headers),
      StatusHelpers::HasStatus(absl::InvalidArgumentError("unsupported ORCA header format: BAD")));
}

TEST(OrcaParserUtilTest, InvalidOrcaHeaderPrefixWithLargePrefix) {
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeader), "BADBAD random-value"}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(
                  absl::InvalidArgumentError("unsupported ORCA header format: BADBA")));
}

TEST(OrcaParserUtilTest, EmptyOrcaHeader) {
  Http::TestRequestHeaderMapImpl headers{{std::string(kEndpointLoadMetricsHeader), ""}};
  EXPECT_THAT(
      parseOrcaLoadReportHeaders(headers),
      StatusHelpers::HasStatus(absl::InvalidArgumentError("unsupported ORCA header format: ")));
}

TEST(OrcaParserUtilTest, NativeHttpEncodedHeader) {
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeader),
       absl::StrCat(kHeaderFormatPrefixText,
                    "cpu_utilization:0.7,application_utilization:0.8,mem_utilization:0.9,"
                    "rps_fractional:1000,eps:2,"
                    "named_metrics.foo:123,named_metrics.bar:0.2")}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::IsOkAndHolds(ProtoEq(exampleOrcaLoadReport())));
}

TEST(OrcaParserUtilTest, NativeHttpEncodedHeaderIncorrectFieldType) {
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeader),
       absl::StrCat(kHeaderFormatPrefixText, "cpu_utilization:\"0.7\"")}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(
                  absl::InvalidArgumentError("unable to parse custom backend load metric "
                                             "value(cpu_utilization): \"0.7\"")));
}

TEST(OrcaParserUtilTest, NativeHttpEncodedHeaderNanMetricValue) {
  Http::TestRequestHeaderMapImpl headers{{std::string(kEndpointLoadMetricsHeader),
                                          absl::StrCat(kHeaderFormatPrefixText, "cpu_utilization:",
                                                       std::numeric_limits<double>::quiet_NaN())}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(absl::InvalidArgumentError(
                  "custom backend load metric value(cpu_utilization) cannot be NaN.")));
}

TEST(OrcaParserUtilTest, NativeHttpEncodedHeaderInfinityMetricValue) {
  Http::TestRequestHeaderMapImpl headers{{std::string(kEndpointLoadMetricsHeader),
                                          absl::StrCat(kHeaderFormatPrefixText, "cpu_utilization:",
                                                       std::numeric_limits<double>::infinity())}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(absl::InvalidArgumentError(
                  "custom backend load metric value(cpu_utilization) cannot be "
                  "infinity.")));
}

TEST(OrcaParserUtilTest, NativeHttpEncodedHeaderContainsDuplicateMetric) {
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeader),
       absl::StrCat(kHeaderFormatPrefixText, "cpu_utilization:0.7,cpu_utilization:0.8")}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(absl::AlreadyExistsError(absl::StrCat(
                  kEndpointLoadMetricsHeader, " contains duplicate metric: cpu_utilization"))));
}

TEST(OrcaParserUtilTest, NativeHttpEncodedHeaderUnsupportedMetric) {
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeader),
       absl::StrCat(kHeaderFormatPrefixText, "cpu_utilization:0.7,unsupported_metric:0.8")}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(
                  absl::InvalidArgumentError("unsupported metric name: unsupported_metric")));
}

TEST(OrcaParserUtilTest, NativeHttpEncodedHeaderContainsDuplicateNamedMetric) {
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeader),
       absl::StrCat(
           kHeaderFormatPrefixText,
           "named_metrics.foo:123,named_metrics.duplicate:123,named_metrics.duplicate:0.2")}};
  EXPECT_THAT(
      parseOrcaLoadReportHeaders(headers),
      StatusHelpers::HasStatus(absl::AlreadyExistsError(absl::StrCat(
          kEndpointLoadMetricsHeader, " contains duplicate metric: named_metrics.duplicate"))));
}

TEST(OrcaParserUtilTest, NativeHttpEncodedHeaderContainsEmptyNamedMetricKey) {
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeader),
       absl::StrCat(kHeaderFormatPrefixText, "named_metrics.:123")}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(absl::InvalidArgumentError("named metric key is empty.")));
}

TEST(OrcaParserUtilTest, InvalidNativeHttpEncodedHeader) {
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeader),
       absl::StrCat(kHeaderFormatPrefixText, "not-a-list-of-key-value-pairs")}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(
                  absl::InvalidArgumentError("metric values cannot be empty strings")));
}

TEST(OrcaParserUtilTest, JsonHeader) {
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeader),
       absl::StrCat(kHeaderFormatPrefixJson,
                    "{\"cpu_utilization\": 0.7, \"application_utilization\": 0.8, "
                    "\"mem_utilization\": 0.9, \"rps_fractional\": 1000, \"eps\": 2, "
                    "\"named_metrics\": {\"foo\": 123,\"bar\": 0.2}}")}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::IsOkAndHolds(ProtoEq(exampleOrcaLoadReport())));
}

TEST(OrcaParserUtilTest, InvalidJsonHeader) {
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeader),
       absl::StrCat(kHeaderFormatPrefixJson, "JSON not-a-valid-json-string")}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                       testing::HasSubstr("invalid JSON")));
}

TEST(OrcaParserUtilTest, JsonHeaderUnknownField) {
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeader),
       absl::StrCat(kHeaderFormatPrefixJson,
                    "{\"cpu_utilization\": 0.7, \"application_utilization\": 0.8, "
                    "\"mem_utilization\": 0.9, \"rps_fractional\": 1000, \"eps\": 2,  "
                    "\"unknown_field\": 2,"
                    "\"named_metrics\": {\"foo\": 123,\"bar\": 0.2}}")}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                       testing::HasSubstr("invalid JSON")));
}

TEST(OrcaParserUtilTest, JsonHeaderIncorrectFieldType) {
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeader),
       absl::StrCat(kHeaderFormatPrefixJson, "{\"cpu_utilization\": \"0.7\"")}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                       testing::HasSubstr("invalid JSON")));
}

TEST(OrcaParserUtilTest, LegacyBinaryHeader) {
  // Verify processing of headers sent in legacy ORCA header inherited from gRPC
  // implementation works as intended.
  const std::string proto_string =
      TestUtility::getProtobufBinaryStringFromMessage(exampleOrcaLoadReport());
  const auto orca_load_report_header_bin =
      Envoy::Base64::encode(proto_string.c_str(), proto_string.length());
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeaderBin), orca_load_report_header_bin}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::IsOkAndHolds(ProtoEq(exampleOrcaLoadReport())));
}

TEST(OrcaParserUtilTest, BinaryHeader) {
  // Verify serialized binary header processing when using default ORCA header
  // and appropriate format prefix in the header value.
  const std::string proto_string =
      TestUtility::getProtobufBinaryStringFromMessage(exampleOrcaLoadReport());
  const auto orca_load_report_header_bin =
      Envoy::Base64::encode(proto_string.c_str(), proto_string.length());
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeader),
       absl::StrCat(kHeaderFormatPrefixBin, orca_load_report_header_bin)}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::IsOkAndHolds(ProtoEq(exampleOrcaLoadReport())));
}

TEST(OrcaParserUtilTest, InvalidBinaryHeader) {
  const std::string proto_string =
      TestUtility::getProtobufBinaryStringFromMessage(exampleOrcaLoadReport());
  // Force a bad base64 encoding by shortening the length of the output.
  const auto orca_load_report_header_bin =
      Envoy::Base64::encode(proto_string.c_str(), proto_string.length() / 2);
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeaderBin), orca_load_report_header_bin}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(
                  absl::StatusCode::kInvalidArgument,
                  testing::HasSubstr("unable to parse binaryheader to OrcaLoadReport")));
}

TEST(OrcaParserUtilTest, BinaryHeaderPopulatedWithReadableString) {
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeaderBin), "not-a-valid-binary-proto"}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(
                  absl::StatusCode::kInvalidArgument,
                  testing::HasSubstr(
                      "unable to decode ORCA binary header value: not-a-valid-binary-proto")));
}

TEST(OrcaParserUtilTest, EmptyBinaryHeader) {
  Http::TestRequestHeaderMapImpl headers{{std::string(kEndpointLoadMetricsHeaderBin), ""}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                       testing::HasSubstr("ORCA binary header value is empty")));
}

TEST(OrcaParserUtilTest, BinHeaderPrecedence) {
  // Verifies that the order of precedence (binary proto over native http
  // format) is observed when multiple ORCA headers are sent from the backend.
  const std::string proto_string =
      TestUtility::getProtobufBinaryStringFromMessage(exampleOrcaLoadReport());
  const auto orca_load_report_header_bin =
      Envoy::Base64::encode(proto_string.c_str(), proto_string.length());
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeader), "cpu_utilization:0.7"},
      {std::string(kEndpointLoadMetricsHeaderBin), orca_load_report_header_bin}};
  EXPECT_THAT(parseOrcaLoadReportHeaders(headers),
              StatusHelpers::IsOkAndHolds(ProtoEq(exampleOrcaLoadReport())));
}

} // namespace
} // namespace Orca
} // namespace Envoy
