#include <string>
#include <vector>

#include "envoy/admin/v3/certs.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.validate.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/common/base64.h"
#include "source/common/crypto/utility.h"
#include "source/common/json/json_loader.h"
#include "source/common/secret/sds_api.h"
#include "source/common/ssl/ssl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/tls/cert_validator/default_validator.h"
#include "source/common/tls/context_config_impl.h"
#include "source/common/tls/context_impl.h"
#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_ssl_socket.h"
#include "source/common/tls/utility.h"

#include "test/common/tls/ssl_certs_test.h"
#include "test/common/tls/ssl_test_utility.h"
#include "test/common/tls/test_data/no_san_cert_info.h"
#include "test/common/tls/test_data/san_dns3_cert_info.h"
#include "test/common/tls/test_data/san_ip_cert_info.h"
#include "test/common/tls/test_data/unittest_cert_info.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "openssl/crypto.h"
#include "openssl/x509v3.h"

using Envoy::Protobuf::util::MessageDifferencer;
using testing::EndsWith;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class ClientContextConfigImplTest : public SslCertsTest {
public:
  ABSL_MUST_USE_RESULT Cleanup cleanUpHelper(Envoy::Ssl::ClientContextSharedPtr& context) {
    return {[&manager = manager_, &context]() {
      if (context != nullptr) {
        manager.removeContext(context);
      }
    }};
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  ContextManagerImpl manager_{server_factory_context_};
};

// Validate that empty SNI (according to C string rules) fails config validation.
TEST_F(ClientContextConfigImplTest, EmptyServerNameIndication) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;

  tls_context.set_sni(std::string("\000", 1));
  EXPECT_EQ(ClientContextConfigImpl::create(tls_context, factory_context).status().message(),
            "SNI names containing NULL-byte are not allowed");
  tls_context.set_sni(std::string("a\000b", 3));
  EXPECT_EQ(ClientContextConfigImpl::create(tls_context, factory_context).status().message(),
            "SNI names containing NULL-byte are not allowed");
}

// Validate that it is an error configure `auto_sni_san_validation` without configuring
// a validation context.
TEST_F(ClientContextConfigImplTest, AutoSniSanValidationWithoutValidationContext) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  tls_context.set_auto_sni_san_validation(true);
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context);
  Stats::IsolatedStoreImpl store;
  EXPECT_EQ(manager_.createSslClientContext(*store.rootScope(), *client_context_config)
                .status()
                .message(),
            "'auto_sni_san_validation' was configured without a validation context");
}

// Validate that it is an error configure `auto_sni_san_validation` without configuring
// a trusted CA.
TEST_F(ClientContextConfigImplTest, AutoSniSanValidationWithoutTrustedCa) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  tls_context.set_auto_sni_san_validation(true);
  tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->set_trust_chain_verification(envoy::extensions::transport_sockets::tls::v3::
                                         CertificateValidationContext::ACCEPT_UNTRUSTED);
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context);
  Stats::IsolatedStoreImpl store;
  EXPECT_EQ(manager_.createSslClientContext(*store.rootScope(), *client_context_config)
                .status()
                .message(),
            "'auto_sni_san_validation' was configured without configuring a trusted CA");
}

// Validate that values other than a hex-encoded SHA-256 fail config validation.
TEST_F(ClientContextConfigImplTest, InvalidCertificateHash) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;
  tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      // This is valid hex-encoded string, but it doesn't represent SHA-256 (80 vs 64 chars).
      ->add_verify_certificate_hash("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                                    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context);
  Stats::IsolatedStoreImpl store;
  EXPECT_THAT(manager_.createSslClientContext(*store.rootScope(), *client_context_config)
                  .status()
                  .message(),
              testing::MatchesRegex("Invalid hex-encoded SHA-256 .*"));
}

// Validate that values other than a base64-encoded SHA-256 fail config validation.
TEST_F(ClientContextConfigImplTest, InvalidCertificateSpki) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;
  tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      // Not a base64-encoded string.
      ->add_verify_certificate_spki("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context);
  Stats::IsolatedStoreImpl store;
  EXPECT_THAT(manager_.createSslClientContext(*store.rootScope(), *client_context_config)
                  .status()
                  .message(),
              testing::MatchesRegex("Invalid base64-encoded SHA-256 .*"));
}

