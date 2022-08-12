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

  EXPECT_TRUE(uhv->validateRequestHeaderMap(headers).ok());
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

  EXPECT_TRUE(uhv->validateRequestHeaderMap(headers).ok());
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

  EXPECT_TRUE(uhv->validateRequestHeaderMap(headers).ok());
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
                             UhvResponseCodeDetail::get().InvalidHost);
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

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderMapValid) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"x-foo", "bar"}};
  auto uhv = createH2(empty_config);

  EXPECT_TRUE(uhv->validateResponseHeaderMap(headers).ok());
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

TEST_F(Http2HeaderValidatorTest, ValidateTE) {
  HeaderString trailers{"trailers"};
  HeaderString deflate{"deflate"};
  auto uhv = createH2(empty_config);
  EXPECT_TRUE(uhv->validateTEHeader(trailers).ok());
  EXPECT_REJECT_WITH_DETAILS(uhv->validateTEHeader(deflate), "uhv.http2.invalid_te");
}

TEST_F(Http2HeaderValidatorTest, ValidateGenericPath) {
  HeaderString valid{"/"};
  auto uhv = createH2(empty_config);
  // TODO(meilya) - after path normalization has been approved and implemented
  EXPECT_TRUE(uhv->validateGenericPathHeader(valid).ok());
}

TEST_F(Http2HeaderValidatorTest, ValidateGenericHeaderKeyConnectionRejectedTransferEncoding) {
  HeaderString transfer_encoding{"transfer-encoding"};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateGenericHeaderName(transfer_encoding),
                             "uhv.http2.connection_header_rejected");
}

TEST_F(Http2HeaderValidatorTest, ValidateGenericHeaderKeyConnectionRejectedConnection) {
  HeaderString connection{"connection"};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateGenericHeaderName(connection),
                             "uhv.http2.connection_header_rejected");
}

TEST_F(Http2HeaderValidatorTest, ValidateGenericHeaderKeyConnectionRejectedKeepAlive) {
  HeaderString keep_alive{"keep-alive"};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateGenericHeaderName(keep_alive),
                             "uhv.http2.connection_header_rejected");
}

TEST_F(Http2HeaderValidatorTest, ValidateGenericHeaderKeyConnectionRejectedUpgrde) {
  HeaderString upgrade{"upgrade"};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateGenericHeaderName(upgrade),
                             "uhv.http2.connection_header_rejected");
}

