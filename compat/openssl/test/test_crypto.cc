#include <gtest/gtest.h>
#include <openssl/crypto.h>

TEST(TestCrypto, test_FIPS_mode) { ASSERT_EQ(0, FIPS_mode()); }

#ifdef BSSL_COMPAT
#include <dlfcn.h>
#include <gmock/gmock.h>

TEST(TestCrypto, test_openssl_uses_main_executable_allocator) {
  ossl_CRYPTO_malloc_fn malloc_fn = nullptr;
  ossl_CRYPTO_realloc_fn realloc_fn = nullptr;
  ossl_CRYPTO_free_fn free_fn = nullptr;
  ossl_CRYPTO_get_mem_functions(&malloc_fn, &realloc_fn, &free_fn);

  Dl_info info;
  ASSERT_NE(dladdr(reinterpret_cast<void *>(malloc_fn), &info), 0);
  EXPECT_THAT(info.dli_fname, ::testing::Not(::testing::HasSubstr("libcrypto")));

  ASSERT_NE(dladdr(reinterpret_cast<void *>(realloc_fn), &info), 0);
  EXPECT_THAT(info.dli_fname, ::testing::Not(::testing::HasSubstr("libcrypto")));

  ASSERT_NE(dladdr(reinterpret_cast<void *>(free_fn), &info), 0);
  EXPECT_THAT(info.dli_fname, ::testing::Not(::testing::HasSubstr("libcrypto")));
}
#endif
