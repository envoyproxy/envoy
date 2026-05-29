#include "envoy/router/string_accessor.h"

#include "source/common/tls/cert_validator/cert_validator.h"
#include "source/common/tls/stats.h"
#include "source/extensions/transport_sockets/tls/cert_validator/dynamic_modules/config.h"

#include "test/common/tls/cert_validator/test_common.h"
#include "test/common/tls/ssl_test_utility.h"
#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
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

using ::testing::NiceMock;

class DynamicModuleCertValidatorTest : public testing::Test {
protected:
  DynamicModuleCertValidatorTest()
      : api_(Api::createApiForTest()), stats_(generateSslStats(*store_.rootScope())) {
    Envoy::Extensions::DynamicModules::DynamicModulesTestEnvironment::setModulesSearchPath();
  }

  // Helper to create a config that loads the dynamic module by the C test program name and
  // creates the in-module config. Returns the shared config pointer or an error.
  absl::StatusOr<DynamicModuleCertValidatorConfigSharedPtr>
  createConfig(const std::string& module_name, const std::string& validator_name = "test",
               const std::string& validator_config = "") {
    auto module =
        Envoy::Extensions::DynamicModules::newDynamicModuleByName(module_name, false, false);
    if (!module.ok()) {
      return module.status();
    }
    return newDynamicModuleCertValidatorConfig(validator_name, validator_config,
                                               std::move(module.value()));
  }

  Api::ApiPtr api_;
  Stats::TestUtil::TestStore store_;
  SslStats stats_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
};

// =============================================================================
// Config creation tests.
// =============================================================================

TEST_F(DynamicModuleCertValidatorTest, ConfigNewSuccess) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());
  EXPECT_NE(config_or_error.value()->in_module_config_, nullptr);
}

TEST_F(DynamicModuleCertValidatorTest, ConfigNewReturnsNull) {
  auto config_or_error = createConfig("cert_validator_config_new_fail");
  ASSERT_FALSE(config_or_error.ok());
  EXPECT_THAT(config_or_error.status().message(),
              testing::HasSubstr("Failed to initialize dynamic module cert validator config"));
}

TEST_F(DynamicModuleCertValidatorTest, ConfigNewModuleNotFound) {
  auto module =
      Envoy::Extensions::DynamicModules::newDynamicModuleByName("nonexistent_module", false, false);
  EXPECT_FALSE(module.ok());
}

TEST_F(DynamicModuleCertValidatorTest, ConfigNewMissingSymbol) {
  // The "no_op" module does not implement cert validator functions.
  auto config_or_error = createConfig("no_op");
  ASSERT_FALSE(config_or_error.ok());
}

// =============================================================================
// doVerifyCertChain tests.
// =============================================================================

TEST_F(DynamicModuleCertValidatorTest, VerifyCertChainSuccess) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());

  DynamicModuleCertValidator validator(config_or_error.value(), stats_);

  // Load a real certificate for the chain.
  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));

  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  // Transfer ownership to the stack which frees elements via `sk_X509_pop_free`.
  sk_X509_push(cert_chain.get(), cert.release());

  CSmartPtr<SSL_CTX, SSL_CTX_free> ssl_ctx(SSL_CTX_new(TLS_method()));
  auto results = validator.doVerifyCertChain(*cert_chain, nullptr, nullptr, *ssl_ctx, {}, false,
                                             "example.com");
  EXPECT_EQ(ValidationResults::ValidationStatus::Successful, results.status);
  EXPECT_EQ(Envoy::Ssl::ClientValidationStatus::Validated, results.detailed_status);
  EXPECT_FALSE(results.tls_alert.has_value());
  EXPECT_FALSE(results.error_details.has_value());
}

TEST_F(DynamicModuleCertValidatorTest, VerifyCertChainFailure) {
  auto config_or_error = createConfig("cert_validator_fail");
  ASSERT_TRUE(config_or_error.ok());

  DynamicModuleCertValidator validator(config_or_error.value(), stats_);

  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));

  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  sk_X509_push(cert_chain.get(), cert.release());

  CSmartPtr<SSL_CTX, SSL_CTX_free> ssl_ctx(SSL_CTX_new(TLS_method()));
  auto results = validator.doVerifyCertChain(*cert_chain, nullptr, nullptr, *ssl_ctx, {}, false,
                                             "example.com");
  EXPECT_EQ(ValidationResults::ValidationStatus::Failed, results.status);
  EXPECT_EQ(Envoy::Ssl::ClientValidationStatus::Failed, results.detailed_status);
  ASSERT_TRUE(results.tls_alert.has_value());
  // SSL_AD_BAD_CERTIFICATE = 42.
  EXPECT_EQ(42, results.tls_alert.value());
  ASSERT_TRUE(results.error_details.has_value());
  EXPECT_EQ("certificate rejected by module", results.error_details.value());
  EXPECT_EQ(1, stats_.fail_verify_error_.value());
}