// Validate that 2048-bit RSA certificates load successfully.
TEST_F(ClientContextConfigImplTest, RSA2048Cert) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Stats::IsolatedStoreImpl store;
  auto context_or = manager_.createSslClientContext(*store.rootScope(), *client_context_config);
  EXPECT_OK(context_or);
  auto cleanup = cleanUpHelper(*context_or);
}

// Validate that 1024-bit RSA certificates are rejected.
TEST_F(ClientContextConfigImplTest, RSA1024Cert) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_rsa_1024_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_rsa_1024_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Stats::IsolatedStoreImpl store;

  std::string error_msg(absl::StrCat(
      "Failed to load certificate chain from .*selfsigned_rsa_1024_cert.pem, only RSA "
      "certificates ",
      (FIPS_mode() ? "with 2048-bit, 3072-bit or 4096-bit keys are supported in FIPS mode"
                   : "with 2048-bit or larger keys are supported")));
  EXPECT_THAT(manager_.createSslClientContext(*store.rootScope(), *client_context_config)
                  .status()
                  .message(),
              testing::MatchesRegex(error_msg));
}

// Validate that 1024-bit RSA certificates are rejected from `pkcs12`.
TEST_F(ClientContextConfigImplTest, RSA1024Pkcs12) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  pkcs12:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_rsa_1024_certkey.p12"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Stats::IsolatedStoreImpl store;

  std::string error_msg(absl::StrCat(
      "Failed to load certificate chain from .*selfsigned_rsa_1024_certkey.p12, "
      "only RSA certificates ",
      (FIPS_mode() ? "with 2048-bit, 3072-bit or 4096-bit keys are supported in FIPS mode"
                   : "with 2048-bit or larger keys are supported")));
  EXPECT_THAT(manager_.createSslClientContext(*store.rootScope(), *client_context_config)
                  .status()
                  .message(),
              testing::MatchesRegex(error_msg));
}

// Validate that 3072-bit RSA certificates load successfully.
TEST_F(ClientContextConfigImplTest, RSA3072Cert) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_rsa_3072_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_rsa_3072_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);
  ContextManagerImpl manager(server_factory_context_);
  Stats::IsolatedStoreImpl store;
  auto context_or = manager_.createSslClientContext(*store.rootScope(), *client_context_config);
  EXPECT_OK(context_or);
  auto cleanup = cleanUpHelper(*context_or);
}

// Validate that 4096-bit RSA certificates load successfully.
TEST_F(ClientContextConfigImplTest, RSA4096Cert) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_rsa_4096_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_rsa_4096_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Stats::IsolatedStoreImpl store;
  auto context_or = manager_.createSslClientContext(*store.rootScope(), *client_context_config);
  EXPECT_OK(context_or);
  auto cleanup = cleanUpHelper(*context_or);
}

// Validate that P256 ECDSA certs load.
TEST_F(ClientContextConfigImplTest, P256EcdsaCert) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p256_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Stats::IsolatedStoreImpl store;
  auto context_or = manager_.createSslClientContext(*store.rootScope(), *client_context_config);
  EXPECT_OK(context_or);
  auto cleanup = cleanUpHelper(*context_or);
}

// Validate that P384 ECDSA certs load.
TEST_F(ClientContextConfigImplTest, P384EcdsaCert) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p384_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p384_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Stats::IsolatedStoreImpl store;
  auto context_or = manager_.createSslClientContext(*store.rootScope(), *client_context_config);
  EXPECT_OK(context_or);
  auto cleanup = cleanUpHelper(*context_or);
}

// Validate that P384 ECDSA certs are loaded from `pkcs12`.
TEST_F(ClientContextConfigImplTest, P384EcdsaPkcs12) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  pkcs12:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p384_certkey.p12"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Stats::IsolatedStoreImpl store;
}

TEST_F(ClientContextConfigImplTest, P521EcdsaCert) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p521_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_ecdsa_p521_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Stats::IsolatedStoreImpl store;
  auto context_or = manager_.createSslClientContext(*store.rootScope(), *client_context_config);
  EXPECT_OK(context_or);
  auto cleanup = cleanUpHelper(*context_or);
}

