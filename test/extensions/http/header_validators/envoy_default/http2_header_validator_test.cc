#include "source/extensions/http/header_validators/envoy_default/character_tables.h"
#include "source/extensions/http/header_validators/envoy_default/http2_header_validator.h"

#include "test/extensions/http/header_validators/envoy_default/header_validator_test.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {
namespace {

using ::Envoy::Extensions::Http::HeaderValidators::EnvoyDefault::Http2HeaderValidator;
using ::Envoy::Http::HeaderString;
using ::Envoy::Http::Protocol;

class Http2HeaderValidatorTest : public HeaderValidatorTest {
protected:
  Http2HeaderValidatorPtr createH2(absl::string_view config_yaml) {
    envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
        typed_config;
    TestUtility::loadFromYaml(std::string(config_yaml), typed_config);

    return std::make_unique<Http2HeaderValidator>(typed_config, Protocol::Http2, stream_info_);
  }
};

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

TEST_F(Http2HeaderValidatorTest, ValidateTE) {
  HeaderString trailers{"trailers"};
  HeaderString deflate{"deflate"};
  auto uhv = createH2(empty_config);
  EXPECT_ACCEPT(uhv->validateTEHeader(trailers));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateTEHeader(deflate), "uhv.http2.invalid_te");
}

TEST_F(Http2HeaderValidatorTest, ValidateGenericHeaderNameRejectConnectionHeaders) {
  HeaderString transfer_encodings[] = {HeaderString("transfer-encoding"),
                                       HeaderString("connection"), HeaderString("keep-alive"),
                                       HeaderString("upgrade"), HeaderString("proxy-connection")};
  auto uhv = createH2(empty_config);

  for (auto& encoding : transfer_encodings) {
    EXPECT_REJECT_WITH_DETAILS(uhv->validateGenericHeaderName(encoding),
                               "uhv.http2.connection_header_rejected");
  }
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderAuthority) {
  HeaderString authority{":authority"};
  HeaderString valid{"envoy.com"};
  HeaderString invalid{"user:pass@envoy.com"};
  auto uhv = createH2(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderEntry(authority, valid));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(authority, invalid),
                             UhvResponseCodeDetail::get().InvalidHostDeprecatedUserInfo);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderPath) {
  HeaderString path{":path"};
  HeaderString valid{"/"};
  HeaderString invalid{"/ bad path"};
  auto uhv = createH2(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderEntry(path, valid));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(path, invalid),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderTE) {
  HeaderString name{"te"};
  HeaderString valid{"trailers"};
  HeaderString invalid{"chunked"};
  auto uhv = createH2(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderEntry(name, valid));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(name, invalid),
                             "uhv.http2.invalid_te");
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMethod) {
  HeaderString method{":method"};
  HeaderString valid{"GET"};
  HeaderString invalid{"CUSTOM-METHOD"};
  auto uhv = createH2(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderEntry(method, valid));
  EXPECT_ACCEPT(uhv->validateRequestHeaderEntry(method, invalid));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderContentLength) {
  HeaderString content_length{"content-length"};
  HeaderString valid{"100"};
  HeaderString invalid{"10a2"};
  auto uhv = createH2(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderEntry(content_length, valid));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(content_length, invalid),
                             UhvResponseCodeDetail::get().InvalidContentLength);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderScheme) {
  HeaderString scheme{":scheme"};
  HeaderString valid{"https"};
  HeaderString invalid{"http_ssh"};
  auto uhv = createH2(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderEntry(scheme, valid));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(scheme, invalid),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderGeneric) {
  HeaderString valid_name{"x-foo"};
  HeaderString invalid_name{""};
  HeaderString valid_value{"bar"};

  HeaderString invalid_value;
  setHeaderStringUnvalidated(invalid_value, "hello\nworld");

  auto uhv = createH2(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderEntry(valid_name, valid_value));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(invalid_name, valid_value),
                             UhvResponseCodeDetail::get().EmptyHeaderName);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(valid_name, invalid_value),
                             UhvResponseCodeDetail::get().InvalidValueCharacters);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderAllowUnderscores) {
  HeaderString name{"x_foo"};
  HeaderString value{"bar"};
  auto uhv = createH2(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderEntry(name, value));
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderStatus) {
  HeaderString status{":status"};
  HeaderString valid{"200"};
  HeaderString invalid{"1024"};
  auto uhv = createH2(empty_config);

  EXPECT_ACCEPT(uhv->validateResponseHeaderEntry(status, valid));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderEntry(status, invalid),
                             UhvResponseCodeDetail::get().InvalidStatus);
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderGeneric) {
  HeaderString valid_name{"x-foo"};
  HeaderString valid_name_underscore{"x_foo"};
  HeaderString invalid_name{""};
  HeaderString valid_value{"bar"};
  HeaderString invalid_name_uppercase{"X-Foo"};

  HeaderString invalid_value;
  setHeaderStringUnvalidated(invalid_value, "hello\nworld");

  auto uhv = createH2(empty_config);

  EXPECT_ACCEPT(uhv->validateResponseHeaderEntry(valid_name, valid_value));
  EXPECT_ACCEPT(uhv->validateResponseHeaderEntry(valid_name_underscore, valid_value));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderEntry(invalid_name, valid_value),
                             UhvResponseCodeDetail::get().EmptyHeaderName);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderEntry(valid_name, invalid_value),
                             UhvResponseCodeDetail::get().InvalidValueCharacters);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderEntry(invalid_name_uppercase, valid_value),
                             UhvResponseCodeDetail::get().InvalidNameCharacters);
}