TEST_F(DynamicModuleCertValidatorTest, VerifyCertChainEmptyChain) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());

  DynamicModuleCertValidator validator(config_or_error.value(), stats_);

  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());

  CSmartPtr<SSL_CTX, SSL_CTX_free> ssl_ctx(SSL_CTX_new(TLS_method()));
  auto results =
      validator.doVerifyCertChain(*cert_chain, nullptr, nullptr, *ssl_ctx, {}, false, "");
  EXPECT_EQ(ValidationResults::ValidationStatus::Failed, results.status);
  EXPECT_EQ(Envoy::Ssl::ClientValidationStatus::NoClientCertificate, results.detailed_status);
  EXPECT_EQ(1, stats_.fail_verify_error_.value());
}

TEST_F(DynamicModuleCertValidatorTest, VerifyCertChainDerEncodingError) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());

  DynamicModuleCertValidator validator(config_or_error.value(), stats_);

  // Push a null X509 pointer into the chain to trigger a DER encoding failure.
  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  sk_X509_push(cert_chain.get(), nullptr);

  CSmartPtr<SSL_CTX, SSL_CTX_free> ssl_ctx(SSL_CTX_new(TLS_method()));
  auto results =
      validator.doVerifyCertChain(*cert_chain, nullptr, nullptr, *ssl_ctx, {}, false, "");
  EXPECT_EQ(ValidationResults::ValidationStatus::Failed, results.status);
  EXPECT_EQ(Envoy::Ssl::ClientValidationStatus::Failed, results.detailed_status);
  EXPECT_FALSE(results.tls_alert.has_value());
  ASSERT_TRUE(results.error_details.has_value());
  EXPECT_EQ("verify cert failed: DER encoding error", results.error_details.value());
  EXPECT_EQ(1, stats_.fail_verify_error_.value());
}

TEST_F(DynamicModuleCertValidatorTest, VerifyCertChainNoClientCertificateStatus) {
  auto config_or_error = createConfig("cert_validator_no_client_cert");
  ASSERT_TRUE(config_or_error.ok());

  DynamicModuleCertValidator validator(config_or_error.value(), stats_);

  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));

  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  sk_X509_push(cert_chain.get(), cert.release());

  CSmartPtr<SSL_CTX, SSL_CTX_free> ssl_ctx(SSL_CTX_new(TLS_method()));
  auto results = validator.doVerifyCertChain(*cert_chain, nullptr, nullptr, *ssl_ctx, {}, false,
                                             "example.com");
  EXPECT_EQ(ValidationResults::ValidationStatus::Failed, results.status);
  EXPECT_EQ(Envoy::Ssl::ClientValidationStatus::NoClientCertificate, results.detailed_status);
  EXPECT_EQ(1, stats_.fail_verify_error_.value());
}

TEST_F(DynamicModuleCertValidatorTest, VerifyCertChainNotValidatedDefaultStatus) {
  auto config_or_error = createConfig("cert_validator_not_validated");
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
  // Module explicitly returns NotValidated detailed status.
  EXPECT_EQ(Envoy::Ssl::ClientValidationStatus::NotValidated, results.detailed_status);
}

TEST_F(DynamicModuleCertValidatorTest, VerifyCertChainMultipleCerts) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());

  DynamicModuleCertValidator validator(config_or_error.value(), stats_);

  bssl::UniquePtr<X509> cert1 = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));
  bssl::UniquePtr<X509> cert2 = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"));

  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  sk_X509_push(cert_chain.get(), cert1.release());
  sk_X509_push(cert_chain.get(), cert2.release());

  CSmartPtr<SSL_CTX, SSL_CTX_free> ssl_ctx(SSL_CTX_new(TLS_method()));
  auto results = validator.doVerifyCertChain(*cert_chain, nullptr, nullptr, *ssl_ctx, {}, false,
                                             "example.com");
  EXPECT_EQ(ValidationResults::ValidationStatus::Successful, results.status);
  EXPECT_EQ(Envoy::Ssl::ClientValidationStatus::Validated, results.detailed_status);
}

