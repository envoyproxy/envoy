// Cross-language tests for the dynamic_modules cert validator. Parameterized over the SDK
// language. Each language ships a "cert_validator_test" module exposing a "test" cert
// validator that always returns Successful. The C counterpart (cert_validator_no_op) is
// exercised by the TEST_F suite in dynamic_modules_cert_validator_test.cc.
//
// This is split from the unit test file because loading Go test_data .so files at runtime
// requires the test binary to depend on //source/extensions/dynamic_modules:all_abi_impls
// to satisfy unresolved Go SDK symbols. That meta-dep also pulls in the WEAK fallbacks for
// cert-validator host callbacks (set_filter_state / get_filter_state / set_error_details).
// When both weak and strong are in the same link unit, the linker resolves direct C++ calls
// to the WEAK version, leaving the strong code paths in config.cc uncovered. Keeping these
// cross-language tests in their own binary lets the unit test binary depend only on
// //source/extensions/transport_sockets/tls/cert_validator/dynamic_modules:config, so direct
// C++ calls there resolve to the strong version and get coverage.

#include "source/common/tls/cert_validator/cert_validator.h"
#include "source/common/tls/stats.h"
#include "source/extensions/transport_sockets/tls/cert_validator/dynamic_modules/config.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/common/tls/ssl_test_utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace DynamicModules {
namespace {

class DynamicModuleCertValidatorLanguageTest : public testing::TestWithParam<std::string> {
protected:
  DynamicModuleCertValidatorLanguageTest()
      : api_(Api::createApiForTest()), stats_(generateSslStats(*store_.rootScope())) {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/" +
                                    GetParam()),
        1);
    TestEnvironment::setEnvVar("GODEBUG", "cgocheck=0", 1);
  }

  Api::ApiPtr api_;
  Stats::TestUtil::TestStore store_;
  SslStats stats_;
};

INSTANTIATE_TEST_SUITE_P(SdkLanguages, DynamicModuleCertValidatorLanguageTest,
                         testing::Values("rust", "go"));

TEST_P(DynamicModuleCertValidatorLanguageTest, ConfigNewSuccess) {
  auto module = Envoy::Extensions::DynamicModules::newDynamicModuleByName("cert_validator_test",
                                                                          false, false);
  ASSERT_TRUE(module.ok());
  auto config_or_error = newDynamicModuleCertValidatorConfig("test", "", std::move(module.value()));
  ASSERT_TRUE(config_or_error.ok());
  EXPECT_NE(config_or_error.value()->in_module_config_, nullptr);
}

TEST_P(DynamicModuleCertValidatorLanguageTest, VerifyCertChainSuccess) {
  auto module = Envoy::Extensions::DynamicModules::newDynamicModuleByName("cert_validator_test",
                                                                          false, false);
  ASSERT_TRUE(module.ok());
  auto config_or_error = newDynamicModuleCertValidatorConfig("test", "", std::move(module.value()));
  ASSERT_TRUE(config_or_error.ok());

  DynamicModuleCertValidator validator(config_or_error.value(), stats_);

  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  sk_X509_push(cert_chain.get(), cert.release());

  CSmartPtr<SSL_CTX, SSL_CTX_free> ssl_ctx(SSL_CTX_new(TLS_method()));
  auto results = validator.doVerifyCertChain(*cert_chain, nullptr, nullptr, *ssl_ctx, {}, false,
                                             "example.com");
  EXPECT_EQ(ValidationResults::ValidationStatus::Successful, results.status);
}

} // namespace
} // namespace DynamicModules
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