TEST_F(Http2HeaderValidatorTest, ValidateGenericHeaderName) {
  auto uhv = createH2(empty_config);
  std::string name{"aaaaa"};
  for (int i = 0; i < 0xff; ++i) {
    char c = static_cast<char>(i);
    HeaderString header_string{"x"};
    name[2] = c;

    setHeaderStringUnvalidated(header_string, name);

    auto result = uhv->validateGenericHeaderName(header_string);
    if (testChar(kGenericHeaderNameCharTable, c) && (c < 'A' || c > 'Z')) {
      EXPECT_ACCEPT(result);
    } else if (c != '_') {
      EXPECT_REJECT_WITH_DETAILS(result, UhvResponseCodeDetail::get().InvalidNameCharacters);
    } else {
      EXPECT_REJECT_WITH_DETAILS(result, UhvResponseCodeDetail::get().InvalidUnderscore);
    }
  }
}

TEST_F(Http2HeaderValidatorTest, ValidateGenericHeaderKeyStrictInvalidEmpty) {
  HeaderString invalid_empty{""};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateGenericHeaderName(invalid_empty),
                             UhvResponseCodeDetail::get().EmptyHeaderName);
}

TEST_F(Http2HeaderValidatorTest, ValidateGenericHeaderKeyDropUnderscores) {
  HeaderString drop_underscore{"x_foo"};
  auto uhv = createH2(drop_headers_with_underscores_config);

  auto result = uhv->validateGenericHeaderName(drop_underscore);
  EXPECT_EQ(result.action(), decltype(result)::Action::DropHeader);
  EXPECT_EQ(result.details(), UhvResponseCodeDetail::get().InvalidUnderscore);
}

TEST_F(Http2HeaderValidatorTest, ValidateGenericHeaderKeyRejectDropUnderscores) {
  HeaderString invalid_with_underscore{"x_fo<o"};
  auto uhv = createH2(drop_headers_with_underscores_config);

  auto result = uhv->validateGenericHeaderName(invalid_with_underscore);
  EXPECT_REJECT_WITH_DETAILS(result, UhvResponseCodeDetail::get().InvalidNameCharacters);
}

} // namespace
} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