TEST_F(DynamicModuleCertValidatorTest, VerifyCertChainWithIsServerTrue) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());

  DynamicModuleCertValidator validator(config_or_error.value(), stats_);

  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));

  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  sk_X509_push(cert_chain.get(), cert.release());

  CSmartPtr<SSL_CTX, SSL_CTX_free> ssl_ctx(SSL_CTX_new(TLS_method()));
  auto results =
      validator.doVerifyCertChain(*cert_chain, nullptr, nullptr, *ssl_ctx, {}, true, "example.com");
  EXPECT_EQ(ValidationResults::ValidationStatus::Successful, results.status);
}

// =============================================================================
// initializeSslContexts tests.
// =============================================================================

TEST_F(DynamicModuleCertValidatorTest, InitializeSslContextsReturnsVerifyMode) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());

  DynamicModuleCertValidator validator(config_or_error.value(), stats_);

  Stats::Scope& scope = *store_.rootScope();
  auto result = validator.initializeSslContexts({}, false, scope);
  ASSERT_TRUE(result.ok());
  // cert_validator_no_op returns SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT = 0x03.
  EXPECT_EQ(0x03, result.value());
}

TEST_F(DynamicModuleCertValidatorTest, InitializeSslContextsHandshakerProvidesCerts) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());

  DynamicModuleCertValidator validator(config_or_error.value(), stats_);

  Stats::Scope& scope = *store_.rootScope();
  auto result = validator.initializeSslContexts({}, true, scope);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(0x03, result.value());
}

// =============================================================================
// updateDigestForSessionId tests.
// =============================================================================

TEST_F(DynamicModuleCertValidatorTest, UpdateDigestForSessionId) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());

  DynamicModuleCertValidator validator(config_or_error.value(), stats_);

  bssl::ScopedEVP_MD_CTX md;
  EVP_DigestInit(md.get(), EVP_sha256());
  uint8_t hash_buffer[EVP_MAX_MD_SIZE];
  unsigned hash_length = 0;

  // Should not crash and should update the digest.
  EXPECT_NO_THROW(validator.updateDigestForSessionId(md, hash_buffer, hash_length));

  // Finalize and verify the digest is non-trivial.
  unsigned final_length = 0;
  EVP_DigestFinal(md.get(), hash_buffer, &final_length);
  EXPECT_GT(final_length, 0u);
}

TEST_F(DynamicModuleCertValidatorTest, UpdateDigestForSessionIdEmptyDigest) {
  auto config_or_error = createConfig("cert_validator_empty_digest");
  ASSERT_TRUE(config_or_error.ok());

  DynamicModuleCertValidator validator(config_or_error.value(), stats_);

  bssl::ScopedEVP_MD_CTX md;
  EVP_DigestInit(md.get(), EVP_sha256());
  uint8_t hash_buffer[EVP_MAX_MD_SIZE];
  unsigned hash_length = 0;

  // Should not crash even when the module returns empty digest data.
  EXPECT_NO_THROW(validator.updateDigestForSessionId(md, hash_buffer, hash_length));

  // Finalize and verify the digest is non-trivial (name and config are still hashed).
  unsigned final_length = 0;
  EVP_DigestFinal(md.get(), hash_buffer, &final_length);
  EXPECT_GT(final_length, 0u);
}

// =============================================================================
// Other CertValidator interface tests.
// =============================================================================

TEST_F(DynamicModuleCertValidatorTest, DaysUntilFirstCertExpiresReturnsNullopt) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());

  DynamicModuleCertValidator validator(config_or_error.value(), stats_);
  EXPECT_FALSE(validator.daysUntilFirstCertExpires().has_value());
}

TEST_F(DynamicModuleCertValidatorTest, GetCaFileNameReturnsEmpty) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());

  DynamicModuleCertValidator validator(config_or_error.value(), stats_);
  EXPECT_EQ("", validator.getCaFileName());
}

TEST_F(DynamicModuleCertValidatorTest, GetCaCertInformationReturnsNull) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());

  DynamicModuleCertValidator validator(config_or_error.value(), stats_);
  EXPECT_EQ(nullptr, validator.getCaCertInformation());
}

