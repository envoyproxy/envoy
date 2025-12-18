#pragma once

#ifdef ENVOY_SSL_OPENSSL
#define SSL_SELECT(BORINGSSL, OPENSSL) OPENSSL
#else
#define SSL_SELECT(BORINGSSL, OPENSSL) BORINGSSL
#endif

#ifdef ENVOY_SSL_OPENSSL
#define BORINGSSL_TEST_P(test_suite, test_name) \
  TEST_P(test_suite, DISABLED_##test_name)
#else
#define BORINGSSL_TEST_P(test_suite, test_name) \
  TEST_P(test_suite, test_name)
#endif

#ifdef ENVOY_SSL_OPENSSL
#define BORINGSSL_TEST_F(test_suite, test_name) \
  TEST_F(test_suite, DISABLED_##test_name)
#else
#define BORINGSSL_TEST_F(test_suite, test_name) \
  TEST_F(test_suite, test_name)
#endif
