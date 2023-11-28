#include "envoy/http/header_validator_errors.h"

#include "source/extensions/http/header_validators/envoy_default/character_tables.h"
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

using ::Envoy::Extensions::Http::HeaderValidators::EnvoyDefault::Http2HeaderValidator;
using ::Envoy::Http::HeaderString;
using ::Envoy::Http::Protocol;
using ::Envoy::Http::testCharInTable;
using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::Envoy::Http::TestRequestTrailerMapImpl;
using ::Envoy::Http::TestResponseHeaderMapImpl;
using ::Envoy::Http::UhvResponseCodeDetail;

class Http2HeaderValidatorTest : public HeaderValidatorUtils, public testing::Test {
protected:
  ::Envoy::Http::ServerHeaderValidatorPtr createH2ServerUhv(absl::string_view config_yaml) {
    envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
        typed_config;
    TestUtility::loadFromYaml(std::string(config_yaml), typed_config);

    ConfigOverrides overrides(scoped_runtime_.loader().snapshot());
    return std::make_unique<ServerHttp2HeaderValidator>(typed_config, protocol_, stats_, overrides);
  }

  ::Envoy::Http::ClientHeaderValidatorPtr createH2ClientUhv(absl::string_view config_yaml) {
    envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
        typed_config;
    TestUtility::loadFromYaml(std::string(config_yaml), typed_config);

    ConfigOverrides overrides(scoped_runtime_.loader().snapshot());
    return std::make_unique<ClientHttp2HeaderValidator>(typed_config, protocol_, stats_, overrides);
  }

  std::unique_ptr<Http2HeaderValidator> createH2BaseUhv(absl::string_view config_yaml) {
    envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
        typed_config;
    TestUtility::loadFromYaml(std::string(config_yaml), typed_config);

    ConfigOverrides overrides(scoped_runtime_.loader().snapshot());
    return std::make_unique<Http2HeaderValidator>(typed_config, protocol_, stats_, overrides);
  }

  TestRequestHeaderMapImpl makeGoodRequestHeaders() {
    return TestRequestHeaderMapImpl{
        {":scheme", "https"}, {":method", "GET"}, {":authority", "envoy.com"}, {":path", "/"}};
  }

  TestResponseHeaderMapImpl makeGoodResponseHeaders() {
    return TestResponseHeaderMapImpl{{":status", "200"}};
  }

  ::testing::NiceMock<Envoy::Http::MockHeaderValidatorStats> stats_;
  TestScopedRuntime scoped_runtime_;
  Protocol protocol_{Protocol::Http2};
};