TEST_F(DynamicModuleCertValidatorTest, AddClientValidationContext) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());

  DynamicModuleCertValidator validator(config_or_error.value(), stats_);

  CSmartPtr<SSL_CTX, SSL_CTX_free> ssl_ctx(SSL_CTX_new(TLS_method()));
  // With require_client_cert = true.
  EXPECT_TRUE(validator.addClientValidationContext(ssl_ctx.get(), true).ok());
  EXPECT_EQ(SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
            SSL_CTX_get_verify_mode(ssl_ctx.get()));

  // With require_client_cert = false.
  CSmartPtr<SSL_CTX, SSL_CTX_free> ssl_ctx2(SSL_CTX_new(TLS_method()));
  EXPECT_TRUE(validator.addClientValidationContext(ssl_ctx2.get(), false).ok());
  EXPECT_EQ(SSL_VERIFY_PEER, SSL_CTX_get_verify_mode(ssl_ctx2.get()));
}

// =============================================================================
// Factory tests.
// =============================================================================

TEST_F(DynamicModuleCertValidatorTest, FactoryCreateCertValidator) {
  // Build a TypedExtensionConfig that wraps our DynamicModuleCertValidatorConfig proto.
  const std::string yaml = R"EOF(
name: envoy.tls.cert_validator.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_validator.dynamic_modules.v3.DynamicModuleCertValidatorConfig
  dynamic_module_config:
    name: cert_validator_no_op
  validator_name: test
)EOF";
  envoy::config::core::v3::TypedExtensionConfig typed_conf;
  TestUtility::loadFromYaml(yaml, typed_conf);
  TestCertificateValidationContextConfig validation_config(typed_conf);

  DynamicModuleCertValidatorFactory factory;
  EXPECT_EQ("envoy.tls.cert_validator.dynamic_modules", factory.name());

  auto result = factory.createCertValidator(&validation_config, stats_, factory_context_,
                                            *store_.rootScope());
  ASSERT_TRUE(result.ok());
  EXPECT_NE(result.value(), nullptr);
}

TEST_F(DynamicModuleCertValidatorTest, FactoryCreateCertValidatorWithValidatorConfig) {
  const std::string yaml = R"EOF(
name: envoy.tls.cert_validator.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_validator.dynamic_modules.v3.DynamicModuleCertValidatorConfig
  dynamic_module_config:
    name: cert_validator_no_op
  validator_name: test
  validator_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
    value: "some_config"
)EOF";
  envoy::config::core::v3::TypedExtensionConfig typed_conf;
  TestUtility::loadFromYaml(yaml, typed_conf);
  TestCertificateValidationContextConfig validation_config(typed_conf);

  DynamicModuleCertValidatorFactory factory;
  auto result = factory.createCertValidator(&validation_config, stats_, factory_context_,
                                            *store_.rootScope());
  ASSERT_TRUE(result.ok());
  EXPECT_NE(result.value(), nullptr);
}

TEST_F(DynamicModuleCertValidatorTest, FactoryCreateCertValidatorConfigNewFails) {
  const std::string yaml = R"EOF(
name: envoy.tls.cert_validator.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_validator.dynamic_modules.v3.DynamicModuleCertValidatorConfig
  dynamic_module_config:
    name: cert_validator_config_new_fail
  validator_name: test
)EOF";
  envoy::config::core::v3::TypedExtensionConfig typed_conf;
  TestUtility::loadFromYaml(yaml, typed_conf);
  TestCertificateValidationContextConfig validation_config(typed_conf);

  DynamicModuleCertValidatorFactory factory;
  auto result = factory.createCertValidator(&validation_config, stats_, factory_context_,
                                            *store_.rootScope());
  ASSERT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("Failed to initialize dynamic module cert validator config"));
}

TEST_F(DynamicModuleCertValidatorTest, FactoryCreateCertValidatorModuleNotFound) {
  const std::string yaml = R"EOF(
name: envoy.tls.cert_validator.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_validator.dynamic_modules.v3.DynamicModuleCertValidatorConfig
  dynamic_module_config:
    name: nonexistent_module
  validator_name: test
)EOF";
  envoy::config::core::v3::TypedExtensionConfig typed_conf;
  TestUtility::loadFromYaml(yaml, typed_conf);
  TestCertificateValidationContextConfig validation_config(typed_conf);

  DynamicModuleCertValidatorFactory factory;
  auto result = factory.createCertValidator(&validation_config, stats_, factory_context_,
                                            *store_.rootScope());
  ASSERT_FALSE(result.ok());
}

