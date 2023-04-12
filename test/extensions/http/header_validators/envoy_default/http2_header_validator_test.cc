#include "envoy/http/header_validator_errors.h"

#include "source/extensions/http/header_validators/envoy_default/character_tables.h"
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

using ::Envoy::Extensions::Http::HeaderValidators::EnvoyDefault::Http2HeaderValidator;
using ::Envoy::Http::HeaderString;
using ::Envoy::Http::Protocol;
using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::Envoy::Http::TestRequestTrailerMapImpl;
using ::Envoy::Http::TestResponseHeaderMapImpl;
using ::Envoy::Http::UhvResponseCodeDetail;

class Http2HeaderValidatorTest : public HeaderValidatorTest {
protected:
  Http2HeaderValidatorPtr createH2(absl::string_view config_yaml) {
    envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
        typed_config;
    TestUtility::loadFromYaml(std::string(config_yaml), typed_config);

    return std::make_unique<Http2HeaderValidator>(typed_config, Protocol::Http2, stats_);
  }

  TestRequestHeaderMapImpl makeGoodRequestHeaders() {
    return TestRequestHeaderMapImpl{
        {":scheme", "https"}, {":method", "GET"}, {":authority", "envoy.com"}, {":path", "/"}};
  }

  TestResponseHeaderMapImpl makeGoodResponseHeaders() {
    return TestResponseHeaderMapImpl{{":status", "200"}};
  }

  TestScopedRuntime scoped_runtime_;
};

TEST_F(Http2HeaderValidatorTest, GoodHeadersAccepted) {
  auto uhv = createH2(empty_config);
  TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(request_headers));
  TestResponseHeaderMapImpl response_headers = makeGoodResponseHeaders();
  EXPECT_ACCEPT(uhv->validateResponseHeaderMap(response_headers));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapAllowed) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/"},
                                                  {":authority", "envoy.com"},
                                                  {"x-foo", "bar"}};
  auto uhv = createH2(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapMissingPath) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"}, {":method", "GET"}, {"x-foo", "bar"}};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapMissingMethod) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"}, {":path", "/"}, {"x-foo", "bar"}};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidMethod);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapMissingScheme) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/"}, {"x-foo", "bar"}};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapExtraPseudoHeader) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"}, {":method", "GET"}, {":path", "/"}, {":foo", "bar"}};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidPseudoHeader);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapNoAuthorityIsOk) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"}, {":method", "GET"}, {":path", "/"}, {"x-foo", "bar"}};
  auto uhv = createH2(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapConnect) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":method", "CONNECT"}, {":authority", "envoy.com"}, {"x-foo", "bar"}};
  auto uhv = createH2(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapConnectExtraPseudoHeader) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":method", "CONNECT"}, {":scheme", "https"}, {":authority", "envoy.com"}, {"x-foo", "bar"}};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapConnectMissingAuthority) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "CONNECT"}, {"x-foo", "bar"}};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapConnectWithPath) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":method", "CONNECT"}, {":authority", "envoy.com"}, {":path", "/bar"}};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapConnectWithScheme) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":method", "CONNECT"}, {":authority", "envoy.com"}, {":scheme", "https"}};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapOptionsAsterisk) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "OPTIONS"},
                                                  {":path", "*"},
                                                  {":authority", "envoy.com"},
                                                  {"x-foo", "bar"}};
  auto uhv = createH2(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapNotOptionsAsterisk) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "*"},
                                                  {":authority", "envoy.com"},
                                                  {"x-foo", "bar"}};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapInvalidAuthority) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/"},
                                                  {":authority", "user:pass@envoy.com"},
                                                  {"x-foo", "bar"}};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidHostDeprecatedUserInfo);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapEmptyGenericName) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/"},
                                                  {":authority", "envoy.com"},
                                                  {"", "bar"}};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().EmptyHeaderName);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapUnderscoreHeadersAllowedByDefault) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/"},
                                                  {":authority", "envoy.com"},
                                                  {"x_foo", "bar"}};
  auto uhv = createH2(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
  EXPECT_EQ(headers, ::Envoy::Http::TestRequestHeaderMapImpl({{":scheme", "https"},
                                                              {":method", "GET"},
                                                              {":path", "/"},
                                                              {":authority", "envoy.com"},
                                                              {"x_foo", "bar"}}));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapDropUnderscoreHeaders) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/"},
                                                  {":authority", "envoy.com"},
                                                  {"x_foo", "bar"}};
  auto uhv = createH2(drop_headers_with_underscores_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
  EXPECT_EQ(
      headers,
      ::Envoy::Http::TestRequestHeaderMapImpl(
          {{":scheme", "https"}, {":method", "GET"}, {":path", "/"}, {":authority", "envoy.com"}}));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapRejectUnderscoreHeaders) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/"},
                                                  {":authority", "envoy.com"},
                                                  {"x_foo", "bar"}};
  auto uhv = createH2(reject_headers_with_underscores_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidUnderscore);
}

