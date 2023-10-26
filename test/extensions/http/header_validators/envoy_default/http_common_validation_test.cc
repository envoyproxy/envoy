#include "envoy/http/header_validator_errors.h"

#include "source/extensions/http/header_validators/envoy_default/character_tables.h"
#include "source/extensions/http/header_validators/envoy_default/http1_header_validator.h"
#include "source/extensions/http/header_validators/envoy_default/http2_header_validator.h"

#include "test/extensions/http/header_validators/envoy_default/header_validator_utils.h"
#include "test/mocks/http/header_validator.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {
namespace {

using ::Envoy::Http::Protocol;
using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::Envoy::Http::UhvResponseCodeDetail;

// This test suite runs the same tests against both H/1 and H/2 header validators.
class HttpCommonValidationTest : public HeaderValidatorUtils,
                                 public testing::TestWithParam<Protocol> {
protected:
  ::Envoy::Http::ServerHeaderValidatorPtr createUhv(absl::string_view config_yaml) {
    envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
        typed_config;
    TestUtility::loadFromYaml(std::string(config_yaml), typed_config);

    ConfigOverrides overrides(scoped_runtime_.loader().snapshot());
    if (GetParam() == Protocol::Http11) {
      return std::make_unique<ServerHttp1HeaderValidator>(typed_config, Protocol::Http11, stats_,
                                                          overrides);
    }
    return std::make_unique<ServerHttp2HeaderValidator>(typed_config, GetParam(), stats_,
                                                        overrides);
  }

  ::testing::NiceMock<Envoy::Http::MockHeaderValidatorStats> stats_;
  TestScopedRuntime scoped_runtime_;
};

std::string protocolTestParamsToString(const ::testing::TestParamInfo<Protocol>& params) {
  return params.param == Protocol::Http11  ? "Http1"
         : params.param == Protocol::Http2 ? "Http2"
                                           : "Http3";
}

INSTANTIATE_TEST_SUITE_P(Protocols, HttpCommonValidationTest,
                         testing::Values(Protocol::Http11, Protocol::Http2, Protocol::Http3),
                         protocolTestParamsToString);

TEST_P(HttpCommonValidationTest, MalformedUrlEncodingAllowed) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.uhv_allow_malformed_url_encoding", "true"}});
  TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                   {":path", "/path%Z%30with%xYbad%7Jencoding%"},
                                   {":authority", "envoy.com"},
                                   {":method", "GET"}};
  auto uhv = createUhv(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  EXPECT_EQ(headers.path(), "/path%Z0with%xYbad%7Jencoding%");
}

TEST_P(HttpCommonValidationTest, MalformedUrlEncodingRejectedWithOverride) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.uhv_allow_malformed_url_encoding", "false"}});
  TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                   {":path", "/path%Z%30with%xYbad%7Jencoding%A"},
                                   {":authority", "envoy.com"},
                                   {":method", "GET"}};
  auto uhv = createUhv(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_REJECT_WITH_DETAILS(uhv->transformRequestHeaders(headers), "uhv.invalid_url");
}

TEST_P(HttpCommonValidationTest, BackslashInPathIsTranslatedToSlash) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "true"}});
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":path", "/path\\with/back\\/slash%5C"},
                                                  {":authority", "envoy.com"},
                                                  {":method", "GET"}};
  auto uhv = createUhv(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  EXPECT_EQ(headers.path(), "/path/with/back/slash%5C");
}

TEST_P(HttpCommonValidationTest, BackslashInPathIsRejectedWithOverride) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "false"}});
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":path", "/path\\with/back\\/slash%5c"},
                                                  {":authority", "envoy.com"},
                                                  {":method", "GET"}};
  auto uhv = createUhv(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers), "uhv.invalid_url");
}

// With the allow_non_compliant_characters_in_path set to false a request with URL path containing
// characters not allowed in https://datatracker.ietf.org/doc/html/rfc3986#section-3.3 RFC is
// rejected.
TEST_P(HttpCommonValidationTest, PathCharacterSetValidation) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "false"}});
  auto uhv = createUhv(fragment_in_path_allowed);
  // ? and # start query and fragment
  // validateAllCharacters modifies 12th character in the `path` parameter
  validateAllCharactersInUrlPath(*uhv, "/path/with/additional/characters", "?#");
}

TEST_P(HttpCommonValidationTest, QueryCharacterSetValidation) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "false"}});
  // # starts fragment
  auto uhv = createUhv(fragment_in_path_allowed);
  validateAllCharactersInUrlPath(*uhv, "/query?key=additional/characters", "?#");
}

