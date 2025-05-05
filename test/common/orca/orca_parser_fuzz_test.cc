#include <limits>

#include "source/common/common/base64.h"
#include "source/common/orca/orca_parser.h"

#include "test/common/orca/orca_parser_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "xds/data/orca/v3/orca_load_report.pb.h"

namespace Envoy {
namespace Upstream {

DEFINE_PROTO_FUZZER(const test::common::upstream::OrcaParserTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  // Try to parse the load report, expect no crash.
  Http::TestResponseHeaderMapImpl headers;
  for (const auto& header : input.response_headers().headers()) {
    headers.addCopy(header.key(), header.value());
  }
  auto ignored_load_report = Orca::parseOrcaLoadReportHeaders(headers);
}

} // namespace Upstream
} // namespace Envoy
