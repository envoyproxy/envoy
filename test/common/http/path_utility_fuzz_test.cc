#include "common/http/path_utility.h"

#include "test/common/http/path_utility_fuzz.pb.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"


namespace Envoy {
namespace Fuzz {
namespace {
DEFINE_PROTO_FUZZER(const test::common::http::PathUtilityTestCase& input) {
    /*try {

    } catch () {
        
    }*/
    switch (input.path_utility_selector_case()) {
        case test::common::http::PathUtilityTestCase::kCanonicalPath: { //BIG QUESTION: HOW TO REPRESENT HEADER MAPS WITH PROTO
            auto request_headers = fromHeaders<Http::TestRequestHeaderMapImpl>(input.canonical_path().request_headers(),
                                                      {}, {});
            Http::PathUtil::canonicalPath(request_headers);
            break;
        }
        case test::common::http::PathUtilityTestCase::kMergeSlashes: {
            auto request_headers = fromHeaders<Http::TestRequestHeaderMapImpl>(input.merge_slashes().request_headers(),
                                                      {}, {});
            Http::PathUtil::mergeSlashes(request_headers);
            break;
        }
        default:
            break;
    }
}

}
} //namespace Fuzz
} //namespace Envoy