TEST_F(Http2HeaderValidatorTest, GoodHeadersAccepted) {
  auto server_uhv = createH2ServerUhv(empty_config);
  auto client_uhv = createH2ClientUhv(empty_config);
  EXPECT_ACCEPT(server_uhv->validateRequestHeaders(makeGoodRequestHeaders()));
  EXPECT_ACCEPT(server_uhv->validateResponseHeaders(makeGoodResponseHeaders()));
  EXPECT_ACCEPT(client_uhv->validateRequestHeaders(makeGoodRequestHeaders()));
  EXPECT_ACCEPT(client_uhv->validateResponseHeaders(makeGoodResponseHeaders()));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapAllowed) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/"},
                                                  {":authority", "envoy.com"},
                                                  {"x-foo", "bar"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapMissingPath) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"}, {":method", "GET"}, {"x-foo", "bar"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapMissingMethod) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"}, {":path", "/"}, {"x-foo", "bar"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidMethod);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapMissingScheme) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":method", "GET"}, {":path", "/"}, {"x-foo", "bar"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapExtraPseudoHeader) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"}, {":method", "GET"}, {":path", "/"}, {":foo", "bar"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidPseudoHeader);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapNoAuthorityIsOk) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"}, {":method", "GET"}, {":path", "/"}, {"x-foo", "bar"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapConnect) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":method", "CONNECT"}, {":authority", "envoy.com"}, {"x-foo", "bar"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));

  // Check that UHV does not modify plain vanilla CONNECT method
  EXPECT_EQ(headers, ::Envoy::Http::TestRequestHeaderMapImpl(
                         {{":method", "CONNECT"}, {":authority", "envoy.com"}, {"x-foo", "bar"}}));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapConnectExtraPseudoHeader) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":method", "CONNECT"}, {":scheme", "https"}, {":authority", "envoy.com"}, {"x-foo", "bar"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapConnectMissingAuthority) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "CONNECT"}, {"x-foo", "bar"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapConnectWithPath) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":method", "CONNECT"}, {":authority", "envoy.com"}, {":path", "/bar"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapConnectWithScheme) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":method", "CONNECT"}, {":authority", "envoy.com"}, {":scheme", "https"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapOptionsAsterisk) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "OPTIONS"},
                                                  {":path", "*"},
                                                  {":authority", "envoy.com"},
                                                  {"x-foo", "bar"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapNotOptionsAsterisk) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "*"},
                                                  {":authority", "envoy.com"},
                                                  {"x-foo", "bar"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapInvalidAuthority) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/"},
                                                  {":authority", "user:pass@envoy.com"},
                                                  {"x-foo", "bar"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidHostDeprecatedUserInfo);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapEmptyGenericName) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/"},
                                                  {":authority", "envoy.com"},
                                                  {"", "bar"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers),
                             UhvResponseCodeDetail::get().EmptyHeaderName);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapUnderscoreHeadersAllowedByDefault) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "GET"},
                                                  {":path", "/"},
                                                  {":authority", "envoy.com"},
                                                  {"x_foo", "bar"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
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
  auto uhv = createH2ServerUhv(drop_headers_with_underscores_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  // Headers with underscores are dropped by the transform method
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
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
  auto uhv = createH2ServerUhv(reject_headers_with_underscores_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidUnderscore);
}

TEST_F(Http2HeaderValidatorTest, RequestExtendedConnect) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"},  {":method", "CONNECT"},      {":protocol", "websocket"},
      {":path", "/foo/bar"}, {":authority", "envoy.com"}, {"x-foo", "bar"}};
  auto uhv = createH2ServerUhv(empty_config);
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  // Extended CONNECT is transformed to H/1 upgrade
  EXPECT_EQ(headers.method(), "GET");
  EXPECT_EQ(headers.getUpgradeValue(), "websocket");
  EXPECT_EQ(headers.getConnectionValue(), "upgrade");
  EXPECT_EQ(headers.protocol(), "");
}

TEST_F(Http2HeaderValidatorTest, RequestExtendedConnectNoScheme) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "CONNECT"},
                                                  {":protocol", "websocket"},
                                                  {":path", "/foo/bar"},
                                                  {":authority", "envoy.com"}};
  auto uhv = createH2ServerUhv(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidScheme);
  // If validation failed, the extended CONNECT request should not be transformed to H/1 upgrade
  EXPECT_EQ(headers.method(), "CONNECT");
}

TEST_F(Http2HeaderValidatorTest, RequestExtendedConnectNoPath) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "CONNECT"},
                                                  {":protocol", "websocket"},
                                                  {":authority", "envoy.com"}};
  auto uhv = createH2ServerUhv(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http2HeaderValidatorTest, RequestExtendedConnectInvalidPath) {
  // Character with value 0x7F is invalid in URI path
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "CONNECT"},
                                                  {":protocol", "websocket"},
                                                  {":path", "/fo\x7fo/bar"},
                                                  {":authority", "envoy.com"}};
  auto uhv = createH2ServerUhv(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http2HeaderValidatorTest, RequestExtendedConnectPathNormalization) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "CONNECT"},
                                                  {":protocol", "websocket"},
                                                  {":path", "/./dir1/../dir2"},
                                                  {":authority", "envoy.com"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  EXPECT_EQ(headers.path(), "/dir2");
  // Expect transformation to H/1 upgrade as well
  EXPECT_EQ(headers.method(), "GET");
  EXPECT_EQ(headers.getUpgradeValue(), "websocket");
  EXPECT_EQ(headers.getConnectionValue(), "upgrade");
  EXPECT_EQ(headers.protocol(), "");
}

TEST_F(Http2HeaderValidatorTest, RequestExtendedConnectNoAuthorityIsOk) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                                  {":method", "CONNECT"},
                                                  {":path", "/foo/bar"},
                                                  {":protocol", "websocket"}};
  auto uhv = createH2ServerUhv(empty_config);
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
}

