#include "source/extensions/http/header_validators/envoy_default/http1_header_validator.h"

#include "test/extensions/http/header_validators/envoy_default/header_validator_test.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {
namespace {

using ::Envoy::Http::HeaderString;
using ::Envoy::Http::Protocol;

class Http1HeaderValidatorTest : public HeaderValidatorTest {
protected:
  Http1HeaderValidatorPtr createH1(absl::string_view config_yaml) {
    envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
        typed_config;
    TestUtility::loadFromYaml(std::string(config_yaml), typed_config);

    return std::make_unique<Http1HeaderValidator>(typed_config, Protocol::Http11, stream_info_);
  }
};

TEST_F(Http1HeaderValidatorTest, ValidateTransferEncoding) {
  HeaderString valid{"chunked"};
  HeaderString valid_mixed_case{"ChuNKeD"};
  HeaderString invalid{"gzip"};
  auto uhv = createH1(empty_config);

  EXPECT_ACCEPT(uhv->validateTransferEncodingHeader(valid));
  EXPECT_ACCEPT(uhv->validateTransferEncodingHeader(valid_mixed_case));

  EXPECT_REJECT_WITH_DETAILS(uhv->validateTransferEncodingHeader(invalid),
                             "uhv.http1.invalid_transfer_encoding");
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderEntryEmpty) {
  HeaderString empty{""};
  HeaderString value{"foo"};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(empty, value),
                             UhvResponseCodeDetail::get().EmptyHeaderName);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderEntryMethod) {
  HeaderString name{":method"};
  HeaderString valid{"GET"};
  HeaderString valid_custom{"CUSTOM-METHOD"};
  auto uhv = createH1(empty_config);
  EXPECT_ACCEPT(uhv->validateRequestHeaderEntry(name, valid));
  EXPECT_ACCEPT(uhv->validateRequestHeaderEntry(name, valid_custom));
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderEntryAuthority) {
  HeaderString name{":authority"};
  HeaderString valid{"envoy.com"};
  HeaderString invalid{"user:pass@envoy.com"};
  auto uhv = createH1(empty_config);
  EXPECT_ACCEPT(uhv->validateRequestHeaderEntry(name, valid));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(name, invalid),
                             UhvResponseCodeDetail::get().InvalidHostDeprecatedUserInfo);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderEntrySchemeValid) {
  HeaderString scheme{":scheme"};
  HeaderString valid{"https"};
  HeaderString invalid{"http_ssh"};
  auto uhv = createH1(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderEntry(scheme, valid));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(scheme, invalid),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderEntryPath) {
  HeaderString name{":path"};
  HeaderString valid{"/"};
  HeaderString invalid{"/ bad path"};
  auto uhv = createH1(empty_config);
  EXPECT_ACCEPT(uhv->validateRequestHeaderEntry(name, valid));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(name, invalid),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderEntryTransferEncoding) {
  HeaderString name{"transfer-encoding"};
  HeaderString valid{"chunked"};
  HeaderString invalid{"{deflate}"};
  auto uhv = createH1(empty_config);
  EXPECT_ACCEPT(uhv->validateRequestHeaderEntry(name, valid));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(name, invalid),
                             "uhv.http1.invalid_transfer_encoding");
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestEntryHeaderContentLength) {
  HeaderString content_length{"content-length"};
  HeaderString valid{"100"};
  HeaderString invalid{"10a2"};
  auto uhv = createH1(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderEntry(content_length, valid));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(content_length, invalid),
                             UhvResponseCodeDetail::get().InvalidContentLength);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderEntryGeneric) {
  HeaderString valid_name{"x-foo"};
  HeaderString invalid_name{"foo oo"};
  HeaderString valid_value{"bar"};

  HeaderString invalid_value{};
  setHeaderStringUnvalidated(invalid_value, "hello\nworld");

  auto uhv = createH1(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderEntry(valid_name, valid_value));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(invalid_name, valid_value),
                             UhvResponseCodeDetail::get().InvalidNameCharacters);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderEntry(valid_name, invalid_value),
                             UhvResponseCodeDetail::get().InvalidValueCharacters);
}

TEST_F(Http1HeaderValidatorTest, ValidateResponseHeaderEntryEmpty) {
  HeaderString name{""};
  HeaderString valid{"chunked"};
  auto uhv = createH1(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderEntry(name, valid),
                             UhvResponseCodeDetail::get().EmptyHeaderName);
}