TEST_F(Http2HeaderValidatorTest, RequestExtendedConnect) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"},  {":method", "CONNECT"},      {":protocol", "websocket"},
      {":path", "/foo/bar"}, {":authority", "envoy.com"}, {"x-foo", "bar"}};
  auto uhv = createH2(empty_config);
  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
}

TEST_F(Http2HeaderValidatorTest, RequestExtendedConnectNoScheme) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "CONNECT"},
                                                  {":protocol", "websocket"},
                                                  {":path", "/foo/bar"},
                                                  {":authority", "envoy.com"}};
  auto uhv = createH2(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(Http2HeaderValidatorTest, RequestExtendedConnectNoPath) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "CONNECT"},
                                                  {":protocol", "websocket"},
                                                  {":authority", "envoy.com"}};
  auto uhv = createH2(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http2HeaderValidatorTest, RequestExtendedConnectInvalidPath) {
  // Character with value 0x7F is invalid in URI path
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "CONNECT"},
                                                  {":protocol", "websocket"},
                                                  {":path", "/fo\x7fo/bar"},
                                                  {":authority", "envoy.com"}};
  auto uhv = createH2(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http2HeaderValidatorTest, RequestExtendedConnectPathNormalization) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "CONNECT"},
                                                  {":protocol", "websocket"},
                                                  {":path", "/./dir1/../dir2"},
                                                  {":authority", "envoy.com"}};
  auto uhv = createH2(empty_config);

  EXPECT_TRUE(uhv->validateRequestHeaderMap(headers).ok());
  EXPECT_EQ(headers.path(), "/dir2");
}

TEST_F(Http2HeaderValidatorTest, RequestExtendedConnectNoAuthorityIsOk) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "CONNECT"},
                                                  {":path", "/foo/bar"},
                                                  {":protocol", "websocket"}};
  auto uhv = createH2(empty_config);
  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderMapValid) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"x-foo", "bar"}};
  auto uhv = createH2(empty_config);

  EXPECT_ACCEPT(uhv->validateResponseHeaderMap(headers));
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderMapMissingStatus) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{{"x-foo", "bar"}};
  auto uhv = createH2(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidStatus);
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderMapExtraPseudoHeader) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{
      {":status", "200"}, {":foo", "bar"}, {"x-foo", "bar"}};
  auto uhv = createH2(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidPseudoHeader);
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderMapInvalidStatus) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "1024"}, {"x-foo", "bar"}};
  auto uhv = createH2(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidStatus);
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderMapEmptyGenericName) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"", "bar"}};
  auto uhv = createH2(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderMap(headers),
                             UhvResponseCodeDetail::get().EmptyHeaderName);
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderMapDropUnderscoreHeaders) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"x_foo", "bar"}};
  auto uhv = createH2(drop_headers_with_underscores_config);

  EXPECT_ACCEPT(uhv->validateResponseHeaderMap(headers));
  EXPECT_EQ(headers, ::Envoy::Http::TestResponseHeaderMapImpl({{":status", "200"}}));
}