TEST_F(Http2HeaderValidatorTest, DownstreamUpgradeResponseTransformedToExtendedConnectResponse) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{
      {":status", "101"}, {"connection", "keep-alive, upgrade"}, {"upgrade", "websocket"}};
  auto uhv = createH2ServerUhv(empty_config);
  auto result = uhv->transformResponseHeaders(headers);
  EXPECT_ACCEPT(result.status);
  // Expect the extended CONNECT response
  EXPECT_EQ(result.new_headers->getStatusValue(), "200");
  EXPECT_EQ(result.new_headers->Upgrade(), nullptr);
  EXPECT_EQ(result.new_headers->Connection(), nullptr);
  // New headers should be valid H/2 response headers
  EXPECT_ACCEPT(uhv->validateResponseHeaders(*result.new_headers));
}

TEST_F(Http2HeaderValidatorTest, UpstreamUpgradeRequestTransformedToExtendedConnect) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"},        {":method", "GET"},       {":path", "/./dir1/../dir2"},
      {":authority", "envoy.com"}, {"upgrade", "websocket"}, {"connection", "upgrade,keep-alive"}};
  auto uhv = createH2ClientUhv(empty_config);
  auto result = uhv->transformRequestHeaders(headers);
  EXPECT_TRUE(result.status.ok());
  // Expect the extended CONNECT request
  EXPECT_EQ(result.new_headers->method(), "CONNECT");
  EXPECT_EQ(result.new_headers->protocol(), "websocket");
  EXPECT_EQ(result.new_headers->Upgrade(), nullptr);
  EXPECT_EQ(result.new_headers->Connection(), nullptr);
  // No path normalization is expected at the client codec
  EXPECT_EQ(result.new_headers->path(), "/./dir1/../dir2");
  EXPECT_EQ(result.new_headers->host(), "envoy.com");
  EXPECT_EQ(result.new_headers->getSchemeValue(), "https");

  // New headers must be valid H/2 extended CONNECT headers
  EXPECT_ACCEPT(uhv->validateRequestHeaders(*result.new_headers));
}