TEST_F(Http1HeaderValidatorTest, ValidateResponseHeaderEntryStatus) {
  HeaderString name{":status"};
  HeaderString valid{"200"};
  HeaderString invalid{"1024"};
  auto uhv = createH1(empty_config);
  EXPECT_ACCEPT(uhv->validateResponseHeaderEntry(name, valid));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderEntry(name, invalid),
                             UhvResponseCodeDetail::get().InvalidStatus);
}

TEST_F(Http1HeaderValidatorTest, ValidateResponseHeaderEntryGeneric) {
  HeaderString valid_name{"x-foo"};
  HeaderString invalid_name{"foo oo"};
  HeaderString valid_value{"bar"};

  HeaderString invalid_value{};
  setHeaderStringUnvalidated(invalid_value, "hello\nworld");

  auto uhv = createH1(empty_config);

  EXPECT_ACCEPT(uhv->validateResponseHeaderEntry(valid_name, valid_value));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderEntry(invalid_name, valid_value),
                             UhvResponseCodeDetail::get().InvalidNameCharacters);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderEntry(valid_name, invalid_value),
                             UhvResponseCodeDetail::get().InvalidValueCharacters);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapAllowed) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/"},
                                                  {":authority", "envoy.com"},
                                                  {"x-foo", "bar"}};
  auto uhv = createH1(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapAllowedHostAlias) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/"},
                                                  {"host", "envoy.com"},
                                                  {"x-foo", "bar"}};
  auto uhv = createH1(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapMissingPath) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"}, {":method", "GET"}, {":authority", "envoy.com"}, {"x-foo", "bar"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapMissingMethod) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"}, {":path", "/"}, {":authority", "envoy.com"}, {"x-foo", "bar"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidMethod);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapMissingHost) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"}, {":method", "GET"}, {":path", "/"}, {"x-foo", "bar"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapStarPathAccept) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "OPTIONS"},
                                                  {":path", "*"},
                                                  {":authority", "envoy.com"},
                                                  {"x-foo", "bar"}};
  auto uhv = createH1(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapStarPathReject) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "*"},
                                                  {":authority", "envoy.com"},
                                                  {"x-foo", "bar"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapTransferEncodingValid) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/"},
                                                  {":authority", "envoy.com"},
                                                  {"transfer-encoding", "chunked"}};
  auto uhv = createH1(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
}

TEST_F(Http1HeaderValidatorTest, ValidateConnectRegName) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "CONNECT"},
                                                  {":authority", "www.envoy.com:443"},
                                                  {"x-foo", "bar"}};
  auto uhv = createH1(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
}

TEST_F(Http1HeaderValidatorTest, ValidateConnectIPv6) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "CONNECT"},
                                                  {":authority", "[2001:8080]:9000"},
                                                  {"x-foo", "bar"}};
  auto uhv = createH1(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
}

