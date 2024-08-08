#include "source/common/orca/orca_parser.h"

#include <string>

#include "envoy/http/header_map.h"

#include "source/common/common/base64.h"
#include "source/common/common/fmt.h"

#include "absl/strings/string_view.h"

using ::Envoy::Http::HeaderMap;
using xds::data::orca::v3::OrcaLoadReport;

namespace Envoy {
namespace Orca {

namespace {

const Http::LowerCaseString& endpointLoadMetricsHeaderBin() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, kEndpointLoadMetricsHeaderBin);
}

} // namespace

absl::StatusOr<OrcaLoadReport> parseOrcaLoadReportHeaders(const HeaderMap& headers) {
  OrcaLoadReport load_report;

  // Binary protobuf format.
  if (const auto header_bin = headers.get(endpointLoadMetricsHeaderBin()); !header_bin.empty()) {
    const auto header_value = header_bin[0]->value().getStringView();
    const std::string decoded_value = Envoy::Base64::decode(header_value);
    if (!load_report.ParseFromString(decoded_value)) {
      return absl::InvalidArgumentError(
          fmt::format("unable to parse binaryheader to OrcaLoadReport: {}", header_value));
    }
  } else {
    return absl::NotFoundError("no ORCA data sent from the backend");
  }

  return load_report;
}

} // namespace Orca
} // namespace Envoy