TEST_F(Http2HeaderValidatorTest, UpstreamExtendedConnectResponseTransformedToUpgradeResponse) {
  // The response transformation is expected when the request was H/1 upgrade
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"},        {":method", "GET"},       {":path", "/./dir1/../dir2"},
      {":authority", "envoy.com"}, {"upgrade", "websocket"}, {"connection", "upgrade,keep-alive"}};
  auto uhv = createH2ClientUhv(empty_config);
  auto result = uhv->transformRequestHeaders(headers);
  EXPECT_TRUE(result.status.ok());
  // Make sure transformation produced valid H/2 extended CONNECT
  EXPECT_ACCEPT(uhv->validateRequestHeaders(*result.new_headers));

  ::Envoy::Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_ACCEPT(uhv->validateResponseHeaders(response_headers));
  EXPECT_ACCEPT(uhv->transformResponseHeaders(response_headers));
  // Expect the upgrade response
  EXPECT_EQ(response_headers.getStatusValue(), "101");
  EXPECT_EQ(response_headers.getUpgradeValue(), "websocket");
  EXPECT_EQ(response_headers.getConnectionValue(), "upgrade");
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderMapValid) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"x-foo", "bar"}};
  auto uhv = createH2ClientUhv(empty_config);

  EXPECT_ACCEPT(uhv->validateResponseHeaders(headers));
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderMapMissingStatus) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{{"x-foo", "bar"}};
  auto uhv = createH2ClientUhv(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidStatus);
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderMapExtraPseudoHeader) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{
      {":status", "200"}, {":foo", "bar"}, {"x-foo", "bar"}};
  auto uhv = createH2ClientUhv(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidPseudoHeader);
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderMapInvalidStatus) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "1024"}, {"x-foo", "bar"}};
  auto uhv = createH2ClientUhv(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidStatus);
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderMapEmptyGenericName) {
  ::Envoy::Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"", "bar"}};
  auto uhv = createH2ClientUhv(empty_config);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaders(headers),
                             UhvResponseCodeDetail::get().EmptyHeaderName);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestTrailersAllowUnderscoreHeadersByDefault) {
  TestRequestTrailerMapImpl trailers{{"trailer1", "value1"}, {"x_foo", "bar"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestTrailers(trailers));
  EXPECT_ACCEPT(uhv->transformRequestTrailers(trailers));
  EXPECT_EQ(trailers, TestRequestTrailerMapImpl({{"trailer1", "value1"}, {"x_foo", "bar"}}));
}

TEST_F(Http2HeaderValidatorTest, ValidateGenericHeaderNameRejectConnectionHeaders) {
  std::string connection_headers[] = {"transfer-encoding", "connection", "keep-alive", "upgrade",
                                      "proxy-connection"};
  auto uhv = createH2ServerUhv(empty_config);

  for (auto& header : connection_headers) {
    TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
    request_headers.addCopy(header, "some-value");
    EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(request_headers),
                               "uhv.http2.connection_header_rejected");
  }
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderPath) {
  auto uhv = createH2ServerUhv(empty_config);
  TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
  request_headers.setPath("/ bad\x7Fpath");

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(request_headers),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderTETrailersAllowed) {
  auto uhv = createH2ServerUhv(empty_config);
  TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
  request_headers.addCopy("te", "trailers");

  EXPECT_ACCEPT(uhv->validateRequestHeaders(request_headers));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderInvalidTERejected) {
  auto uhv = createH2ServerUhv(empty_config);
  TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
  request_headers.addCopy("te", "chunked");

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(request_headers), "uhv.http2.invalid_te");
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderCustomMethod) {
  auto uhv = createH2ServerUhv(empty_config);
  TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
  request_headers.setMethod("CUSTOM-METHOD");

  EXPECT_ACCEPT(uhv->validateRequestHeaders(request_headers));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderContentLength) {
  auto uhv = createH2ServerUhv(empty_config);
  TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
  request_headers.setContentLength("100");
  EXPECT_ACCEPT(uhv->validateRequestHeaders(request_headers));

  request_headers.setContentLength("10a2");
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(request_headers),
                             UhvResponseCodeDetail::get().InvalidContentLength);
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderContentLength) {
  auto uhv = createH2ClientUhv(empty_config);
  TestResponseHeaderMapImpl response_headers = makeGoodResponseHeaders();
  response_headers.setContentLength("100");
  EXPECT_ACCEPT(uhv->validateResponseHeaders(response_headers));

  response_headers.setContentLength("10a2");
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaders(response_headers),
                             UhvResponseCodeDetail::get().InvalidContentLength);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderInvalidScheme) {
  auto uhv = createH2ServerUhv(empty_config);
  TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
  request_headers.setScheme("http_ssh");
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(request_headers),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderInvalidProtocol) {
  auto uhv = createH2ServerUhv(empty_config);
  TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                   {":method", "CONNECT"},
                                   {":protocol", "something \x7F bad"},
                                   {":path", "/foo/bar"},
                                   {":authority", "envoy.com"}};

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidValueCharacters);
}

TEST_F(Http2HeaderValidatorTest, InvalidRequestHeaderNameRejected) {
  auto uhv = createH2ServerUhv(empty_config);
  TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
  // This header name is valid
  request_headers.addCopy("x-foo", "bar");
  EXPECT_ACCEPT(uhv->validateRequestHeaders(request_headers));

  // Reject invalid name
  request_headers.addCopy("foo oo", "bar");
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(request_headers),
                             UhvResponseCodeDetail::get().InvalidNameCharacters);
}

TEST_F(Http2HeaderValidatorTest, InvalidRequestHeaderValueRejected) {
  auto uhv = createH2ServerUhv(empty_config);
  TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
  HeaderString invalid_value{};
  setHeaderStringUnvalidated(invalid_value, "hello\nworld");
  request_headers.addViaMove(HeaderString(absl::string_view("x-foo")), std::move(invalid_value));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(request_headers),
                             UhvResponseCodeDetail::get().InvalidValueCharacters);
}

TEST_F(Http2HeaderValidatorTest, InvalidResponseHeaderNameRejected) {
  auto uhv = createH2ClientUhv(empty_config);
  TestResponseHeaderMapImpl response_headers = makeGoodResponseHeaders();
  // This header name is valid
  response_headers.addCopy("x-foo", "bar");
  EXPECT_ACCEPT(uhv->validateResponseHeaders(response_headers));

  // Reject invalid name
  response_headers.addCopy("foo oo", "bar");
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaders(response_headers),
                             UhvResponseCodeDetail::get().InvalidNameCharacters);
}

