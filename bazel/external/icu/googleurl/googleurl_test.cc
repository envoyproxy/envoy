#include "gtest/gtest.h"

#include "url/gurl.h"

namespace Envoy {
namespace {

void expectInvalidUrl(const std::string& input) {
  GURL url(input);
  EXPECT_FALSE(url.is_valid());
}

} // namespace

// Verifies that valid URLs are parsed correctly.
TEST(GoogleUrl, ValidUrl) {
  GURL url("https://example.org/test?foo=bar#section");
  EXPECT_TRUE(url.is_valid());
  EXPECT_EQ(url.scheme(), "https");
  EXPECT_EQ(url.host(), "example.org");
  EXPECT_EQ(url.EffectiveIntPort(), 443);
  EXPECT_EQ(url.path(), "/test");
  EXPECT_EQ(url.query(), "foo=bar");
  EXPECT_EQ(url.ref(), "section");

  GURL punycode("https://xn--c1yn36f.example");
  EXPECT_TRUE(punycode.is_valid());

  GURL percent_encoded_valid("https://%20.example");
  EXPECT_TRUE(percent_encoded_valid.is_valid());

  GURL extra_slashes("https:///host");
  EXPECT_TRUE(extra_slashes.is_valid());
  EXPECT_EQ(extra_slashes.host(), "host");
}

// Verifies that invalid URLs can be handled and ensures that GURL (with shimmed ICU) works properly
// when parsing URLs with IDN host name.
TEST(GoogleUrl, InvalidUrl) {
  // Ensure ICU shim is functioning correctly, i.e. not crashing and resulting invalid parsed URL.
  expectInvalidUrl("https://\xe5\x85\x89.example/");

  expectInvalidUrl("http://\xef\xb9\xaa.com");
  expectInvalidUrl("https://%Da%aa.example");
  expectInvalidUrl("http://[wwww].example");
  expectInvalidUrl("http://?k=v");
}
} // namespace Envoy
