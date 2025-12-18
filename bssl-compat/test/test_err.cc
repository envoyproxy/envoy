#include <gtest/gtest.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <cerrno>
#include <cstring>
#include <limits>

TEST(ErrTest, test_ERR_func_error_string) {
  ASSERT_STREQ("OPENSSL_internal", ERR_func_error_string(0));
  ASSERT_STREQ("OPENSSL_internal", ERR_func_error_string(42));
}

TEST(ErrTest, test_ERR_LIB_SSL_ERR_R_MALLOC_FAILURE) {
  char buf[256]{};

  ERR_clear_error();

  ERR_put_error(ERR_LIB_SSL, 0, ERR_R_MALLOC_FAILURE, __FILE__, __LINE__);

  uint32_t e = ERR_get_error();

  EXPECT_EQ(0x10000041, e);
  EXPECT_STREQ("SSL routines", ERR_lib_error_string(e));
  EXPECT_STREQ("malloc failure", ERR_reason_error_string(e));
  EXPECT_STREQ("error:10000041:SSL routines:OPENSSL_internal:malloc failure",
               ERR_error_string_n(e, buf, sizeof(buf)));
}

/**
 * This covers a fix for test
 * IpVersionsClientVersions/SslCertficateIntegrationTest.ServerEcdsaClientRsaOnlyWithAccessLog/IPv4_TLSv1_3
 * which fails because of an error string mismatch between BoringSSL's string and OpenSSL's string:
 *
 * Expected:
 * "DOWNSTREAM_TRANSPORT_FAILURE_REASON=TLS_error:_268435709:SSL_routines:OPENSSL_internal:NO_COMMON_SIGNATURE_ALGORITHMS"
 * Actual:
 * "DOWNSTREAM_TRANSPORT_FAILURE_REASON=TLS_error:_167772278:SSL_routines:OPENSSL_internal:no_suitable_signature_algorithm
 * FILTER_CHAIN_NAME=-"
 */
TEST(ErrTest, test_SSL_R_NO_SUITABLE_SIGNATURE_ALGORITHM) {
  char buf[256]{};
  ERR_clear_error();

#ifdef BSSL_COMPAT
  ERR_put_error(ERR_LIB_SSL, 0, ossl_SSL_R_NO_SUITABLE_SIGNATURE_ALGORITHM, __FILE__, __LINE__);
#else // BoringSSL
  ERR_put_error(ERR_LIB_SSL, 0, SSL_R_NO_COMMON_SIGNATURE_ALGORITHMS, __FILE__, __LINE__);
#endif

  uint32_t e = ERR_get_error();

  EXPECT_EQ(268435709, e);
  EXPECT_STREQ("SSL routines", ERR_lib_error_string(e));
  EXPECT_STREQ("NO_COMMON_SIGNATURE_ALGORITHMS", ERR_reason_error_string(e));
  EXPECT_STREQ("error:100000fd:SSL routines:OPENSSL_internal:NO_COMMON_SIGNATURE_ALGORITHMS",
               ERR_error_string_n(e, buf, sizeof(buf)));
}

/**
 * OpenSSL and BoringSSL pack errors (consisting of library & reason codes) into
 * a 32 bit integer using similar mechanisms, but with slightly different mask
 * and shift values, giving different values between the libraries. This is
 * mostly hidden by the use of macros like ERR_PACK(library, reason),
 * ERR_GET_LIB(error), ERR_GET_REASON(error), and library codes like
 * ERR_LIB_SSL, ERR_LIB_X509 etc.
 *
 * However, OpenSSL and BoringSSL pack _system errors_ very differently....
 *
 * BoringSSL packs system errors the same as all its other errors, by passing
 * ERR_LIB_SYS as the library code, and errno as the reason, to the
 * ERR_PACK(lib,reason) macro i.e. ERR_PACK(ERR_LIB_SYS, errno).
 *
 * OpenSSL _does not_ pack system errors with ERR_PACK(ERR_LIB_SYS, errno) as
 * one might expect. Instead, it uses a different special case mechanism of
 * OR-ing the special ERR_SYSTEM_FLAG (0x80000000) with the errno value i.e.
 * (ERR_SYSTEM_FLAG | errno).
 *
 * For example, to represent a EPIPE system error (0x20 on Linux):
 * - BoringSSL : ERR_PACK(ERR_LIB_SYS,EPIPE)) = 0x02000020
 * - OpenSSL   : (ERR_SYSTEM_FLAG | EPIPE) = 0x80000020
 *
 * Since bssl-compat is trying to emulate BoringSSL's behavior, we need to
 * ensure that packing system errors using ERR_PACK() appears to work the same
 * way as BoringSSL (even though the resulting numeric values will be
 * different).
 */