TEST_F(Http2HeaderValidatorTest, InvalidResponseHeaderValueRejected) {
  auto uhv = createH2ClientUhv(empty_config);
  TestResponseHeaderMapImpl response_headers = makeGoodResponseHeaders();
  HeaderString invalid_value{};
  setHeaderStringUnvalidated(invalid_value, "hello\nworld");
  response_headers.addViaMove(HeaderString(absl::string_view("x-foo")), std::move(invalid_value));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaders(response_headers),
                             UhvResponseCodeDetail::get().InvalidValueCharacters);
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseHeaderInvalidStatusRejected) {
  auto uhv = createH2ClientUhv(empty_config);
  TestResponseHeaderMapImpl response_headers = makeGoodResponseHeaders();
  response_headers.setStatus("1024");

  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaders(response_headers),
                             UhvResponseCodeDetail::get().InvalidStatus);
}

TEST_F(Http2HeaderValidatorTest, ValidateUppercaseRequestHeaderRejected) {
  auto uhv = createH2ServerUhv(empty_config);

  HeaderString invalid_name_uppercase;
  setHeaderStringUnvalidated(invalid_name_uppercase, "X-Foo");
  TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
  request_headers.addViaMove(std::move(invalid_name_uppercase),
                             HeaderString(absl::string_view("some value")));

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestHeaders(request_headers),
                             UhvResponseCodeDetail::get().InvalidNameCharacters);
}

TEST_F(Http2HeaderValidatorTest, ValidateUppercaseResponseHeaderRejected) {
  auto uhv = createH2ClientUhv(empty_config);

  HeaderString invalid_name_uppercase;
  setHeaderStringUnvalidated(invalid_name_uppercase, "X-Foo");
  TestResponseHeaderMapImpl headers = makeGoodResponseHeaders();
  headers.addViaMove(std::move(invalid_name_uppercase),
                     HeaderString(absl::string_view("some value")));

  EXPECT_REJECT_WITH_DETAILS(uhv->validateResponseHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidNameCharacters);
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestGenericHeaderName) {
  auto uhv = createH2ServerUhv(empty_config);
  std::string name{"aaaaa"};
  for (int i = 0; i < 0xff; ++i) {
    char c = static_cast<char>(i);
    HeaderString header_string{"x"};
    name[2] = c;

    setHeaderStringUnvalidated(header_string, name);
    TestRequestHeaderMapImpl request_headers = makeGoodRequestHeaders();
    request_headers.addViaMove(std::move(header_string),
                               HeaderString(absl::string_view("some value")));

    auto result = uhv->validateRequestHeaders(request_headers);
    if (testCharInTable(::Envoy::Http::kGenericHeaderNameCharTable, c) && (c < 'A' || c > 'Z')) {
      EXPECT_ACCEPT(result);
    } else if (c != '_') {
      EXPECT_REJECT_WITH_DETAILS(result, UhvResponseCodeDetail::get().InvalidNameCharacters);
    } else {
      EXPECT_REJECT_WITH_DETAILS(result, UhvResponseCodeDetail::get().InvalidUnderscore);
    }
  }
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseGenericHeaderName) {
  auto uhv = createH2ClientUhv(empty_config);
  std::string name{"aaaaa"};
  for (int i = 0; i < 0xff; ++i) {
    char c = static_cast<char>(i);
    HeaderString header_string{"x"};
    name[2] = c;

    setHeaderStringUnvalidated(header_string, name);
    TestResponseHeaderMapImpl headers = makeGoodResponseHeaders();
    headers.addViaMove(std::move(header_string), HeaderString(absl::string_view("some value")));

    auto result = uhv->validateResponseHeaders(headers);
    if (testCharInTable(::Envoy::Http::kGenericHeaderNameCharTable, c) && (c < 'A' || c > 'Z')) {
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
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_TRUE(uhv->validateRequestHeaders(headers).ok());
  EXPECT_TRUE(uhv->transformRequestHeaders(headers).ok());
  EXPECT_EQ(headers.path(), "/dir2");
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestHeaderMapRejectPath) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"}, {":method", "GET"}, {":path", "/.."}, {":authority", "envoy.com"}};
  auto uhv = createH2ServerUhv(empty_config);
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  // Path normalization should fail due to /.. path
  EXPECT_REJECT_WITH_DETAILS(uhv->transformRequestHeaders(headers),
                             UhvResponseCodeDetail::get().InvalidUrl);
  ;
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestTrailerMap) {
  auto uhv = createH2ServerUhv(empty_config);
  ::Envoy::Http::TestRequestTrailerMapImpl request_trailer_map{{"trailer1", "value1"},
                                                               {"trailer2", "values"}};
  EXPECT_TRUE(uhv->validateRequestTrailers(request_trailer_map));
}

TEST_F(Http2HeaderValidatorTest, ValidateInvalidRequestTrailerMap) {
  auto uhv = createH2ServerUhv(empty_config);
  // H/2 trailers must not contain pseudo headers
  ::Envoy::Http::TestRequestTrailerMapImpl request_trailer_map{{":path", "value1"},
                                                               {"trailer2", "values"}};
  auto result = uhv->validateRequestTrailers(request_trailer_map);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.details(), "uhv.invalid_name_characters");
}

