#include "common/http/utility.h"

#include "test/common/http/utility_fuzz.pb.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Fuzz {

DEFINE_PROTO_FUZZER(const test::common::http::UtilityTestCase& input) {
  switch (input.utility_selector_case()) {
  case test::common::http::UtilityTestCase::kParseQueryString: {
    Http::Utility::parseQueryString(input.parse_query_string());
    break;
  }
  case test::common::http::UtilityTestCase::kParseCookieValue: {
    const auto& parse_cookie_value = input.parse_cookie_value();
    Http::TestHeaderMapImpl headers;
    for (const std::string& cookie : parse_cookie_value.cookies()) {
      headers.addCopy("cookie", cookie);
    }
    Http::Utility::parseCookieValue(headers, parse_cookie_value.key());
    break;
  }
  case test::common::http::UtilityTestCase::kHasSetCookie: {
    const auto& has_set_cookie = input.has_set_cookie();
    Http::TestHeaderMapImpl headers;
    for (const std::string& cookie : has_set_cookie.cookies()) {
      headers.addCopy("set-cookie", cookie);
    }
    Http::Utility::hasSetCookie(headers, has_set_cookie.key());
    break;
  }
  case test::common::http::UtilityTestCase::kGetLastAddressFromXff: {
    const auto& get_last_address_from_xff = input.get_last_address_from_xff();
    Http::TestHeaderMapImpl headers;
    headers.addCopy("x-forwarded-for", get_last_address_from_xff.xff());
    // Take num_to_skip modulo 32 to avoid wasting time in lala land.
    Http::Utility::getLastAddressFromXFF(headers, get_last_address_from_xff.num_to_skip() % 32);
    break;
  }
  case test::common::http::UtilityTestCase::kExtractHostPathFromUri: {
    absl::string_view host;
    absl::string_view path;
    Http::Utility::extractHostPathFromUri(input.extract_host_path_from_uri(), host, path);
    break;
  }
  default:
    // Nothing to do.
    break;
  }
}

} // namespace Fuzz
} // namespace Envoy