TEST_P(HttpCommonValidationTest, FragmentCharacterSetValidation) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "false"}});
  auto uhv = createUhv(fragment_in_path_allowed);
  validateAllCharactersInUrlPath(*uhv, "/q?k=v#fragment/additional/characters", "?");
}

TEST_P(HttpCommonValidationTest, PathWithFragmentRejectedByDefault) {
  TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                   {":path", "/path/with?query=and#fragment"},
                                   {":authority", "envoy.com"},
                                   {":method", "GET"}};
  auto uhv = createUhv(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers),
                             UhvResponseCodeDetail::get().FragmentInUrlPath);
}

TEST_P(HttpCommonValidationTest, FragmentStrippedFromPathWhenConfigured) {
  TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                   {":path", "/path/with?query=and#fragment"},
                                   {":authority", "envoy.com"},
                                   {":method", "GET"}};
  auto uhv = createUhv(fragment_in_path_allowed);

  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  EXPECT_EQ(headers.path(), "/path/with?query=and");
}

TEST_P(HttpCommonValidationTest, ValidateRequestHeaderMapRedirectPath) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/dir1%2fdir2%5Cdir3%2F..%5Cdir4"},
                                                  {":authority", "envoy.com"}};
  auto uhv = createUhv(redirect_encoded_slash_config);
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  // Path normalization should result in redirect
  auto result = uhv->transformRequestHeaders(headers);
  EXPECT_EQ(
      result.action(),
      ::Envoy::Http::ServerHeaderValidator::RequestHeadersTransformationResult::Action::Redirect);
  EXPECT_EQ(result.details(), "uhv.path_normalization_redirect");
  // By default decoded backslash (%5C) is converted to forward slash.
  EXPECT_EQ(headers.path(), "/dir1/dir2/dir4");
}

TEST_P(HttpCommonValidationTest, ValidateRequestHeadersNoPathNormalization) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/dir1%2fdir2%5C.."},
                                                  {":authority", "envoy.com"}};
  auto uhv = createUhv(no_path_normalization);
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  auto result = uhv->transformRequestHeaders(headers);
  // Even with path normalization tuned off, the path_with_escaped_slashes_action option
  // still takes effect.
  EXPECT_EQ(
      result.action(),
      ::Envoy::Http::ServerHeaderValidator::RequestHeadersTransformationResult::Action::Redirect);
  EXPECT_EQ(headers.path(), "/dir1/dir2\\..");
}

TEST_P(HttpCommonValidationTest, NoPathNormalizationNoSlashDecoding) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/dir1%2Fdir2%5cdir3"},
                                                  {":authority", "envoy.com"}};
  auto uhv = createUhv(no_path_normalization_no_decoding_slashes);
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  auto result = uhv->transformRequestHeaders(headers);
  EXPECT_ACCEPT(result);
  EXPECT_EQ(headers.path(), "/dir1%2Fdir2%5cdir3");
}

TEST_P(HttpCommonValidationTest, NoPathNormalizationDecodeSlashesAndForward) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/dir1%2Fdir2%5c../dir3"},
                                                  {":authority", "envoy.com"}};
  auto uhv = createUhv(decode_slashes_and_forward);
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  auto result = uhv->transformRequestHeaders(headers);
  EXPECT_ACCEPT(result);
  EXPECT_EQ(headers.path(), "/dir1/dir2\\../dir3");
}

TEST_P(HttpCommonValidationTest, RejectEncodedSlashes) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/dir1%2Fdir2%5c../dir3"},
                                                  {":authority", "envoy.com"}};
  auto uhv = createUhv(reject_encoded_slashes);
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_REJECT_WITH_DETAILS(uhv->transformRequestHeaders(headers),
                             "uhv.escaped_slashes_in_url_path");
}

TEST_P(HttpCommonValidationTest, RejectPercent00ByDefault) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/dir1%00dir2"},
                                                  {":authority", "envoy.com"}};
  auto uhv = createUhv(empty_config);
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_REJECT_WITH_DETAILS(uhv->transformRequestHeaders(headers), "uhv.percent_00_in_url_path");
}

TEST_P(HttpCommonValidationTest, AllowPercent00WithOverride) {
  scoped_runtime_.mergeValues({{"envoy.uhv.reject_percent_00", "false"}});
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/dir1%00dir2"},
                                                  {":authority", "envoy.com"}};
  auto uhv = createUhv(empty_config);
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  EXPECT_EQ(headers.path(), "/dir1%00dir2");
}

} // namespace
} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