// Validate that a P-224 key will cause an error.
TEST_F(ClientContextConfigImplTest, UnsupportedCurveEcdsaCert) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_secp224r1_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_secp224r1_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);
  Stats::IsolatedStoreImpl store;
  // Envoy has logic to reject P-224, but newer versions of BoringSSL reject it in `SSL_CTX`
  // before Envoy's logic runs. This test expectation is written to accept both paths.
  EXPECT_THAT(manager_.createSslClientContext(*store.rootScope(), *client_context_config)
                  .status()
                  .message(),
              testing::ContainsRegex(
                  "Failed to load certificate chain from .*selfsigned_secp224r1_cert.pem"));
}

// Multiple TLS certificates are not yet supported.
// TODO(PiotrSikora): Support multiple TLS certificates.
TEST_F(ClientContextConfigImplTest, MultipleTlsCertificates) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  EXPECT_EQ(ClientContextConfigImpl::create(tls_context, factory_context_).status().message(),
            "Multiple TLS certificates are not supported for client contexts");
}

// Validate context config does not support handling both static TLS certificate and dynamic TLS
// certificate.
TEST_F(ClientContextConfigImplTest, TlsCertificatesAndSdsConfig) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  const std::string tls_certificate_yaml = R"EOF(
  certificate_chain:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            *tls_context.mutable_common_tls_context()->add_tls_certificates());
  tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs();
  EXPECT_EQ(ClientContextConfigImpl::create(tls_context, factory_context_).status().message(),
            "Multiple TLS certificates are not supported for client contexts");
}

// Validate context config supports SDS, and is marked as not ready if secrets are not yet
// downloaded.
TEST_F(ClientContextConfigImplTest, SecretNotReady) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_CALL(factory_context_.server_context_, localInfo()).WillOnce(ReturnRef(local_info));
  EXPECT_CALL(factory_context_, initManager()).WillRepeatedly(ReturnRef(init_manager));
  EXPECT_CALL(factory_context_.server_context_, mainThreadDispatcher())
      .WillRepeatedly(ReturnRef(dispatcher));
  auto sds_secret_configs =
      tls_context.mutable_common_tls_context()->mutable_tls_certificate_sds_secret_configs()->Add();
  sds_secret_configs->set_name("abc.com");
  sds_secret_configs->mutable_sds_config();
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);
  // When sds secret is not downloaded, config is not ready.
  EXPECT_FALSE(client_context_config->isReady());
  // Set various callbacks to config.
  NiceMock<Secret::MockSecretCallbacks> secret_callback;
  client_context_config->setSecretUpdateCallback(
      [&secret_callback]() { return secret_callback.onAddOrUpdateSecret(); });
  client_context_config->setSecretUpdateCallback([]() { return absl::OkStatus(); });
}

// Validate client context config supports SDS, and is marked as not ready if dynamic
// certificate validation context is not yet downloaded.
TEST_F(ClientContextConfigImplTest, ValidationContextNotReady) {
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* client_cert =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  client_cert->mutable_certificate_chain()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"));
  client_cert->mutable_private_key()->set_filename(TestEnvironment::substitute(
      "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"));
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Init::MockManager> init_manager;
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_CALL(factory_context_.server_context_, localInfo()).WillOnce(ReturnRef(local_info));
  EXPECT_CALL(factory_context_, initManager()).WillRepeatedly(ReturnRef(init_manager));
  EXPECT_CALL(factory_context_.server_context_, mainThreadDispatcher())
      .WillRepeatedly(ReturnRef(dispatcher));
  auto sds_secret_configs =
      tls_context.mutable_common_tls_context()->mutable_validation_context_sds_secret_config();
  sds_secret_configs->set_name("abc.com");
  sds_secret_configs->mutable_sds_config();
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);
  // When sds secret is not downloaded, config is not ready.
  EXPECT_FALSE(client_context_config->isReady());
  // Set various callbacks to config.
  NiceMock<Secret::MockSecretCallbacks> secret_callback;
  client_context_config->setSecretUpdateCallback(
      [&secret_callback]() { return secret_callback.onAddOrUpdateSecret(); });
  client_context_config->setSecretUpdateCallback([]() { return absl::OkStatus(); });
}

// Validate that client context config with static TLS certificates is created successfully.
TEST_F(ClientContextConfigImplTest, StaticTlsCertificates) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;

  const std::string yaml = R"EOF(
name: "abc.com"
tls_certificate:
  certificate_chain:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
)EOF";

  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_sds_secret_configs()
      ->Add()
      ->set_name("abc.com");

  EXPECT_OK(factory_context_.server_context_.secretManager().addStaticSecret(secret_config));
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);

  const std::string cert_pem = "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
            client_context_config->tlsCertificates()[0].get().certificateChain());
  const std::string key_pem = "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(key_pem)),
            client_context_config->tlsCertificates()[0].get().privateKey());
}

