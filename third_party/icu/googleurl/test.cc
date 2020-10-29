#include "url/gurl.h"

#include "gtest/gtest.h"

// Basic smoke test to ensure that GURL (with shimmed ICU) works properly.
TEST(GoogleUrl, SmokeTest) {
  GURL url("https://example.org/test?foo=bar#section");
  ASSERT_TRUE(url.is_valid());
  ASSERT_EQ(url.scheme(), "https");
  ASSERT_EQ(url.host(), "example.org");
  ASSERT_EQ(url.EffectiveIntPort(), 443);
  ASSERT_EQ(url.path(), "/test");
  ASSERT_EQ(url.query(), "foo=bar");
  ASSERT_EQ(url.ref(), "section");

  // Ensure ICU shim is functioning correctly, i.e. not crashing and resulting invalid parsed URL.
  GURL idn_url("https://\xe5\x85\x89.example/");
  ASSERT_FALSE(idn_url.is_valid());
}