// =============================================================================
// Filter state callback tests.
// =============================================================================

TEST_F(DynamicModuleCertValidatorTest, FilterStateSetAndGet) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());
  auto& config = config_or_error.value();

  // Set up mock transport socket callbacks to provide filter state access.
  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks;

  config->current_callbacks_ = &transport_callbacks;

  const std::string key = "test.key";
  const std::string value = "test.value";

  bool ok = envoy_dynamic_module_callback_cert_validator_set_filter_state(
      static_cast<void*>(config.get()), {const_cast<char*>(key.data()), key.size()},
      {const_cast<char*>(value.data()), value.size()});
  EXPECT_TRUE(ok);

  // Verify by reading it back.
  envoy_dynamic_module_type_envoy_buffer result_buf;
  ok = envoy_dynamic_module_callback_cert_validator_get_filter_state(
      static_cast<void*>(config.get()), {const_cast<char*>(key.data()), key.size()}, &result_buf);
  EXPECT_TRUE(ok);
  EXPECT_EQ(value.size(), result_buf.length);
  EXPECT_EQ(value, std::string(result_buf.ptr, result_buf.length));

  config->current_callbacks_ = nullptr;
}

TEST_F(DynamicModuleCertValidatorTest, FilterStateGetNonExisting) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());
  auto& config = config_or_error.value();

  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks;
  config->current_callbacks_ = &transport_callbacks;

  const std::string key = "nonexistent.key";
  envoy_dynamic_module_type_envoy_buffer result_buf;
  bool ok = envoy_dynamic_module_callback_cert_validator_get_filter_state(
      static_cast<void*>(config.get()), {const_cast<char*>(key.data()), key.size()}, &result_buf);
  EXPECT_FALSE(ok);
  EXPECT_EQ(nullptr, result_buf.ptr);
  EXPECT_EQ(0, result_buf.length);

  config->current_callbacks_ = nullptr;
}

TEST_F(DynamicModuleCertValidatorTest, FilterStateSetNullCallbacks) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());
  auto& config = config_or_error.value();

  // current_callbacks_ is nullptr by default.
  const std::string key = "test.key";
  const std::string value = "test.value";

  bool ok = envoy_dynamic_module_callback_cert_validator_set_filter_state(
      static_cast<void*>(config.get()), {const_cast<char*>(key.data()), key.size()},
      {const_cast<char*>(value.data()), value.size()});
  EXPECT_FALSE(ok);
}

TEST_F(DynamicModuleCertValidatorTest, FilterStateGetNullCallbacks) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());
  auto& config = config_or_error.value();

  // current_callbacks_ is nullptr by default.
  const std::string key = "test.key";
  envoy_dynamic_module_type_envoy_buffer result_buf;
  bool ok = envoy_dynamic_module_callback_cert_validator_get_filter_state(
      static_cast<void*>(config.get()), {const_cast<char*>(key.data()), key.size()}, &result_buf);
  EXPECT_FALSE(ok);
  EXPECT_EQ(nullptr, result_buf.ptr);
  EXPECT_EQ(0, result_buf.length);
}

TEST_F(DynamicModuleCertValidatorTest, FilterStateSetNullKey) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());
  auto& config = config_or_error.value();

  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks;
  config->current_callbacks_ = &transport_callbacks;

  const std::string value = "test.value";
  bool ok = envoy_dynamic_module_callback_cert_validator_set_filter_state(
      static_cast<void*>(config.get()), {nullptr, 0},
      {const_cast<char*>(value.data()), value.size()});
  EXPECT_FALSE(ok);

  config->current_callbacks_ = nullptr;
}

TEST_F(DynamicModuleCertValidatorTest, FilterStateSetNullValue) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());
  auto& config = config_or_error.value();

  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks;
  config->current_callbacks_ = &transport_callbacks;

  const std::string key = "test.key";
  bool ok = envoy_dynamic_module_callback_cert_validator_set_filter_state(
      static_cast<void*>(config.get()), {const_cast<char*>(key.data()), key.size()}, {nullptr, 0});
  EXPECT_FALSE(ok);

  config->current_callbacks_ = nullptr;
}