// Validate that client context config with password-protected TLS certificates is created
// successfully.
TEST_F(ClientContextConfigImplTest, PasswordProtectedTlsCertificates) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;
  secret_config.set_name("abc.com");

  auto* tls_certificate = secret_config.mutable_tls_certificate();
  tls_certificate->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_cert.pem"));
  tls_certificate->mutable_private_key()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_key.pem"));
  tls_certificate->mutable_password()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_password.txt"));

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_sds_secret_configs()
      ->Add()
      ->set_name("abc.com");

  EXPECT_OK(factory_context_.server_context_.secretManager().addStaticSecret(secret_config));
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);

  const std::string cert_pem = "{{ test_rundir "
                               "}}/test/common/tls/test_data/password_protected_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
            client_context_config->tlsCertificates()[0].get().certificateChain());
  const std::string key_pem = "{{ test_rundir "
                              "}}/test/common/tls/test_data/password_protected_key.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(key_pem)),
            client_context_config->tlsCertificates()[0].get().privateKey());
  const std::string password_file = "{{ test_rundir "
                                    "}}/test/common/tls/test_data/password_protected_password.txt";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(password_file)),
            client_context_config->tlsCertificates()[0].get().password());
}

// Validate that client context config with password-protected TLS certificates loaded from
// `PKCS12` is created successfully.
TEST_F(ClientContextConfigImplTest, PasswordProtectedPkcs12) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;
  secret_config.set_name("abc.com");

  auto* tls_certificate = secret_config.mutable_tls_certificate();
  tls_certificate->mutable_pkcs12()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_certkey.p12"));
  tls_certificate->mutable_password()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_password.txt"));

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_sds_secret_configs()
      ->Add()
      ->set_name("abc.com");

  EXPECT_OK(factory_context_.server_context_.secretManager().addStaticSecret(secret_config));
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);

  const std::string cert_p12 = "{{ test_rundir "
                               "}}/test/common/tls/test_data/password_protected_certkey.p12";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_p12)),
            client_context_config->tlsCertificates()[0].get().pkcs12());
  const std::string password_file = "{{ test_rundir "
                                    "}}/test/common/tls/test_data/password_protected_password.txt";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(password_file)),
            client_context_config->tlsCertificates()[0].get().password());
}

// Validate that not supplying the incorrect passphrase for password-protected `PKCS12`
// triggers a failure loading the private key.
TEST_F(ClientContextConfigImplTest, PasswordWrongPkcs12) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;
  secret_config.set_name("abc.com");

  auto* tls_certificate = secret_config.mutable_tls_certificate();
  const std::string pkcs12_path =
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_certkey.p12");
  tls_certificate->mutable_pkcs12()->set_filename(pkcs12_path);
  tls_certificate->mutable_password()->set_inline_string("WrongPassword");

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_sds_secret_configs()
      ->Add()
      ->set_name("abc.com");

  EXPECT_OK(factory_context_.server_context_.secretManager().addStaticSecret(secret_config));
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);

  Stats::IsolatedStoreImpl store;
  EXPECT_EQ(manager_.createSslClientContext(*store.rootScope(), *client_context_config)
                .status()
                .message(),
            absl::StrCat("Failed to load pkcs12 from ", pkcs12_path));
}

// Validate that not supplying a passphrase for password-protected `PKCS12`
// triggers a failure loading the private key.
TEST_F(ClientContextConfigImplTest, PasswordNotSuppliedPkcs12) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;
  secret_config.set_name("abc.com");

  auto* tls_certificate = secret_config.mutable_tls_certificate();
  const std::string pkcs12_path =
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_certkey.p12");
  tls_certificate->mutable_pkcs12()->set_filename(pkcs12_path);
  // Don't supply the password.

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_sds_secret_configs()
      ->Add()
      ->set_name("abc.com");

  EXPECT_OK(factory_context_.server_context_.secretManager().addStaticSecret(secret_config));
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);

  Stats::IsolatedStoreImpl store;
  EXPECT_EQ(manager_.createSslClientContext(*store.rootScope(), *client_context_config)
                .status()
                .message(),
            absl::StrCat("Failed to load pkcs12 from ", pkcs12_path));
}

