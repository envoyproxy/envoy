#include <dlfcn.h>
#include <gtest/gtest.h>
#include <openssl/asn1.h>
#include <openssl/digest.h>
#include <openssl/ec_key.h>
#include <openssl/ecdsa.h>

TEST(MiscTest, test_UniquePtr_ASN1_OBJECT) { bssl::UniquePtr<ASN1_OBJECT> p; }

TEST(MiscTest, test_UniquePtr_EC_KEY) { bssl::UniquePtr<EC_KEY> p; }

TEST(MiscTest, test_UniquePtr_EVP_MD_CTX) { bssl::UniquePtr<EVP_MD_CTX> p; }

TEST(MiscTest, test_UniquePtr_ECDSA_SIG) { bssl::UniquePtr<ECDSA_SIG> p; }

#ifdef BSSL_COMPAT
// Since we load the OpenSSL shared libraries with RTLD_LOCAL, and we build the
// compatibility layer with -fvisibility=hidden, none of these dlsym() calls
// should succeed.
TEST(MiscTest, OpenSSLSymbolsNotVisible) {
  EXPECT_EQ(nullptr, dlsym(RTLD_DEFAULT, "SSL_new"));
  EXPECT_EQ(nullptr, dlsym(RTLD_DEFAULT, "SSL_read"));
  EXPECT_EQ(nullptr, dlsym(RTLD_DEFAULT, "SSL_write"));
}
#endif

#if defined(__SANITIZE_ADDRESS__)
// Verify that LeakSanitizer is active and detecting leaks. This guards against
// build configurations that silently disable ASAN/LSan (e.g. missing flags,
// interceptor bypasses). The death test forks a subprocess that deliberately
// leaks, then calls exit() to trigger the sanitizer checks, which aborts if
// leaks are found.
TEST(MiscTest, LeakSanitizerIsActive) {
  EXPECT_DEATH(
      {
        void* volatile leak = malloc(1234);
        leak = nullptr;
        (void)leak;
        exit(0);
      },
      "LeakSanitizer");
}
#endif
