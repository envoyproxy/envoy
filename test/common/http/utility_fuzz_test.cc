#include "source/common/http/utility.h"

#include "test/common/http/utility_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Fuzz {
namespace {

DEFINE_PROTO_FUZZER(const test::common::http::UtilityTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }
  switch (input.utility_selector_case()) {
  case test::common::http::UtilityTestCase::kParseQueryString: {
    // TODO(dio): Add the case when using parseAndDecodeQueryString().
    Http::Utility::parseQueryString(input.parse_query_string());
    break;
  }
  case test::common::http::UtilityTestCase::kParseCookieValue: {
    const auto& parse_cookie_value = input.parse_cookie_value();
    // Use the production RequestHeaderMapImpl to avoid timeouts from TestHeaderMapImpl asserts.
    auto headers = Http::RequestHeaderMapImpl::create();
    for (const std::string& cookie : parse_cookie_value.cookies()) {
      headers->addCopy(Http::LowerCaseString("cookie"), replaceInvalidCharacters(cookie));
    }
    Http::Utility::parseCookieValue(*headers, parse_cookie_value.key());
    break;
  }
  case test::common::http::UtilityTestCase::kGetLastAddressFromXff: {
    const auto& get_last_address_from_xff = input.get_last_address_from_xff();
    // Use the production RequestHeaderMapImpl to avoid timeouts from TestHeaderMapImpl asserts.
    auto headers = Http::RequestHeaderMapImpl::create();
    headers->addCopy(Http::LowerCaseString("x-forwarded-for"),
                     replaceInvalidCharacters(get_last_address_from_xff.xff()));
    // Take num_to_skip modulo 32 to avoid wasting time in lala land.
    Http::Utility::getLastAddressFromXFF(*headers, get_last_address_from_xff.num_to_skip() % 32);
    break;
  }
  case test::common::http::UtilityTestCase::kExtractHostPathFromUri: {
    absl::string_view host;
    absl::string_view path;
    Http::Utility::extractHostPathFromUri(input.extract_host_path_from_uri(), host, path);
    break;
  }
  case test::common::http::UtilityTestCase::kPercentEncodingString: {
    Http::Utility::PercentEncoding::encode(input.percent_encoding_string());
    break;
  }
  case test::common::http::UtilityTestCase::kPercentDecodingString: {
    Http::Utility::PercentEncoding::decode(input.percent_decoding_string());
    break;
  }
  case test::common::http::UtilityTestCase::kParseParameters: {
    const auto& parse_parameters = input.parse_parameters();
    // TODO(dio): Add a case when doing parse_parameters with decode_params flag true.
    Http::Utility::parseParameters(parse_parameters.data(), parse_parameters.start(),
                                   /*decode_params*/ false);
    break;
  }
  case test::common::http::UtilityTestCase::kFindQueryString: {
    Http::HeaderString path(input.find_query_string());
    Http::Utility::findQueryStringStart(path);
    break;
  }
  case test::common::http::UtilityTestCase::kMakeSetCookieValue: {
    const auto& cookie_value = input.make_set_cookie_value();
    std::chrono::seconds max_age(cookie_value.max_age());
    Http::Utility::makeSetCookieValue(cookie_value.key(), cookie_value.value(), cookie_value.path(),
                                      max_age, cookie_value.httponly());
    break;
  }
  case test::common::http::UtilityTestCase::kParseAuthorityString: {
    const auto& authority_string = input.parse_authority_string();
    Http::Utility::parseAuthority(authority_string);
    break;
  }
  case test::common::http::UtilityTestCase::kInitializeAndValidate: {
    const auto& options = input.initialize_and_validate();
    try {
      Http2::Utility::initializeAndValidateOptions(options);
    } catch (EnvoyException& e) {
      absl::string_view msg = e.what();
      // initializeAndValidateOptions throws exceptions for 4 different reasons due to malformed
      // settings, so check for them and allow any other exceptions through
      if (absl::StartsWith(
              msg, "server push is not supported by Envoy and can not be enabled via a SETTINGS "
                   "parameter.") ||
          absl::StartsWith(
              msg, "the \"allow_connect\" SETTINGS parameter must only be configured through the "
                   "named field") ||
          absl::StartsWith(
              msg, "inconsistent HTTP/2 custom SETTINGS parameter(s) detected; identifiers =") ||
          absl::EndsWith(
              msg, "HTTP/2 SETTINGS parameter(s) can not be configured through both named and "
                   "custom parameters")) {
        ENVOY_LOG_MISC(trace, "Caught exception {} in initializeAndValidateOptions test", e.what());
      } else {
        throw EnvoyException(e.what());
      }
    }
    break;
  }

  default:
    // Nothing to do.
    break;
  }
}

} // namespace
} // namespace Fuzz
} // namespace Envoy