TEST_F(Http2HeaderValidatorTest, ValidateGenericHeaderKeyConnectionRejectedProxy) {
  HeaderString proxy_connection{"proxy-connection"};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateGenericHeaderName(proxy_connection),
                             "uhv.http2.connection_header_rejected");
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderAuthority) {
  HeaderString authority{":authority"};
  HeaderString valid{"envoy.com"};
  HeaderString invalid{"user:pass@envoy.com"};
  auto uhv = createH2(empty_config);

  EXPECT_TRUE(uhv->validateRequestHeaderEntry(authority, valid).ok());
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(authority, invalid),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderAuthorityHost) {
  HeaderString host{"host"};
  HeaderString valid{"envoy.com"};
  HeaderString invalid{"user:pass@envoy.com"};
  auto uhv = createH2(empty_config);

  EXPECT_TRUE(uhv->validateRequestHeaderEntry(host, valid).ok());
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(host, invalid),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderPath) {
  HeaderString path{":path"};
  HeaderString valid{"/"};
  HeaderString invalid{"/ bad path"};
  auto uhv = createH2(empty_config);

  EXPECT_TRUE(uhv->validateRequestHeaderEntry(path, valid).ok());
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(path, invalid),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderTE) {
  HeaderString name{"te"};
  HeaderString valid{"trailers"};
  HeaderString invalid{"chunked"};
  auto uhv = createH2(empty_config);

  EXPECT_TRUE(uhv->validateRequestHeaderEntry(name, valid).ok());
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(name, invalid),
                             "uhv.http2.invalid_te");
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMethodAllowAllMethods) {
  HeaderString method{":method"};
  HeaderString valid{"GET"};
  HeaderString invalid{"CUSTOM-METHOD"};
  auto uhv = createH2(empty_config);

  EXPECT_TRUE(uhv->validateRequestHeaderEntry(method, valid).ok());
  EXPECT_TRUE(uhv->validateRequestHeaderEntry(method, invalid).ok());
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMethodRestrictMethods) {
  HeaderString method{":method"};
  HeaderString valid{"GET"};
  HeaderString invalid{"CUSTOM-METHOD"};
  auto uhv = createH2(restrict_http_methods_config);

  EXPECT_TRUE(uhv->validateRequestHeaderEntry(method, valid).ok());
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(method, invalid),
                             UhvResponseCodeDetail::get().InvalidMethod);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderContentLength) {
  HeaderString content_length{"content-length"};
  HeaderString valid{"100"};
  HeaderString invalid{"10a2"};
  auto uhv = createH2(empty_config);

  EXPECT_TRUE(uhv->validateRequestHeaderEntry(content_length, valid).ok());
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(content_length, invalid),
                             UhvResponseCodeDetail::get().InvalidContentLength);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderSchemeValid) {
  HeaderString scheme{":scheme"};
  HeaderString valid{"https"};
  HeaderString valid_mixed_case{"hTtPs"};
  auto uhv = createH2(empty_config);

  EXPECT_TRUE(uhv->validateRequestHeaderEntry(scheme, valid).ok());
  EXPECT_TRUE(uhv->validateRequestHeaderEntry(scheme, valid_mixed_case).ok());
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderSchemeInvalidChar) {
  HeaderString scheme{":scheme"};
  HeaderString invalid{"http_ssh"};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(scheme, invalid),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderSchemeInvalidStartChar) {
  HeaderString scheme{":scheme"};
  HeaderString invalid_first_char{"+http"};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(scheme, invalid_first_char),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderGenericValid) {
  HeaderString valid_name{"x-foo"};
  HeaderString valid_value{"bar"};
  auto uhv = createH2(empty_config);

  EXPECT_TRUE(uhv->validateRequestHeaderEntry(valid_name, valid_value).ok());
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderGenericInvalidName) {
  HeaderString invalid_name{""};
  HeaderString valid_value{"bar"};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(invalid_name, valid_value),
                             UhvResponseCodeDetail::get().EmptyHeaderName);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderGenericInvalidValue) {
  HeaderString valid_name{"x-foo"};
  HeaderString invalid_value;
  auto uhv = createH2(empty_config);

  setHeaderStringUnvalidated(invalid_value, "hello\nworld");

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(valid_name, invalid_value),
                             UhvResponseCodeDetail::get().InvalidCharacters);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderAllowUnderscores) {
  HeaderString name{"x_foo"};
  HeaderString value{"bar"};
  auto uhv = createH2(empty_config);

  EXPECT_TRUE(uhv->validateRequestHeaderEntry(name, value).ok());
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderRejectUnderscores) {
  HeaderString name{"x_foo"};
  HeaderString value{"bar"};
  auto uhv = createH2(reject_headers_with_underscores_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(name, value),
                             UhvResponseCodeDetail::get().InvalidUnderscore);
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderStatus) {
  HeaderString status{":status"};
  HeaderString valid{"200"};
  HeaderString invalid{"1024"};
  auto uhv = createH2(empty_config);

  EXPECT_TRUE(uhv->validateResponseHeaderEntry(status, valid).ok());
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderEntry(status, invalid),
                             UhvResponseCodeDetail::get().InvalidStatus);
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderGenericValid) {
  HeaderString valid_name{"x-foo"};
  HeaderString valid_value{"bar"};
  auto uhv = createH2(empty_config);

  EXPECT_TRUE(uhv->validateResponseHeaderEntry(valid_name, valid_value).ok());
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderGenericInvalidName) {
  HeaderString invalid_name{""};
  HeaderString valid_value{"bar"};
  auto uhv = createH2(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderEntry(invalid_name, valid_value),
                             UhvResponseCodeDetail::get().EmptyHeaderName);
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderGenericInvalidValue) {
  HeaderString valid_name{"x-foo"};
  HeaderString invalid_value;
  auto uhv = createH2(empty_config);

  setHeaderStringUnvalidated(invalid_value, "hello\nworld");

  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderEntry(valid_name, invalid_value),
                             UhvResponseCodeDetail::get().InvalidCharacters);
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderAllowUnderscores) {
  HeaderString name{"x_foo"};
  HeaderString value{"bar"};
  auto uhv = createH2(empty_config);

  EXPECT_TRUE(uhv->validateResponseHeaderEntry(name, value).ok());
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderRejectUnderscores) {
  HeaderString name{"x_foo"};
  HeaderString value{"bar"};
  auto uhv = createH2(reject_headers_with_underscores_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderEntry(name, value),
                             UhvResponseCodeDetail::get().InvalidUnderscore);
}

TEST_F(Http2HeaderValidatorTest, ValidateLowercaseHeader) {
  HeaderString valid{"x-foo"};
  HeaderString invalid{"X-Foo"};
  auto uhv = createH2(empty_config);

  EXPECT_TRUE(uhv->validateGenericHeaderName(valid).ok());
  EXPECT_REJECT_WITH_DETAILS(uhv->validateGenericHeaderName(invalid),
                             "uhv.http2.invalid_header_name");
}

} // namespace
} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
