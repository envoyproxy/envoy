#include "envoy/http/header_validator_errors.h"

#include "source/extensions/http/header_validators/envoy_default/character_tables.h"
#include "source/extensions/http/header_validators/envoy_default/http1_header_validator.h"
#include "source/extensions/http/header_validators/envoy_default/http2_header_validator.h"

#include "test/extensions/http/header_validators/envoy_default/header_validator_test.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {
namespace {

using ::Envoy::Http::HeaderString;
using ::Envoy::Http::Protocol;
using ::Envoy::Http::TestRequestHeaderMapImpl;

// This test suite runs the same tests against both H/1 and H/2 header validators.
class HttpCommonValidationTest : public HeaderValidatorTest,
                                 public testing::TestWithParam<Protocol> {
protected:
  ::Envoy::Http::ServerHeaderValidatorPtr createUhv(absl::string_view config_yaml) {
    envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
        typed_config;
    TestUtility::loadFromYaml(std::string(config_yaml), typed_config);

    HeaderValidatorConfigOverrides config_overrides{scoped_runtime_.loader().snapshot().getBoolean(
        "envoy.uhv.allow_non_compliant_characters_in_path", true)};
    if (GetParam() == Protocol::Http11) {
      return std::make_unique<ServerHttp1HeaderValidator>(typed_config, Protocol::Http11, stats_,
                                                          config_overrides);
    }
    return std::make_unique<ServerHttp2HeaderValidator>(typed_config, GetParam(), stats_,
                                                        config_overrides);
  }

  void validateAllCharacters(absl::string_view path,
                             absl::string_view additionally_allowed_characters) {
    auto uhv = createUhv(empty_config);

    for (uint32_t ascii = 0x0; ascii <= 0xff; ++ascii) {
      std::string copy(path);
      copy[12] = static_cast<char>(ascii);
      HeaderString invalid_value{};
      setHeaderStringUnvalidated(invalid_value, copy);
      ::Envoy::Http::TestRequestHeaderMapImpl headers{
          {":scheme", "https"}, {":authority", "envoy.com"}, {":method", "GET"}};
      headers.addViaMove(HeaderString(absl::string_view(":path")), std::move(invalid_value));
      if (::Envoy::Http::testCharInTable(kPathHeaderCharTable, static_cast<char>(ascii)) ||
          absl::StrContains(additionally_allowed_characters, static_cast<char>(ascii))) {
        EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
      } else {
        EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers), "uhv.invalid_url");
      }
    }
  }

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

TEST_P(HttpCommonValidationTest, DelInPathRejected) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "true"}});
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":authority", "envoy.com"},
                                                  {":path", "/path/with/DE\x7FL"},
                                                  {":method", "GET"}};
  auto uhv = createUhv(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers), "uhv.invalid_url");

  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "false"}});
  uhv = createUhv(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers), "uhv.invalid_url");
}

TEST_P(HttpCommonValidationTest, DelInQueryRejected) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "true"}});
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":authority", "envoy.com"},
                                                  {":path", "/query?with=DE\x7FL"},
                                                  {":method", "GET"}};
  auto uhv = createUhv(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers), "uhv.invalid_url");

  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "false"}});
  uhv = createUhv(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers), "uhv.invalid_url");
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

// With the allow_non_compliant_characters_in_path set to false a requerst with URL path containing
// characters not allowed in https://datatracker.ietf.org/doc/html/rfc3986#section-3.3 RFC is
// rejected.
TEST_P(HttpCommonValidationTest, PathCharacterSetValidation) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "false"}});
  // ? and # start query and fragment
  // validateAllCharacters modifies 12th character in the `path` parameter
  validateAllCharacters("/path/with/additional/characters", "?#");
}

TEST_P(HttpCommonValidationTest, QueryCharacterSetValidation) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "false"}});
  // # starts fragment
  validateAllCharacters("/query?key=additional/characters", "?#");
}

