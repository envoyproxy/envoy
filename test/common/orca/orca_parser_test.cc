#include <string>

#include "source/common/common/base64.h"
#include "source/common/orca/orca_parser.h"

#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
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

TEST(OrcaParserUtilTest, BinaryHeader) {
  const std::string proto_string =
      TestUtility::getProtobufBinaryStringFromMessage(exampleOrcaLoadReport());
  const auto orca_load_report_header_bin =
      Envoy::Base64::encode(proto_string.c_str(), proto_string.length());
  Http::TestRequestHeaderMapImpl headers{
      {std::string(kEndpointLoadMetricsHeaderBin), orca_load_report_header_bin}};
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

} // namespace
} // namespace Orca
} // namespace Envoy
