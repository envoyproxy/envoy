#include "source/common/http/header_validation.h"

#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace HeaderValidation {

void headerNameIsValid() {
  // Isn't valid but passes legacy checks.
  EXPECT_FALSE(headerNameIsValid(":"));
  EXPECT_FALSE(headerNameIsValid("::"));
  EXPECT_FALSE(headerNameIsValid("\n"));
  EXPECT_FALSE(headerNameIsValid(":\n"));
  EXPECT_FALSE(headerNameIsValid("asd\n"));

  EXPECT_TRUE(headerNameIsValid("asd"));
  // Not actually valid but passes legacy Envoy checks.
  EXPECT_TRUE(headerNameIsValid(":asd"));
}

TEST(HeaderIsValidTest, HeaderNameIsValid) { headerNameIsValid(); }

TEST(HeaderIsValidTest, HeaderNameIsValidLegacy) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.sanitize_http2_headers_without_nghttp2", "false"}});
  headerNameIsValid();
}

TEST(HeaderIsValidTest, InvalidHeaderValuesAreRejected) {
  // ASCII values 1-31 are control characters (with the exception of ASCII
  // values 9, 10, and 13 which are a horizontal tab, line feed, and carriage
  // return, respectively), and are not valid in an HTTP header, per
  // RFC 7230, section 3.2
  for (int i = 0; i < 32; i++) {
    if (i == 9) {
      continue;
    }

    EXPECT_FALSE(headerValueIsValid(std::string(1, i)));
  }
}

TEST(HeaderIsValidTest, ValidHeaderValuesAreAccepted) {
  EXPECT_TRUE(headerValueIsValid("some-value"));
  EXPECT_TRUE(headerValueIsValid("Some Other Value"));
}

TEST(HeaderIsValidTest, AuthorityIsValid) {
  EXPECT_TRUE(authorityIsValid("strangebutlegal$-%&'"));
  EXPECT_FALSE(authorityIsValid("illegal{}"));
  // Validate that the "@" character is allowed.
  // TODO(adisuissa): Once the envoy.reloadable_features.internal_authority_header_validator
  // runtime flag is deprecated, this test should only validate the assignment
  // to "true".
  {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues(
        {{"envoy.reloadable_features.internal_authority_header_validator", "true"}});
    EXPECT_TRUE(authorityIsValid("username@example.com'"));
  }
  {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues(
        {{"envoy.reloadable_features.internal_authority_header_validator", "false"}});
    // When the above is false, Envoy should use oghttp2's validator which will
    // reject the "@" character.
    EXPECT_FALSE(authorityIsValid("username@example.com'"));
  }
}

#ifdef NDEBUG
// These tests send invalid request and response header names which violate ASSERT while creating
// such request/response headers. So they can only be run in NDEBUG mode.
TEST(ValidateHeaders, ForbiddenCharacters) {
  {
    // Valid headers
    TestRequestHeaderMapImpl headers{
        {":method", "CONNECT"}, {":authority", "foo.com:80"}, {"x-foo", "hello world"}};
    EXPECT_EQ(Http::okStatus(), checkValidRequestHeaders(headers));
  }

  {
    // Mixed case header key is ok
    TestRequestHeaderMapImpl headers{{":method", "CONNECT"}, {":authority", "foo.com:80"}};
    Http::HeaderString invalid_key(absl::string_view("x-MiXeD-CaSe"));
    headers.addViaMove(std::move(invalid_key),
                       Http::HeaderString(absl::string_view("hello world")));
    EXPECT_TRUE(checkValidRequestHeaders(headers).ok());
  }

  {
    // Invalid key
    TestRequestHeaderMapImpl headers{
        {":method", "CONNECT"}, {":authority", "foo.com:80"}, {"x-foo\r\n", "hello world"}};
    EXPECT_NE(Http::okStatus(), checkValidRequestHeaders(headers));
  }

  {
    // Invalid value
    TestRequestHeaderMapImpl headers{{":method", "CONNECT"},
                                     {":authority", "foo.com:80"},
                                     {"x-foo", "hello\r\n\r\nGET /evil HTTP/1.1"}};
    EXPECT_NE(Http::okStatus(), checkValidRequestHeaders(headers));
  }
}
#endif

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
TEST(HeaderIsValidTest, IsCapsuleProtocol) {
  EXPECT_TRUE(isCapsuleProtocol(TestRequestHeaderMapImpl{{"Capsule-Protocol", "?1"}}));
  EXPECT_TRUE(
      isCapsuleProtocol(TestRequestHeaderMapImpl{{"Capsule-Protocol", "?1;a=1;b=2;c;d=?0"}}));
  EXPECT_FALSE(isCapsuleProtocol(TestRequestHeaderMapImpl{{"Capsule-Protocol", "?0"}}));
  EXPECT_FALSE(isCapsuleProtocol(
      TestRequestHeaderMapImpl{{"Capsule-Protocol", "?1"}, {"Capsule-Protocol", "?1"}}));
  EXPECT_FALSE(isCapsuleProtocol(TestRequestHeaderMapImpl{{":method", "CONNECT"}}));
  EXPECT_TRUE(
      isCapsuleProtocol(TestResponseHeaderMapImpl{{":status", "200"}, {"Capsule-Protocol", "?1"}}));
  EXPECT_FALSE(isCapsuleProtocol(TestResponseHeaderMapImpl{{":status", "200"}}));
}
#endif

} // namespace HeaderValidation
} // namespace Http
} // namespace Envoy