TEST_F(Http2HeaderValidatorTest, ValidateInvalidRequestTrailerMapClientCodec) {
  auto uhv = createH2ClientUhv(empty_config);
  // H/2 trailers must not contain pseudo headers
  ::Envoy::Http::TestRequestTrailerMapImpl request_trailer_map{{":path", "value1"},
                                                               {"trailer2", "values"}};
  auto result = uhv->validateRequestTrailers(request_trailer_map);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.details(), "uhv.invalid_name_characters");
}

TEST_F(Http2HeaderValidatorTest, ValidateInvalidValueRequestTrailerMap) {
  auto uhv = createH2ServerUhv(empty_config);
  ::Envoy::Http::TestRequestTrailerMapImpl request_trailer_map{{"trailer1", "value1"},
                                                               {"trailer2", "values"}};
  ::Envoy::Http::HeaderString invalid_value;
  // \n must not be present in header values
  invalid_value.setCopyUnvalidatedForTestOnly("invalid\nvalue");
  request_trailer_map.addViaMove(::Envoy::Http::HeaderString("trailer3"), std::move(invalid_value));
  auto result = uhv->validateRequestTrailers(request_trailer_map);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.details(), "uhv.invalid_value_characters");
}

TEST_F(Http2HeaderValidatorTest, UnderscoreHeadersAllowedInRequestTrailersByDefault) {
  ::Envoy::Http::TestRequestTrailerMapImpl trailers{{"trailer1", "value1"}, {"x_foo", "bar"}};
  auto uhv = createH2ServerUhv(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestTrailers(trailers));
  EXPECT_ACCEPT(uhv->transformRequestTrailers(trailers));
  EXPECT_EQ(trailers,
            ::Envoy::Http::TestRequestTrailerMapImpl({{"trailer1", "value1"}, {"x_foo", "bar"}}));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestTrailersDropUnderscoreHeaders) {
  ::Envoy::Http::TestRequestTrailerMapImpl trailers{{"trailer1", "value1"}, {"x_foo", "bar"}};
  auto uhv = createH2ServerUhv(drop_headers_with_underscores_config);

  EXPECT_ACCEPT(uhv->validateRequestTrailers(trailers));
  EXPECT_ACCEPT(uhv->transformRequestTrailers(trailers));
  EXPECT_EQ(trailers, ::Envoy::Http::TestRequestTrailerMapImpl({{"trailer1", "value1"}}));
}

TEST_F(Http2HeaderValidatorTest, ValidateRequestTrailersRejectUnderscoreHeaders) {
  ::Envoy::Http::TestRequestTrailerMapImpl trailers{{"trailer1", "value1"}, {"x_foo", "bar"}};
  auto uhv = createH2ServerUhv(reject_headers_with_underscores_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateRequestTrailers(trailers),
                             UhvResponseCodeDetail::get().InvalidUnderscore);
}

TEST_F(Http2HeaderValidatorTest, ValidateResponseTrailerMap) {
  auto uhv = createH2ClientUhv(empty_config);
  ::Envoy::Http::TestResponseTrailerMapImpl response_trailer_map{{"trailer1", "value1"}};
  EXPECT_TRUE(uhv->validateResponseTrailers(response_trailer_map).ok());
}

TEST_F(Http2HeaderValidatorTest, ValidateInvalidResponseTrailerMap) {
  auto uhv = createH2ClientUhv(empty_config);
  // H/2 trailers must not contain pseudo headers
  ::Envoy::Http::TestResponseTrailerMapImpl response_trailer_map{{":status", "200"},
                                                                 {"trailer1", "value1"}};
  auto result = uhv->validateResponseTrailers(response_trailer_map);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.details(), "uhv.invalid_name_characters");
}

