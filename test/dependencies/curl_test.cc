#include "curl/curl.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Dependencies {

TEST(CurlTest, BuiltWithExpectedFeatures) {
  // Ensure built with the expected features, flags from
  // https://curl.haxx.se/libcurl/c/curl_version_info.html.
  curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);

  EXPECT_NE(0, info->features & CURL_VERSION_ASYNCHDNS);
  EXPECT_NE(0, info->ares_num);
  EXPECT_NE(0, info->features & CURL_VERSION_HTTP2);
  EXPECT_NE(0, info->features & CURL_VERSION_LIBZ);
  EXPECT_NE(0, info->features & CURL_VERSION_IPV6);

#ifndef WIN32
  EXPECT_NE(0, info->features & CURL_VERSION_UNIX_SOCKETS);
#else
  EXPECT_EQ(0, info->features & CURL_VERSION_UNIX_SOCKETS);
#endif

  EXPECT_EQ(0, info->features & CURL_VERSION_BROTLI);
  EXPECT_EQ(0, info->features & CURL_VERSION_GSSAPI);
  EXPECT_EQ(0, info->features & CURL_VERSION_GSSNEGOTIATE);
  EXPECT_EQ(0, info->features & CURL_VERSION_KERBEROS4);
  EXPECT_EQ(0, info->features & CURL_VERSION_KERBEROS5);
  EXPECT_EQ(0, info->features & CURL_VERSION_NTLM);
  EXPECT_EQ(0, info->features & CURL_VERSION_NTLM_WB);
  EXPECT_EQ(0, info->features & CURL_VERSION_SPNEGO);
  EXPECT_EQ(0, info->features & CURL_VERSION_SSL);
  EXPECT_EQ(0, info->features & CURL_VERSION_SSPI);
}

} // namespace Dependencies
} // namespace Envoy