// Validate that not supplying a passphrase for password-protected TLS certificates
// triggers a failure.
TEST_F(ClientContextConfigImplTest, PasswordNotSuppliedTlsCertificates) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;
  secret_config.set_name("abc.com");

  auto* tls_certificate = secret_config.mutable_tls_certificate();
  tls_certificate->mutable_certificate_chain()->set_filename(
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_cert.pem"));
  const std::string private_key_path =
      TestEnvironment::substitute("{{ test_rundir "
                                  "}}/test/common/tls/test_data/password_protected_key.pem");
  tls_certificate->mutable_private_key()->set_filename(private_key_path);
  // Don't supply the password.

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_sds_secret_configs()
      ->Add()
      ->set_name("abc.com");

  EXPECT_OK(factory_context_.server_context_.secretManager().addStaticSecret(secret_config));
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);

  Stats::IsolatedStoreImpl store;
  EXPECT_THAT(
      manager_.createSslClientContext(*store.rootScope(), *client_context_config)
          .status()
          .message(),
      testing::ContainsRegex(absl::StrCat("Failed to load private key from ", private_key_path)));
}

// Validate that client context config with static certificate validation context is created
// successfully.
TEST_F(ClientContextConfigImplTest, StaticCertificateValidationContext) {
  envoy::extensions::transport_sockets::tls::v3::Secret tls_certificate_secret_config;
  const std::string tls_certificate_yaml = R"EOF(
  name: "abc.com"
  tls_certificate:
    certificate_chain:
      filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
    private_key:
      filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            tls_certificate_secret_config);
  EXPECT_TRUE(factory_context_.server_context_.secretManager()
                  .addStaticSecret(tls_certificate_secret_config)
                  .ok());
  envoy::extensions::transport_sockets::tls::v3::Secret
      certificate_validation_context_secret_config;
  const std::string certificate_validation_context_yaml = R"EOF(
    name: "def.com"
    validation_context:
      trusted_ca: { filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem" }
      allow_expired_certificate: true
  )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(certificate_validation_context_yaml),
                            certificate_validation_context_secret_config);
  EXPECT_TRUE(factory_context_.server_context_.secretManager()
                  .addStaticSecret(certificate_validation_context_secret_config)
                  .ok());

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_sds_secret_configs()
      ->Add()
      ->set_name("abc.com");
  tls_context.mutable_common_tls_context()
      ->mutable_validation_context_sds_secret_config()
      ->set_name("def.com");
  auto client_context_config = *ClientContextConfigImpl::create(tls_context, factory_context_);

  const std::string cert_pem = "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
            client_context_config->certificateValidationContext()->caCert());
}

// Validate that constructor of client context config throws an exception when static TLS
// certificate is missing.
TEST_F(ClientContextConfigImplTest, MissingStaticSecretTlsCertificates) {
  envoy::extensions::transport_sockets::tls::v3::Secret secret_config;

  const std::string yaml = R"EOF(
name: "abc.com"
tls_certificate:
  certificate_chain:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
  private_key:
    filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
)EOF";

  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), secret_config);

  EXPECT_OK(factory_context_.server_context_.secretManager().addStaticSecret(secret_config));

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_sds_secret_configs()
      ->Add()
      ->set_name("missing");

  EXPECT_EQ(ClientContextConfigImpl::create(tls_context, factory_context_).status().message(),
            "Unknown static secret: missing");
}

// Validate that constructor of client context config throws an exception when static certificate
// validation context is missing.
TEST_F(ClientContextConfigImplTest, MissingStaticCertificateValidationContext) {
  envoy::extensions::transport_sockets::tls::v3::Secret tls_certificate_secret_config;
  const std::string tls_certificate_yaml = R"EOF(
    name: "abc.com"
    tls_certificate:
      certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_key.pem"
    )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(tls_certificate_yaml),
                            tls_certificate_secret_config);
  EXPECT_TRUE(factory_context_.server_context_.secretManager()
                  .addStaticSecret(tls_certificate_secret_config)
                  .ok());
  envoy::extensions::transport_sockets::tls::v3::Secret
      certificate_validation_context_secret_config;
  const std::string certificate_validation_context_yaml = R"EOF(
      name: "def.com"
      validation_context:
        trusted_ca: { filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem" }
        allow_expired_certificate: true
    )EOF";
  TestUtility::loadFromYaml(TestEnvironment::substitute(certificate_validation_context_yaml),
                            certificate_validation_context_secret_config);
  EXPECT_TRUE(factory_context_.server_context_.secretManager()
                  .addStaticSecret(certificate_validation_context_secret_config)
                  .ok());

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()
      ->mutable_tls_certificate_sds_secret_configs()
      ->Add()
      ->set_name("abc.com");
  tls_context.mutable_common_tls_context()
      ->mutable_validation_context_sds_secret_config()
      ->set_name("missing");
  EXPECT_EQ(ClientContextConfigImpl::create(tls_context, factory_context_).status().message(),
            "Unknown static certificate validation context: missing");
}