TEST_F(Http2HeaderValidatorTest, ValidateGenericHeaderNameRejectConnectionHeaders) {
  std::string connection_headers[] = {"transfer-encoding", "connection", "keep-alive", "upgrade",
                                      "proxy-connection"};
  auto uhv = createH2(empty_config);

  for (auto& header : connection_headers) {
    TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
    request_headers.addCopy(header, "some-value");
    EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(request_headers),
                               "uhv.http2.connection_header_rejected");
  }
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderPath) {
  auto uhv = createH2(empty_config);
  TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
  request_headers.setPath("/ bad path");

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(request_headers),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderTETrailersAllowed) {
  auto uhv = createH2(empty_config);
  TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
  request_headers.addCopy("te", "trailers");

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(request_headers));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderInvalidTERejected) {
  auto uhv = createH2(empty_config);
  TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
  request_headers.addCopy("te", "deflate");

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(request_headers),
                             "uhv.http2.invalid_te");
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderCustomMethod) {
  auto uhv = createH2(empty_config);
  TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
  request_headers.setMethod("CUSTOM-METHOD");

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(request_headers));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderContentLength) {
  auto uhv = createH2(empty_config);
  TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
  request_headers.setContentLength("100");
  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(request_headers));

  request_headers.setContentLength("10a2");
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(request_headers),
                             UhvResponseCodeDetail::get().InvalidContentLength);
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderContentLength) {
  auto uhv = createH2(empty_config);
  TestResponseHeaderMapImpl response_headers = makeGoodResponseHeaders();
  response_headers.setContentLength("100");
  EXPECT_ACCEPT(uhv->validateResponseHeaderMap(response_headers));

  response_headers.setContentLength("10a2");
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderMap(response_headers),
                             UhvResponseCodeDetail::get().InvalidContentLength);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderInvalidScheme) {
  auto uhv = createH2(empty_config);
  TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
  request_headers.setScheme("http_ssh");
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(request_headers),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderInvalidProtocol) {
  auto uhv = createH2(empty_config);
  TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                   {":method", "CONNECT"},
                                   {":protocol", "something \x7F bad"},
                                   {":path", "/foo/bar"},
                                   {":authority", "envoy.com"}};

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidValueCharacters);
}

TEST_F(Http2HeaderValidatorTest, InvalidRequestHeaderNameRejected) {
  auto uhv = createH2(empty_config);
  TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
  // This header name is valid
  request_headers.addCopy("x-foo", "bar");
  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(request_headers));

  // Reject invalid name
  request_headers.addCopy("foo oo", "bar");
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(request_headers),
                             UhvResponseCodeDetail::get().InvalidNameCharacters);
}

TEST_F(Http2HeaderValidatorTest, InvalidRequestHeaderValueRejected) {
  auto uhv = createH2(empty_config);
  TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
  HeaderString invalid_value{};
  setHeaderStringUnvalidated(invalid_value, "hello\nworld");
  request_headers.addViaMove(HeaderString(absl::string_view("x-foo")), std::move(invalid_value));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(request_headers),
                             UhvResponseCodeDetail::get().InvalidValueCharacters);
}

TEST_F(Http2HeaderValidatorTest, InvalidResponseHeaderNameRejected) {
  auto uhv = createH2(empty_config);
  TestResponseHeaderMapImpl response_headers = makeGoodResponseHeaders();
  // This header name is valid
  response_headers.addCopy("x-foo", "bar");
  EXPECT_ACCEPT(uhv->validateResponseHeaderMap(response_headers));

  // Reject invalid name
  response_headers.addCopy("foo oo", "bar");
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderMap(response_headers),
                             UhvResponseCodeDetail::get().InvalidNameCharacters);
}

TEST_F(Http2HeaderValidatorTest, InvalidResponseHeaderValueRejected) {

  auto uhv = createH2(empty_config);
  TestResponseHeaderMapImpl response_headers = makeGoodResponseHeaders();
  HeaderString invalid_value{};
  setHeaderStringUnvalidated(invalid_value, "hello\nworld");
  response_headers.addViaMove(HeaderString(absl::string_view("x-foo")), std::move(invalid_value));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderMap(response_headers),
                             UhvResponseCodeDetail::get().InvalidValueCharacters);
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderInvalidStatusRejected) {
  auto uhv = createH2(empty_config);
  TestResponseHeaderMapImpl response_headers = makeGoodResponseHeaders();
  response_headers.setStatus("1024");

  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderMap(response_headers),
                             UhvResponseCodeDetail::get().InvalidStatus);
}