TEST(ErrTest, test_ERR_PACK_system_error) {
  uint32_t packed = ERR_PACK(ERR_LIB_SYS, EPIPE);

  EXPECT_EQ(EPIPE, 0x20); // Sanity check

#ifdef BSSL_COMPAT
  EXPECT_EQ(ossl_ERR_SYSTEM_FLAG, 0x80000000); // Sanity check
  // These checks will fail if we haven't modified bssl-compat's definition of
  // ERR_PACK(lib,reason) to handle OpenSSL's special case for system errors.
  EXPECT_EQ(packed, ossl_ERR_SYSTEM_FLAG | EPIPE);
  EXPECT_EQ(packed, 0x80000020);
#else
  EXPECT_EQ(ERR_LIB_SYS, 2); // Sanity check
  EXPECT_EQ(packed, 0x02000020);
#endif

  // These should work idenically on BoringSSL and OpenSSL
  EXPECT_EQ(ERR_GET_LIB(packed), ERR_LIB_SYS);
  EXPECT_EQ(ERR_GET_REASON(packed), EPIPE);
}

TEST(ErrTest, test_system_error_ECONNRESET) {
  uint32_t err = ERR_PACK(ERR_LIB_SYS, ECONNRESET);

  ASSERT_EQ(ECONNRESET, 0x68); // Sanity check

#ifdef BSSL_COMPAT
  EXPECT_EQ(err, 0x80000068); // OpenSSL packed value
#else
  EXPECT_EQ(err, 0x02000068); // BoringSSL packed value
#endif

  EXPECT_EQ(ERR_LIB_SYS, ERR_GET_LIB(err));
  EXPECT_EQ(ECONNRESET, ERR_GET_REASON(err));

  const char* lib = ERR_lib_error_string(err);
  ASSERT_NE(nullptr, lib);
  EXPECT_STREQ("system library", lib);

  const char* reason = ERR_reason_error_string(err);
  ASSERT_NE(nullptr, reason);
  EXPECT_STREQ(strerror(ECONNRESET), reason);
}

TEST(ErrTest, test_system_error_EPIPE) {
  uint32_t err = ERR_PACK(ERR_LIB_SYS, EPIPE);

  ASSERT_EQ(EPIPE, 0x20); // Sanity check

#ifdef BSSL_COMPAT
  EXPECT_EQ(err, 0x80000020); // OpenSSL packed value
#else
  EXPECT_EQ(err, 0x02000020); // BoringSSL packed value
#endif

  EXPECT_EQ(ERR_LIB_SYS, ERR_GET_LIB(err));
  EXPECT_EQ(EPIPE, ERR_GET_REASON(err));

  const char* lib = ERR_lib_error_string(err);
  ASSERT_NE(nullptr, lib);
  EXPECT_STREQ("system library", lib);

  const char* reason = ERR_reason_error_string(err);
  ASSERT_NE(nullptr, reason);
  EXPECT_STREQ(strerror(EPIPE), reason);
}

TEST(ErrTest, test_system_error_ETIMEDOUT) {
  uint32_t err = ERR_PACK(ERR_LIB_SYS, ETIMEDOUT);

  ASSERT_EQ(ETIMEDOUT, 0x6E); // Sanity check

#ifdef BSSL_COMPAT
  EXPECT_EQ(err, 0x8000006E); // OpenSSL packed value
#else
  EXPECT_EQ(err, 0x0200006E); // BoringSSL packed value
#endif

  EXPECT_EQ(ERR_LIB_SYS, ERR_GET_LIB(err));
  EXPECT_EQ(ETIMEDOUT, ERR_GET_REASON(err));

  const char* lib = ERR_lib_error_string(err);
  ASSERT_NE(nullptr, lib);
  EXPECT_STREQ("system library", lib);

  const char* reason = ERR_reason_error_string(err);
  ASSERT_NE(nullptr, reason);
  EXPECT_STREQ(strerror(ETIMEDOUT), reason);
}

TEST(ErrTest, test_system_error_invalid_errno) {
  uint32_t err = ERR_PACK(ERR_LIB_SYS, 0xFFF);

  EXPECT_EQ(ERR_LIB_SYS, ERR_GET_LIB(err));

  const char* lib = ERR_lib_error_string(err);
  ASSERT_NE(nullptr, lib);
  EXPECT_STREQ("system library", lib);

  const char* reason = ERR_reason_error_string(err);
  ASSERT_NE(nullptr, reason);
  EXPECT_STREQ("unknown error", reason);
}
