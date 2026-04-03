#pragma once

namespace Envoy {
namespace Ssl {

#ifdef ENVOY_SSL_OPENSSL
#define SSL_SELECT(BORINGSSL, OPENSSL) OPENSSL
#else
#define SSL_SELECT(BORINGSSL, OPENSSL) BORINGSSL
#endif

#ifdef ENVOY_SSL_OPENSSL
#define BORINGSSL_TEST_P(test_suite, test_name) TEST_P(test_suite, DISABLED_##test_name)
#else
#define BORINGSSL_TEST_P(test_suite, test_name) TEST_P(test_suite, test_name)
#endif

#ifdef ENVOY_SSL_OPENSSL
#define BORINGSSL_TEST_F(test_suite, test_name) TEST_F(test_suite, DISABLED_##test_name)
#else
#define BORINGSSL_TEST_F(test_suite, test_name) TEST_F(test_suite, test_name)
#endif

#if ENVOY_SSL_OPENSSL
#define ENVOY_OPENSSL_CAST(cast_type, field) cast_type(field)
#else
#define ENVOY_OPENSSL_CAST(cast_type, field) field
#endif

} // namespace Ssl
} // namespace Envoy