// Verify that an invalid validator factory is rejected.
TEST_F(ClientContextConfigImplTest, TestCertValidatorFactoryNotFound) {
  const std::string yaml = R"EOF(
  common_tls_context:
    validation_context:
      custom_validator_config:
        name: "unknown_cert_validator"
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Empty
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  EXPECT_EQ(manager_.createSslClientContext(*store_.rootScope(), *cfg).status().message(),
            "Failed to get certificate validator factory for unknown_cert_validator");
}

// Verify that an invalid ECDH curves are rejected.
TEST_F(ClientContextConfigImplTest, TestInvalidEcdhCurves) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_params:
      ecdh_curves: "invalid_curve"
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  EXPECT_EQ(manager_.createSslClientContext(*store_.rootScope(), *cfg).status().message(),
            "Failed to initialize ECDH curves invalid_curve");
}

// Verify that an invalid key log path is rejected.
TEST_F(ClientContextConfigImplTest, TestInvalidKeyLogPath) {
  const std::string yaml = R"EOF(
  common_tls_context:
    key_log:
      path: "/non_existent_directory/key.log"
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  EXPECT_CALL(factory_context_.server_context_.access_log_manager_, createAccessLog(_))
      .WillOnce(Return(absl::InvalidArgumentError("Failed to create log file")));

  EXPECT_EQ(manager_.createSslClientContext(*store_.rootScope(), *cfg).status().message(),
            "Failed to create log file");
}

// Verify that a long ALPN is rejected.
TEST_F(ClientContextConfigImplTest, TestInvalidAlpnTooLong) {
  const std::string long_protocol(65535, 'a'); // >= 65535 chars
  const std::string yaml = fmt::format(R"EOF(
  common_tls_context:
    alpn_protocols: "{}"
  )EOF",
                                       long_protocol);

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  EXPECT_EQ(manager_.createSslClientContext(*store_.rootScope(), *cfg).status().message(),
            "Invalid ALPN protocol string");
}

// Verify that invalid signature algorithms are rejected.
TEST_F(ClientContextConfigImplTest, TestInvalidSignatureAlgorithms) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_params:
      signature_algorithms: "invalid_sigalg"
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  EXPECT_EQ(manager_.createSslClientContext(*store_.rootScope(), *cfg).status().message(),
            "Failed to initialize TLS signature algorithms invalid_sigalg");
}

// Verify that a corrupt certificate chain is rejected.
TEST_F(ClientContextConfigImplTest, TestLoadCorruptCert) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        inline_string: "invalid_cert_data"
      private_key:
        inline_string: "invalid_key_data"
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  EXPECT_EQ(manager_.createSslClientContext(*store_.rootScope(), *cfg).status().message(),
            "Failed to load certificate chain from <inline>");
}

// Verify that a corrupt private key is rejected.
TEST_F(ClientContextConfigImplTest, TestLoadCorruptKey) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/common/tls/test_data/selfsigned_cert.pem"
      private_key:
        inline_string: "invalid_key_data"
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  EXPECT_THAT(manager_.createSslClientContext(*store_.rootScope(), *cfg).status().message(),
              testing::HasSubstr("Failed to load private key from <inline>"));
}

// Verify that a corrupt PKCS12 file is rejected.
TEST_F(ClientContextConfigImplTest, TestLoadCorruptPkcs12) {
  const std::string yaml = R"EOF(
  common_tls_context:
    tls_certificates:
    - pkcs12:
        inline_string: "invalid_pkcs12_data"
  )EOF";

  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
  TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
  auto cfg = *ClientContextConfigImpl::create(tls_context, factory_context_);
  EXPECT_EQ(manager_.createSslClientContext(*store_.rootScope(), *cfg).status().message(),
            "Failed to load pkcs12 from <inline>");
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
