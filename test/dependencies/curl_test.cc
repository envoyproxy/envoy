#include "curl/curl.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Dependencies {

TEST(CurlTest, BuiltWithExpectedFeatures) {
  // Ensure built with the expected features, flags from
  // https://curl.haxx.se/libcurl/c/curl_version_info.html.
  curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);

  // In sequence as declared in curl.h. Overlook any toggle of the
  // developer or os elections for DEBUG, CURL DEBUG and LARGE FILE
  EXPECT_NE(0, info->features & CURL_VERSION_IPV6);
  EXPECT_EQ(0, info->features & CURL_VERSION_KERBEROS4);
  EXPECT_EQ(0, info->features & CURL_VERSION_SSL);
  EXPECT_NE(0, info->features & CURL_VERSION_LIBZ);
  EXPECT_EQ(0, info->features & CURL_VERSION_NTLM);
  EXPECT_EQ(0, info->features & CURL_VERSION_GSSNEGOTIATE);
  EXPECT_NE(0, info->features & CURL_VERSION_ASYNCHDNS);
  EXPECT_EQ(0, info->features & CURL_VERSION_SPNEGO);
  EXPECT_EQ(0, info->features & CURL_VERSION_IDN);
  EXPECT_EQ(0, info->features & CURL_VERSION_SSPI);
  EXPECT_EQ(0, info->features & CURL_VERSION_CONV);
  EXPECT_EQ(0, info->features & CURL_VERSION_TLSAUTH_SRP);
  EXPECT_EQ(0, info->features & CURL_VERSION_NTLM_WB);
  EXPECT_NE(0, info->features & CURL_VERSION_HTTP2);
  EXPECT_EQ(0, info->features & CURL_VERSION_GSSAPI);
  EXPECT_EQ(0, info->features & CURL_VERSION_KERBEROS5);
  EXPECT_NE(0, info->features & CURL_VERSION_UNIX_SOCKETS);
  EXPECT_EQ(0, info->features & CURL_VERSION_PSL);
  EXPECT_EQ(0, info->features & CURL_VERSION_HTTPS_PROXY);
  EXPECT_EQ(0, info->features & CURL_VERSION_MULTI_SSL);
  EXPECT_EQ(0, info->features & CURL_VERSION_BROTLI);
  EXPECT_EQ(0, info->features & CURL_VERSION_ALTSVC);
  EXPECT_EQ(0, info->features & CURL_VERSION_HTTP3);
  EXPECT_NE(0, info->ares_num);
}

} // namespace Dependencies
} // namespace Envoy
