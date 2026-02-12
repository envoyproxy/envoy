#include <openssl/ssl.h>
#include <ossl.h>


static const char *kUnknownVersion = "unknown";

struct VersionInfo {
  uint16_t version;
  const char *name;
};

static const struct VersionInfo kVersionNames[] = {
    {TLS1_3_VERSION, "TLSv1.3"},
    {TLS1_2_VERSION, "TLSv1.2"},
    {TLS1_1_VERSION, "TLSv1.1"},
    {TLS1_VERSION, "TLSv1"},
    {DTLS1_VERSION, "DTLSv1"},
    {DTLS1_2_VERSION, "DTLSv1.2"},
};

size_t SSL_get_all_version_names(const char **out, size_t max_out) {
   size_t versionSize = (sizeof(kVersionNames) / sizeof(kVersionNames[0]));
   if(max_out != 0) {
     *out++ = kUnknownVersion;
     for(int i = 0; i < versionSize; i++) {
        *out++ = kVersionNames[i].name;
     }
   }
   return 1+versionSize;
}


