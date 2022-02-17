#include "source/common/http/path_utility.h"

#include "test/common/http/path_utility_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Fuzz {
namespace {
DEFINE_PROTO_FUZZER(const test::common::http::PathUtilityTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  // The following log is needed to pass the `check_build` tests of
  // Cluster-Fuzz for empty inputs.
  ENVOY_LOG_MISC(trace, "Input: {}", input.DebugString());

  switch (input.path_utility_selector_case()) {
  case test::common::http::PathUtilityTestCase::kCanonicalPath: {
    auto request_headers = fromHeaders<Http::TestRequestHeaderMapImpl>(
        input.canonical_path().request_headers(), {},
        {":path"}); // needs to have path header in order to be valid
    Http::PathUtil::canonicalPath(request_headers);
    ASSERT(!request_headers.getPathValue().empty());
    break;
  }
  case test::common::http::PathUtilityTestCase::kMergeSlashes: {
    auto request_headers = fromHeaders<Http::TestRequestHeaderMapImpl>(
        input.merge_slashes().request_headers(), {}, {":path"});
    Http::PathUtil::mergeSlashes(request_headers);
    break;
  }
  case test::common::http::PathUtilityTestCase::kRemoveQueryAndFragment: {
    auto path = input.remove_query_and_fragment().path();
    auto sanitized_path = Http::PathUtil::removeQueryAndFragment(path);
    ASSERT(path.find(std::string(sanitized_path)) != std::string::npos);
    break;
  }
  default:
    break;
  }
}

} // namespace
} // namespace Fuzz
} // namespace Envoy