TEST_F(Http1HeaderValidatorTest, ValidateConnectInvalidUserInfo) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "CONNECT"},
                                                  {":authority", "user:pass@envoy.com"},
                                                  {"x-foo", "bar"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidHostDeprecatedUserInfo);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapTransferEncodingConnect) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "CONNECT"},
                                                  {":authority", "envoy.com"},
                                                  {"transfer-encoding", "chunked"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             "uhv.http1.transfer_encoding_not_allowed");
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapTransferEncodingContentLengthReject) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/"},
                                                  {":authority", "envoy.com"},
                                                  {"transfer-encoding", "chunked"},
                                                  {"content-length", "10"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             "http1.content_length_and_chunked_not_allowed");
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapTransferEncodingContentLengthAllow) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/"},
                                                  {":authority", "envoy.com"},
                                                  {"transfer-encoding", "chunked"},
                                                  {"content-length", "10"}};
  auto uhv = createH1(allow_chunked_length_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
  EXPECT_EQ(headers.ContentLength(), nullptr);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapContentLengthNoTransferEncoding) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/"},
                                                  {":authority", "envoy.com"},
                                                  {"content-length", "10"}};
  auto uhv = createH1(allow_chunked_length_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
  EXPECT_EQ(headers.getContentLengthValue(), "10");
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapContentLengthConnectReject) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "CONNECT"},
                                                  {":authority", "envoy.com:80"},
                                                  {"content-length", "10"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             "uhv.http1.content_length_not_allowed");
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapConnectRegNameMissingPort) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"}, {":method", "CONNECT"}, {":authority", "envoy.com"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapConnectIPv6MissingPort) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"}, {":method", "CONNECT"}, {":authority", "[2001:8080]"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapContentLength0ConnectAccept) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "CONNECT"},
                                                  {":authority", "envoy.com:80"},
                                                  {"content-length", "0"}};
  auto uhv = createH1(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
  EXPECT_EQ(headers.ContentLength(), nullptr);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapConnectWithPath) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"}, {":method", "CONNECT"}, {":authority", "envoy.com:80"}, {":path", "/"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapExtraPseudo) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "CONNECT"},
                                                  {":authority", "envoy.com:80"},
                                                  {":status", "200"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidPseudoHeader);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapEmptyGeneric) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"}, {":method", "CONNECT"}, {":authority", "envoy.com:80"}, {"", "bar"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().EmptyHeaderName);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapInvalidGeneric) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "CONNECT"},
                                                  {":authority", "envoy.com:80"},
                                                  {"foo header", "bar"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidNameCharacters);
}

TEST_F(Http1HeaderValidatorTest, ValidateRequestHeaderMapDropUnderscoreHeaders) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/"},
                                                  {":authority", "envoy.com"},
                                                  {"x_foo", "bar"}};
  auto uhv = createH1(drop_headers_with_underscores_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaderMap(headers));
  EXPECT_EQ(
      headers,
      ::Envoy::Http::TestRequestHeaderMapImpl(
          {{":scheme", "https"}, {":method", "GET"}, {":path", "/"}, {":authority", "envoy.com"}}));
}

TEST_F(Http1HeaderValidatorTest, ValidateResponseHeaderMapValid) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{
      {":status", "200"}, {"x-foo", "bar"}, {"transfer-encoding", "chunked"}};
  auto uhv = createH1(empty_config);

  EXPECT_ACCEPT(uhv->validateResponseHeaderMap(headers));
}

TEST_F(Http1HeaderValidatorTest, ValidateResponseHeaderMapMissingStatus) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{{"x-foo", "bar"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidStatus);
}

TEST_F(Http1HeaderValidatorTest, ValidateResponseHeaderMapInvalidStatus) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "bar"}, {"x-foo", "bar"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidStatus);
}

TEST_F(Http1HeaderValidatorTest, ValidateResponseHeaderMapExtraPseudoHeader) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {":foo", "bar"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderMap(headers),
                             UhvResponseCodeDetail::get().InvalidPseudoHeader);
}

TEST_F(Http1HeaderValidatorTest, ValidateResponseHeaderMapEmptyGenericName) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"", "bar"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderMap(headers),
                             UhvResponseCodeDetail::get().EmptyHeaderName);
}

TEST_F(Http1HeaderValidatorTest, ValidateResponseHeaderMapInvaidTransferEncodingStatus100) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "100"},
                                                   {"transfer-encoding", "chunked"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderMap(headers),
                             "uhv.http1.transfer_encoding_not_allowed");
}

TEST_F(Http1HeaderValidatorTest, ValidateResponseHeaderMapInvaidTransferEncodingStatus204) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "204"},
                                                   {"transfer-encoding", "chunked"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderMap(headers),
                             "uhv.http1.transfer_encoding_not_allowed");
}

TEST_F(Http1HeaderValidatorTest, ValidateResponseHeaderMapInvaidTransferEncodingChars) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                                   {"transfer-encoding", "{chunked}"}};
  auto uhv = createH1(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaderMap(headers),
                             "uhv.http1.invalid_transfer_encoding");
}

TEST_F(Http1HeaderValidatorTest, ValidateResponseHeaderMapDropUnderscoreHeaders) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"x_foo", "bar"}};
  auto uhv = createH1(drop_headers_with_underscores_config);

  EXPECT_ACCEPT(uhv->validateResponseHeaderMap(headers));
  EXPECT_EQ(headers, ::Envoy::Http::TestResponseHeaderMapImpl({{":status", "200"}}));
}

} // namespace
} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