TEST_P(HttpCommonValidationTest, FragmentCharacterSetValidation) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "false"}});
  validateAllCharacters("/q?k=v#fragment/additional/characters", "?");
}

namespace {
std::string generateExtendedAsciiString() {
  std::string extended_ascii_string;
  for (uint32_t ascii = 0x80; ascii <= 0xff; ++ascii) {
    extended_ascii_string.push_back(static_cast<char>(ascii));
  }
  return extended_ascii_string;
}
} // namespace

// The AdditionalCharacters* tests validate behavior with respect to allowing additional characters
// in URL path: For H/1 and H/3 the additional characters are "<>[]^`{}\| For H/2 additional
// characters also include space, TAB and all extended ASCII
TEST_P(HttpCommonValidationTest, AdditionalCharactersInPathAllowed) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "true"}});
  std::string additionally_allowed_characters("\"<>[]^`{}\\|");
  if (GetParam() == Protocol::Http2) {
    additionally_allowed_characters += generateExtendedAsciiString() + "\t ";
  }
  validateAllCharacters("/path/with/additional/characters",
                        absl::StrCat("?#", additionally_allowed_characters));
  /*
  auto uhv = createUhv(empty_config);

  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"}, {":authority", "envoy.com"}, {":method", "GET"}, {":path",
  R"EOF(/some/path/with/characters)EOF"}}; EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  // Note that \ is translated to / and [] remain unencoded
  EXPECT_EQ(headers.path(), "/some/path/with%22%3C%3E[]%5E%60%7B%7D/%7C/characters");
  */
}

TEST_P(HttpCommonValidationTest, AdditionalCharactersInPathAllowedWithoutPathNormalization) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "true"}});
  auto uhv = createUhv(no_path_normalization);

  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"},
      {":authority", "envoy.com"},
      {":method", "GET"},
      {":path", R"EOF(/some/path/with"<>[]^`{}\|/characters)EOF"}};
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  // Note that \ is translated to / and [] remain unencoded
  EXPECT_EQ(headers.path(), "/some/path/with\"<>[]^`{}\\|/characters");
}

TEST_P(HttpCommonValidationTest, AdditionalCharactersInQueryAllowed) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "true"}});
  auto uhv = createUhv(empty_config);

  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"},
      {":authority", "envoy.com"},
      {":method", "GET"},
      {":path", R"EOF(/some/path/with?value="<>[]^`{}\|/characters)EOF"}};
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  EXPECT_EQ(headers.path(), "/some/path/with?value=\"<>[]^`{}\\|/characters");
}

TEST_P(HttpCommonValidationTest, AdditionalCharactersInFragmentAllowed) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "true"}});
  auto uhv = createUhv(empty_config);

  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"},
      {":authority", "envoy.com"},
      {":method", "GET"},
      {":path", R"EOF(/some/path/with?value=aaa#"<>[]^`{}\|/characters)EOF"}};
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  EXPECT_EQ(headers.path(), "/some/path/with?value=aaa#\"<>[]^`{}\\|/characters");
}

// With the allow_non_compliant_characters_in_path set to false a requerst with URL path with the
// "<>[]^`{}\| additional characters is rejected.
TEST_P(HttpCommonValidationTest, AdditionalCharactersInPathRejected) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "false"}});
  auto uhv = createUhv(empty_config);
  constexpr absl::string_view additional_characters = R"EOF("<>[]^`{}\|)EOF";

  for (char c : additional_characters) {
    std::string path("/path/with/additional/characters");
    path[12] = c;
    HeaderString invalid_value{};
    setHeaderStringUnvalidated(invalid_value, path);
    ::Envoy::Http::TestRequestHeaderMapImpl headers{
        {":scheme", "https"}, {":authority", "envoy.com"}, {":method", "GET"}};
    headers.addViaMove(HeaderString(absl::string_view(":path")), std::move(invalid_value));
    EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers), "uhv.invalid_url");
  }
}

} // namespace
} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