TEST_F(Http2HeaderValidatorTest, ValidateInvalidValueResponseTrailerMap) {
  auto uhv = createH2ClientUhv(empty_config);
  // The DEL (0x7F) character is illegal in header values
  ::Envoy::Http::TestResponseTrailerMapImpl response_trailer_map{{"trailer0", "abcd\x7F\\ef"},
                                                                 {"trailer1", "value1"}};
  auto result = uhv->validateResponseTrailers(response_trailer_map);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.details(), "uhv.invalid_value_characters");
}

TEST_F(Http2HeaderValidatorTest, ValidateInvalidValueResponseTrailerMapServerCodec) {
  auto uhv = createH2ServerUhv(empty_config);
  // The DEL (0x7F) character is illegal in header values
  ::Envoy::Http::TestResponseTrailerMapImpl response_trailer_map{{"trailer0", "abcd\x7F\\ef"},
                                                                 {"trailer1", "value1"}};
  auto result = uhv->validateResponseTrailers(response_trailer_map);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.details(), "uhv.invalid_value_characters");
}

namespace {
std::string generateExtendedAsciiString() {
  std::string extended_ascii_string;
  extended_ascii_string.reserve(0x80);
  for (uint32_t ascii = 0x80; ascii <= 0xff; ++ascii) {
    extended_ascii_string.push_back(static_cast<char>(ascii));
  }
  return extended_ascii_string;
}

std::string generatePercentEncodedExtendedAscii() {
  std::string encoded_extended_ascii_string;
  encoded_extended_ascii_string.reserve(0x80 * 3);
  for (uint32_t ascii = 0x80; ascii <= 0xff; ++ascii) {
    encoded_extended_ascii_string.append(fmt::format("%{:02X}", static_cast<unsigned char>(ascii)));
  }
  return encoded_extended_ascii_string;
}

// H/3 also allows "<>[]^`{}\| space, TAB in :path
static const std::string AdditionallyAllowedCharactersHttp3 = "\t \"<>[]^`{}\\|";

// H/2 in addition to H/3 also allows extended ASCII in :path
static const std::string AdditionallyAllowedCharactersHttp2 =
    absl::StrCat(AdditionallyAllowedCharactersHttp3, generateExtendedAsciiString());
} // namespace

// Validate that H/2 UHV allows additional characters "<>[]^`{}\| space TAB and extended
// ASCII and encodes them when path normalization is enabled.
TEST_F(Http2HeaderValidatorTest, AdditionalCharactersInPathAllowedHttp2) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "true"}});
  auto uhv = createH2ServerUhv(fragment_in_path_allowed);
  validateAllCharactersInUrlPath(*uhv, "/path/with/additional/characters",
                                 absl::StrCat("?#", AdditionallyAllowedCharactersHttp2));

  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"},
      {":authority", "envoy.com"},
      {":method", "GET"},
      {":path", absl::StrCat("/path/with", AdditionallyAllowedCharactersHttp2)}};
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  // Note that \ is translated to / and [] remain unencoded
  EXPECT_EQ(headers.path(), absl::StrCat("/path/with%09%20%22%3C%3E[]%5E%60%7B%7D/%7C",
                                         generatePercentEncodedExtendedAscii()));
}

// Validate that H/3 UHV allows additional characters "<>[]^`{}\| space TAB
// and encodes them when path normalization is enabled.
TEST_F(Http2HeaderValidatorTest, AdditionalCharactersInPathAllowedHttp3) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "true"}});
  protocol_ = Protocol::Http3;
  auto uhv = createH2ServerUhv(fragment_in_path_allowed);
  validateAllCharactersInUrlPath(*uhv, "/path/with/additional/characters",
                                 absl::StrCat("?#", AdditionallyAllowedCharactersHttp3));

  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"},
      {":authority", "envoy.com"},
      {":method", "GET"},
      {":path", absl::StrCat("/path/with", AdditionallyAllowedCharactersHttp3)}};
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  // Note that \ is translated to / and [] remain unencoded
  EXPECT_EQ(headers.path(), "/path/with%09%20%22%3C%3E[]%5E%60%7B%7D/%7C");
}

