#include "url/gurl.h"

#include "gtest/gtest.h"

// Basic smoke test to ensure that GURL (with shimmed ICU) works properly.
TEST(GoogleUrl, SmokeTest) {
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

  // Ensure ICU shim is functioning correctly, i.e. not crashing and resulting invalid parsed URL.
  GURL idn_url("https://\xe5\x85\x89.example/");
  EXPECT_FALSE(idn_url.is_valid());

  GURL percent_encoded("https://%Da%aa.example");
  EXPECT_FALSE(percent_encoded.is_valid());

  GURL percent_encoded_more("https://%20.example");
  EXPECT_FALSE(percent_encoded.is_valid());

  GURL with_bracket("http://[wwww].example");
  EXPECT_FALSE(with_bracket.is_valid());
}