TEST_F(DynamicModuleCertValidatorTest, FilterStateGetNullKey) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());
  auto& config = config_or_error.value();

  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks;
  config->current_callbacks_ = &transport_callbacks;

  envoy_dynamic_module_type_envoy_buffer result_buf;
  bool ok = envoy_dynamic_module_callback_cert_validator_get_filter_state(
      static_cast<void*>(config.get()), {nullptr, 0}, &result_buf);
  EXPECT_FALSE(ok);
  EXPECT_EQ(nullptr, result_buf.ptr);
  EXPECT_EQ(0, result_buf.length);

  config->current_callbacks_ = nullptr;
}

TEST_F(DynamicModuleCertValidatorTest, FilterStateSetEmptyValue) {
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());
  auto& config = config_or_error.value();

  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks;
  config->current_callbacks_ = &transport_callbacks;

  const std::string key = "test.key";
  const std::string empty_value = "";

  bool ok = envoy_dynamic_module_callback_cert_validator_set_filter_state(
      static_cast<void*>(config.get()), {const_cast<char*>(key.data()), key.size()},
      {const_cast<char*>(empty_value.data()), empty_value.size()});
  // Empty value has a non-null pointer but zero length, so ptr check passes.
  EXPECT_TRUE(ok);

  // Verify by reading it back.
  envoy_dynamic_module_type_envoy_buffer result_buf;
  ok = envoy_dynamic_module_callback_cert_validator_get_filter_state(
      static_cast<void*>(config.get()), {const_cast<char*>(key.data()), key.size()}, &result_buf);
  EXPECT_TRUE(ok);
  EXPECT_EQ(0, result_buf.length);

  config->current_callbacks_ = nullptr;
}

TEST_F(DynamicModuleCertValidatorTest, FilterStateViaDoVerifyCertChain) {
  // This test uses the cert_validator_filter_state C module which sets and reads
  // filter state during do_verify_cert_chain.
  auto config_or_error = createConfig("cert_validator_filter_state");
  ASSERT_TRUE(config_or_error.ok());

  DynamicModuleCertValidator validator(config_or_error.value(), stats_);

  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));

  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  sk_X509_push(cert_chain.get(), cert.release());

  CSmartPtr<SSL_CTX, SSL_CTX_free> ssl_ctx(SSL_CTX_new(TLS_method()));

  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks;
  CertValidator::ExtraValidationContext validation_context{&transport_callbacks};

  auto results = validator.doVerifyCertChain(*cert_chain, nullptr, nullptr, *ssl_ctx,
                                             validation_context, false, "example.com");
  EXPECT_EQ(ValidationResults::ValidationStatus::Successful, results.status);
  EXPECT_EQ(Envoy::Ssl::ClientValidationStatus::Validated, results.detailed_status);

  // Verify the filter state was set on the connection's stream info.
  const auto* accessor = transport_callbacks.connection_.streamInfo()
                             .filterState()
                             ->getDataReadOnly<Router::StringAccessor>("cert_validator.test_key");
  ASSERT_NE(nullptr, accessor);
  EXPECT_EQ("cert_validator.test_value", accessor->asString());
}

TEST_F(DynamicModuleCertValidatorTest, FilterStateCallbacksResetAfterVerify) {
  // Verify that current_callbacks_ is reset to nullptr after doVerifyCertChain returns.
  auto config_or_error = createConfig("cert_validator_no_op");
  ASSERT_TRUE(config_or_error.ok());

  DynamicModuleCertValidator validator(config_or_error.value(), stats_);

  bssl::UniquePtr<X509> cert = readCertFromFile(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_dns_cert.pem"));

  bssl::UniquePtr<STACK_OF(X509)> cert_chain(sk_X509_new_null());
  sk_X509_push(cert_chain.get(), cert.release());

  CSmartPtr<SSL_CTX, SSL_CTX_free> ssl_ctx(SSL_CTX_new(TLS_method()));

  NiceMock<Network::MockTransportSocketCallbacks> transport_callbacks;
  CertValidator::ExtraValidationContext validation_context{&transport_callbacks};

  validator.doVerifyCertChain(*cert_chain, nullptr, nullptr, *ssl_ctx, validation_context, false,
                              "example.com");

  // After the call, current_callbacks_ should be reset.
  EXPECT_EQ(nullptr, config_or_error.value()->current_callbacks_);
}

} // namespace
} // namespace DynamicModules
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
