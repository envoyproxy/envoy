#include <gtest/gtest.h>
#include <openssl/crypto.h>

TEST(TestCrypto, test_FIPS_mode) { ASSERT_EQ(0, FIPS_mode()); }

#ifdef BSSL_COMPAT
#include <dlfcn.h>
#include <gmock/gmock.h>
extern "C" {
#include "ossl_dlutil.h"
}

TEST(TestCrypto, test_openssl_uses_main_executable_allocator) {
  typedef void (*CRYPTO_get_mem_functions_fn)(
      void *(**)(size_t, const char *, int),
      void *(**)(void *, size_t, const char *, int),
      void (**)(void *, const char *, int));

  auto get_mem_fn = reinterpret_cast<CRYPTO_get_mem_functions_fn>(
      ossl_dlsym("ossl_CRYPTO_get_mem_functions"));
  ASSERT_NE(get_mem_fn, nullptr);

  void *(*malloc_fn)(size_t, const char *, int) = nullptr;
  void *(*realloc_fn)(void *, size_t, const char *, int) = nullptr;
  void (*free_fn)(void *, const char *, int) = nullptr;
  get_mem_fn(&malloc_fn, &realloc_fn, &free_fn);

  Dl_info info;
  ASSERT_NE(dladdr(reinterpret_cast<void *>(malloc_fn), &info), 0);
  EXPECT_THAT(info.dli_fname, ::testing::Not(::testing::HasSubstr("libcrypto")));

  ASSERT_NE(dladdr(reinterpret_cast<void *>(realloc_fn), &info), 0);
  EXPECT_THAT(info.dli_fname, ::testing::Not(::testing::HasSubstr("libcrypto")));

  ASSERT_NE(dladdr(reinterpret_cast<void *>(free_fn), &info), 0);
  EXPECT_THAT(info.dli_fname, ::testing::Not(::testing::HasSubstr("libcrypto")));
}
#endif