TEST_F(Http2HeaderValidatorTest, ValidateUppercaseResponseHeaderRejected) {
  auto uhv = createH2(empty_config);

  HeaderString invalid_name_uppercase;
  setHeaderStringUnvalidated(invalid_name_uppercase, "X-Foo");
  TestResponseHeaderMapImpl headers = makeGoodResponseHeaders();
  headers.addViaMove(std::move(invalid_name_uppercase),
                     HeaderString(absl::string_view("some value")));

  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidNameCharacters);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestGenericHeaderName) {
  auto uhv = createH2(empty_config);
  std::string name{"aaaaa"};
  for (int i = 0; i < 0xff; ++i) {
    char c = static_cast<char>(i);
    HeaderString header_string{"x"};
    name[2] = c;

    setHeaderStringUnvalidated(header_string, name);
    TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
    request_headers.addViaMove(std::move(header_string),
                               HeaderString(absl::string_view("some value")));

    auto result = uhv->validateRequestHeaderMap(request_headers);
    if (testChar(kGenericHeaderNameCharTable, c) && (c < 'A' || c > 'Z')) {
      EXPECT_ACCEPT(result);
    } else if (c != '_') {
      EXPECT_REJECT_WITH_DETAILS(result, UhvResponseCodeDetail::get().InvalidNameCharacters);
    } else {
      EXPECT_REJECT_WITH_DETAILS(result, UhvResponseCodeDetail::get().InvalidUnderscore);
    }
  }
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseGenericHeaderName) {
  auto uhv = createH2(empty_config);
  std::string name{"aaaaa"};
  for (int i = 0; i < 0xff; ++i) {
    char c = static_cast<char>(i);
    HeaderString header_string{"x"};
    name[2] = c;

    setHeaderStringUnvalidated(header_string, name);
    TestResponseHeaderMapImpl headers = makeGoodResponseHeaders();
    headers.addViaMove(std::move(header_string), HeaderString(absl::string_view("some value")));

    auto result = uhv->validateResponseHeaderMap(headers);
    if (testChar(kGenericHeaderNameCharTable, c) && (c < 'A' || c > 'Z')) {
      EXPECT_ACCEPT(result);
    } else if (c != '_') {
      EXPECT_REJECT_WITH_DETAILS(result, UhvResponseCodeDetail::get().InvalidNameCharacters);
    } else {
      EXPECT_REJECT_WITH_DETAILS(result, UhvResponseCodeDetail::get().InvalidUnderscore);
    }
  }
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapNormalizePath) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/./dir1/../dir2"},
                                                  {":authority", "envoy.com"}};
  auto uhv = createH2(empty_config);

  EXPECT_TRUE(uhv->validateRequestHeaderMap(headers).ok());
  EXPECT_EQ(headers.path(), "/dir2");
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapRejectPath) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"}, {":method", "GET"}, {":path", "/.."}, {":authority", "envoy.com"}};
  auto uhv = createH2(empty_config);
  auto result = uhv->validateRequestHeaderMap(headers);
  EXPECT_EQ(result.action(), HeaderValidator::RejectOrRedirectAction::Reject);
  EXPECT_EQ(result.details(), UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapRedirectPath) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/dir1%2fdir2"},
                                                  {":authority", "envoy.com"}};
  auto uhv = createH2(redirect_encoded_slash_config);
  auto result = uhv->validateRequestHeaderMap(headers);
  EXPECT_EQ(result.action(), HeaderValidator::RejectOrRedirectAction::Redirect);
  EXPECT_EQ(result.details(), "uhv.path_noramlization_redirect");
  EXPECT_EQ(headers.path(), "/dir1/dir2");
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeadersPathNormalizationDisabled) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/./dir1%2f../dir2"},
                                                  {":authority", "envoy.com"}};
  auto uhv = createH2(no_path_normalization);

  EXPECT_TRUE(uhv->validateRequestHeaderMap(headers).ok());
  EXPECT_EQ(headers.path(), "/./dir1%2f../dir2");
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestTrailerMap) {
  auto uhv = createH2(empty_config);
  ::Envoy::Http::TestRequestTrailerMapImpl request_trailer_map{{"trailer1", "value1"},
                                                               {"trailer2", "values"}};
  EXPECT_TRUE(uhv->validateRequestTrailerMap(request_trailer_map));
}

TEST_F(Http2HeaderValidatorTest, ValidateInvalidRequestTrailerMap) {
  auto uhv = createH2(empty_config);
  // H/2 trailers must not contain pseudo headers
  ::Envoy::Http::TestRequestTrailerMapImpl request_trailer_map{{":path", "value1"},
                                                               {"trailer2", "values"}};
  auto result = uhv->validateRequestTrailerMap(request_trailer_map);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.details(), "uhv.invalid_name_characters");
}