// Validate that without path normalization additional characters remain untouched
TEST_F(Http2HeaderValidatorTest, AdditionalCharactersInPathAllowedWithoutPathNormalizationHttp2) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "true"}});
  auto uhv = createH2ServerUhv(no_path_normalization);

  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"},
      {":authority", "envoy.com"},
      {":method", "GET"},
      {":path", absl::StrCat("/path/with", AdditionallyAllowedCharactersHttp2)}};
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  EXPECT_EQ(headers.path(),
            absl::StrCat("/path/with\t \"<>[]^`{}\\|", generateExtendedAsciiString()));
}

TEST_F(Http2HeaderValidatorTest, AdditionalCharactersInPathAllowedWithoutPathNormalizationHttp3) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "true"}});
  protocol_ = Protocol::Http3;
  auto uhv = createH2ServerUhv(no_path_normalization);

  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"},
      {":authority", "envoy.com"},
      {":method", "GET"},
      {":path", absl::StrCat("/path/with", AdditionallyAllowedCharactersHttp3)}};
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  EXPECT_EQ(headers.path(), "/path/with\t \"<>[]^`{}\\|");
}

TEST_F(Http2HeaderValidatorTest, AdditionalCharactersInQueryAllowedHttp2) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "true"}});
  auto uhv = createH2ServerUhv(fragment_in_path_allowed);

  validateAllCharactersInUrlPath(*uhv, "/query?key=additional/characters",
                                 absl::StrCat("?#", AdditionallyAllowedCharactersHttp2));

  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"},
      {":authority", "envoy.com"},
      {":method", "GET"},
      {":path", absl::StrCat("/path/with?value=", AdditionallyAllowedCharactersHttp2)}};
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  // Additional characters in query always remain untouched
  EXPECT_EQ(headers.path(),
            absl::StrCat("/path/with?value=\t \"<>[]^`{}\\|", generateExtendedAsciiString()));
}

TEST_F(Http2HeaderValidatorTest, AdditionalCharactersInQueryAllowedHttp3) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "true"}});
  protocol_ = Protocol::Http3;
  auto uhv = createH2ServerUhv(fragment_in_path_allowed);

  validateAllCharactersInUrlPath(*uhv, "/query?key=additional/characters",
                                 absl::StrCat("?#", AdditionallyAllowedCharactersHttp3));

  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"},
      {":authority", "envoy.com"},
      {":method", "GET"},
      {":path", absl::StrCat("/path/with?value=", AdditionallyAllowedCharactersHttp3)}};
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  // Additional characters in query always remain untouched
  EXPECT_EQ(headers.path(), "/path/with?value=\t \"<>[]^`{}\\|");
}

TEST_F(Http2HeaderValidatorTest, AdditionalCharactersInFragmentAllowedHttp2) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "true"}});
  auto uhv = createH2ServerUhv(fragment_in_path_allowed);

  validateAllCharactersInUrlPath(*uhv, "/q?k=v#fragment/additional/characters",
                                 absl::StrCat("?#", AdditionallyAllowedCharactersHttp2));

  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"},
      {":authority", "envoy.com"},
      {":method", "GET"},
      {":path", absl::StrCat("/path/with?value=aaa#", AdditionallyAllowedCharactersHttp2)}};
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  // UHV strips fragment from URL path
  EXPECT_EQ(headers.path(), "/path/with?value=aaa");
}

TEST_F(Http2HeaderValidatorTest, AdditionalCharactersInFragmentAllowedHttp3) {
  scoped_runtime_.mergeValues({{"envoy.uhv.allow_non_compliant_characters_in_path", "true"}});
  protocol_ = Protocol::Http3;
  auto uhv = createH2ServerUhv(fragment_in_path_allowed);

  validateAllCharactersInUrlPath(*uhv, "/q?k=v#fragment/additional/characters",
                                 absl::StrCat("?#", AdditionallyAllowedCharactersHttp3));

  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":scheme", "https"},
      {":authority", "envoy.com"},
      {":method", "GET"},
      {":path", absl::StrCat("/path/with?value=aaa#", AdditionallyAllowedCharactersHttp3)}};
  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  // UHV strips fragment from URL path
  EXPECT_EQ(headers.path(), "/path/with?value=aaa");
}

} // namespace
} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