TEST_F(Http2HeaderValidatorTest, ValidateInvalidValueRequestTrailerMap) {
  auto uhv = createH2(empty_config);
  ::Envoy::Http::TestRequestTrailerMapImpl request_trailer_map{{"trailer1", "value1"},
                                                               {"trailer2", "values"}};
  ::Envoy::Http::HeaderString invalid_value;
  // \n must not be present in header values
  invalid_value.setCopyUnvalidatedForTestOnly("invalid\nvalue");
  request_trailer_map.addViaMove(::Envoy::Http::HeaderString("trailer3"), std::move(invalid_value));
  auto result = uhv->validateRequestTrailerMap(request_trailer_map);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.details(), "uhv.invalid_value_characters");
}

TEST_F(Http2HeaderValidatorTest, UnderscoreHeadersAllowedInRequestTrailersByDefault) {
  ::Envoy::Http::TestRequestTrailerMapImpl trailers{{"trailer1", "value1"}, {"x_foo", "bar"}};
  auto uhv = createH2(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestTrailerMap(trailers));
  EXPECT_EQ(trailers,
            ::Envoy::Http::TestRequestTrailerMapImpl({{"trailer1", "value1"}, {"x_foo", "bar"}}));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestTrailersDropUnderscoreHeaders) {
  ::Envoy::Http::TestRequestTrailerMapImpl trailers{{"trailer1", "value1"}, {"x_foo", "bar"}};
  auto uhv = createH2(drop_headers_with_underscores_config);

  EXPECT_ACCEPT(uhv->validateRequestTrailerMap(trailers));
  EXPECT_EQ(trailers, ::Envoy::Http::TestRequestTrailerMapImpl({{"trailer1", "value1"}}));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestTrailersRejectUnderscoreHeaders) {
  ::Envoy::Http::TestRequestTrailerMapImpl trailers{{"trailer1", "value1"}, {"x_foo", "bar"}};
  auto uhv = createH2(reject_headers_with_underscores_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestTrailerMap(trailers),
                             UhvResponseCodeDetail::get().InvalidUnderscore);
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseTrailerMap) {
  auto uhv = createH2(empty_config);
  ::Envoy::Http::TestResponseTrailerMapImpl response_trailer_map{{"trailer1", "value1"}};
  EXPECT_TRUE(uhv->validateResponseTrailerMap(response_trailer_map).ok());
}

TEST_F(Http2HeaderValidatorTest, ValidateInvalidResponseTrailerMap) {
  auto uhv = createH2(empty_config);
  // H/2 trailers must not contain pseudo headers
  ::Envoy::Http::TestResponseTrailerMapImpl response_trailer_map{{":status", "200"},
                                                                 {"trailer1", "value1"}};
  auto result = uhv->validateResponseTrailerMap(response_trailer_map);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.details(), "uhv.invalid_name_characters");
}

TEST_F(Http2HeaderValidatorTest, ValidateInvalidValueResponseTrailerMap) {
  auto uhv = createH2(empty_config);
  // The DEL (0x7F) character is illegal in header values
  ::Envoy::Http::TestResponseTrailerMapImpl response_trailer_map{{"trailer0", "abcd\x7F\\ef"},
                                                                 {"trailer1", "value1"}};
  auto result = uhv->validateResponseTrailerMap(response_trailer_map);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.details(), "uhv.invalid_value_characters");
}

TEST_F(Http2HeaderValidatorTest, BackslashInPathIsTranslatedToSlash) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.uhv_translate_backslash_to_slash", "true"}});
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":path", "/path\\with/back\\/slash%5c"},
                                                  {":authority", "envoy.com"},
                                                  {":method", "GET"}};
  auto uhv = createH2(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
  EXPECT_EQ(headers.path(), "/path/with/back/slash%5C");
}

TEST_F(Http2HeaderValidatorTest, BackslashInPathIsRejectedWithOverride) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.uhv_translate_backslash_to_slash", "false"}});
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":path", "/path\\with/back\\/slash%5c"},
                                                  {":authority", "envoy.com"},
                                                  {":method", "GET"}};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers), "uhv.invalid_url");
}

} // namespace
} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
