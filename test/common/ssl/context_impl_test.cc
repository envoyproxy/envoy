#include <string>
#include <vector>

#include "common/json/json_loader.h"
#include "common/ssl/context_config_impl.h"
#include "common/ssl/context_impl.h"
#include "common/stats/stats_impl.h"

#include "test/common/ssl/ssl_certs_test.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Ssl {

class SslContextImplTest : public SslCertsTest {};

TEST_F(SslContextImplTest, TestdNSNameMatching) {
  EXPECT_TRUE(ContextImpl::dNSNameMatch("lyft.com", "lyft.com"));
  EXPECT_TRUE(ContextImpl::dNSNameMatch("a.lyft.com", "*.lyft.com"));
  EXPECT_TRUE(ContextImpl::dNSNameMatch("a.b.lyft.com", "*.lyft.com"));
  EXPECT_FALSE(ContextImpl::dNSNameMatch("foo.test.com", "*.lyft.com"));
  EXPECT_FALSE(ContextImpl::dNSNameMatch("lyft.com", "*.lyft.com"));
  EXPECT_FALSE(ContextImpl::dNSNameMatch("alyft.com", "*.lyft.com"));
  EXPECT_FALSE(ContextImpl::dNSNameMatch("alyft.com", "*lyft.com"));
  EXPECT_FALSE(ContextImpl::dNSNameMatch("lyft.com", "*lyft.com"));
  EXPECT_FALSE(ContextImpl::dNSNameMatch("", "*lyft.com"));
  EXPECT_FALSE(ContextImpl::dNSNameMatch("lyft.com", ""));
}

TEST_F(SslContextImplTest, TestVerifySubjectAltNameDNSMatched) {
  FILE* fp = fopen(
      TestEnvironment::runfilesPath("test/common/ssl/test_data/san_dns_cert.pem").c_str(), "r");
  EXPECT_NE(fp, nullptr);
  X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
  EXPECT_NE(cert, nullptr);
  std::vector<std::string> verify_subject_alt_name_list = {"server1.example.com",
                                                           "server2.example.com"};
  EXPECT_TRUE(ContextImpl::verifySubjectAltName(cert, verify_subject_alt_name_list));
  X509_free(cert);
  fclose(fp);
}

TEST_F(SslContextImplTest, TestVerifySubjectAltNameURIMatched) {
  FILE* fp = fopen(
      TestEnvironment::runfilesPath("test/common/ssl/test_data/san_uri_cert.pem").c_str(), "r");
  EXPECT_NE(fp, nullptr);
  X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
  EXPECT_NE(cert, nullptr);
  std::vector<std::string> verify_subject_alt_name_list = {"spiffe://lyft.com/fake-team",
                                                           "spiffe://lyft.com/test-team"};
  EXPECT_TRUE(ContextImpl::verifySubjectAltName(cert, verify_subject_alt_name_list));
  X509_free(cert);
  fclose(fp);
}

TEST_F(SslContextImplTest, TestVerifySubjectAltNameNotMatched) {
  FILE* fp = fopen(
      TestEnvironment::runfilesPath("test/common/ssl/test_data/san_dns_cert.pem").c_str(), "r");
  EXPECT_NE(fp, nullptr);
  X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
  EXPECT_NE(cert, nullptr);
  std::vector<std::string> verify_subject_alt_name_list = {"foo", "bar"};
  EXPECT_FALSE(ContextImpl::verifySubjectAltName(cert, verify_subject_alt_name_list));
  X509_free(cert);
  fclose(fp);
}

TEST_F(SslContextImplTest, TestCipherSuites) {
  std::string json = R"EOF(
  {
    "cipher_suites": "AES128-SHA:BOGUS:AES256-SHA"
  }
  )EOF";

  Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(json);
  ClientContextConfigImpl cfg(*loader);
  Runtime::MockLoader runtime;
  ContextManagerImpl manager(runtime);
  Stats::IsolatedStoreImpl store;
  EXPECT_THROW(manager.createSslClientContext(store, cfg), EnvoyException);
}

TEST_F(SslContextImplTest, TestExpiringCert) {
  std::string json = R"EOF(
  {
      "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
      "private_key_file": "{{ test_tmpdir }}/unittestkey.pem"
  }
  )EOF";

  Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(json);
  ClientContextConfigImpl cfg(*loader);
  Runtime::MockLoader runtime;
  ContextManagerImpl manager(runtime);
  Stats::IsolatedStoreImpl store;
  ClientContextPtr context(manager.createSslClientContext(store, cfg));

  // This is a total hack, but right now we generate the cert and it expires in 15 days only in the
  // first second that it's valid. This can become invalid and then cause slower tests to fail.
  // Optimally we would make the cert valid for 15 days and 23 hours, but that is not easy to do
  // with the command line so we have this for now. Good enough.
  EXPECT_TRUE(15 == context->daysUntilFirstCertExpires() ||
              14 == context->daysUntilFirstCertExpires());
}

TEST_F(SslContextImplTest, TestExpiredCert) {
  std::string json = R"EOF(
  {
      "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/expired_cert.pem",
      "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/expired_key.pem"
  }
  )EOF";

  Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(json);
  ClientContextConfigImpl cfg(*loader);
  Runtime::MockLoader runtime;
  ContextManagerImpl manager(runtime);
  Stats::IsolatedStoreImpl store;
  ClientContextPtr context(manager.createSslClientContext(store, cfg));
  EXPECT_EQ(0U, context->daysUntilFirstCertExpires());
}

TEST_F(SslContextImplTest, TestGetCertInformation) {
  std::string json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem"
  }
  )EOF";

  Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(json);
  ClientContextConfigImpl cfg(*loader);
  Runtime::MockLoader runtime;
  ContextManagerImpl manager(runtime);
  Stats::IsolatedStoreImpl store;

  ClientContextPtr context(manager.createSslClientContext(store, cfg));
  // This is similar to the hack above, but right now we generate the ca_cert and it expires in 15
  // days only in the first second that it's valid. We will partially match for up until Days until
  // Expiration: 1.
  // For the cert_chain, it is dynamically created when we run_envoy_test.sh which changes the
  // serial number with
  // every build. For cert_chain output, we check only for the certificate path.
  std::string ca_cert_partial_output(TestEnvironment::substitute(
      "Certificate Path: {{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem, Serial Number: "
      "eaf3b0ea1d0e579a, "
      "Days until Expiration: "));
  std::string cert_chain_partial_output(
      TestEnvironment::substitute("Certificate Path: {{ test_tmpdir }}/unittestcert.pem"));

  EXPECT_TRUE(context->getCaCertInformation().find(ca_cert_partial_output) != std::string::npos);
  EXPECT_TRUE(context->getCertChainInformation().find(cert_chain_partial_output) !=
              std::string::npos);
}

TEST_F(SslContextImplTest, TestNoCert) {
  Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString("{}");
  ClientContextConfigImpl cfg(*loader);
  Runtime::MockLoader runtime;
  ContextManagerImpl manager(runtime);
  Stats::IsolatedStoreImpl store;
  ClientContextPtr context(manager.createSslClientContext(store, cfg));
  EXPECT_EQ("", context->getCaCertInformation());
  EXPECT_EQ("", context->getCertChainInformation());
}

class SslServerContextImplTicketTest : public SslContextImplTest {
public:
  static void loadConfig(ServerContextConfigImpl& cfg) {
    Runtime::MockLoader runtime;
    ContextManagerImpl manager(runtime);
    Stats::IsolatedStoreImpl store;
    ServerContextPtr server_ctx(
        manager.createSslServerContext(store, cfg, std::vector<std::string>{}));
  }

  static void loadConfigV2(envoy::api::v2::auth::DownstreamTlsContext& cfg) {
    // Must add a certificate for the config to be considered valid.
    envoy::api::v2::auth::TlsCertificate* server_cert =
        cfg.mutable_common_tls_context()->add_tls_certificates();
    server_cert->mutable_certificate_chain()->set_filename(
        TestEnvironment::substitute("{{ test_tmpdir }}/unittestcert.pem"));
    server_cert->mutable_private_key()->set_filename(
        TestEnvironment::substitute("{{ test_tmpdir }}/unittestkey.pem"));

    ServerContextConfigImpl server_context_config(cfg);
    loadConfig(server_context_config);
  }

  static void loadConfigJson(const std::string& json) {
    Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(json);
    ServerContextConfigImpl cfg(*loader);
    loadConfig(cfg);
  }
};

TEST_F(SslServerContextImplTicketTest, TicketKeySuccess) {
  // Both keys are valid; no error should be thrown
  std::string json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "session_ticket_key_paths": [
      "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a",
      "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_b"
    ]
  }
  )EOF";

  EXPECT_NO_THROW(loadConfigJson(json));
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInvalidLen) {
  // First key is valid, second key isn't. Should throw if any keys are invalid.
  std::string json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "session_ticket_key_paths": [
      "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_a",
      "{{ test_rundir }}/test/common/ssl/test_data/ticket_key_wrong_len"
    ]
  }
  )EOF";

  EXPECT_THROW(loadConfigJson(json), EnvoyException);
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInvalidCannotRead) {
  std::string json = R"EOF(
  {
    "cert_chain_file": "{{ test_tmpdir }}/unittestcert.pem",
    "private_key_file": "{{ test_tmpdir }}/unittestkey.pem",
    "session_ticket_key_paths": [
      "{{ test_rundir }}/test/common/ssl/test_data/this_file_does_not_exist"
    ]
  }
  )EOF";

  EXPECT_THROW(loadConfigJson(json), std::exception);
}

TEST_F(SslServerContextImplTicketTest, TicketKeyNone) {
  envoy::api::v2::auth::DownstreamTlsContext cfg;
  EXPECT_NO_THROW(loadConfigV2(cfg));
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInlineBytesSuccess) {
  envoy::api::v2::auth::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys()->add_keys()->set_inline_bytes(std::string(80, '\0'));
  EXPECT_NO_THROW(loadConfigV2(cfg));
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInlineStringSuccess) {
  envoy::api::v2::auth::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys()->add_keys()->set_inline_string(std::string(80, '\0'));
  EXPECT_NO_THROW(loadConfigV2(cfg));
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInlineBytesFailTooBig) {
  envoy::api::v2::auth::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys()->add_keys()->set_inline_bytes(std::string(81, '\0'));
  EXPECT_THROW(loadConfigV2(cfg), EnvoyException);
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInlineStringFailTooBig) {
  envoy::api::v2::auth::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys()->add_keys()->set_inline_string(std::string(81, '\0'));
  EXPECT_THROW(loadConfigV2(cfg), EnvoyException);
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInlineBytesFailTooSmall) {
  envoy::api::v2::auth::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys()->add_keys()->set_inline_bytes(std::string(79, '\0'));
  EXPECT_THROW(loadConfigV2(cfg), EnvoyException);
}

TEST_F(SslServerContextImplTicketTest, TicketKeyInlineStringFailTooSmall) {
  envoy::api::v2::auth::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys()->add_keys()->set_inline_string(std::string(79, '\0'));
  EXPECT_THROW(loadConfigV2(cfg), EnvoyException);
}

TEST_F(SslServerContextImplTicketTest, TicketKeySdsFail) {
  envoy::api::v2::auth::DownstreamTlsContext cfg;
  cfg.mutable_session_ticket_keys_sds_secret_config();
  EXPECT_THROW_WITH_MESSAGE(loadConfigV2(cfg), EnvoyException, "SDS not supported yet");
}

TEST_F(SslServerContextImplTicketTest, CRLSuccess) {
  std::string json = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem",
    "crl_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.crl"
  }
  )EOF";

  EXPECT_NO_THROW(loadConfigJson(json));
}

TEST_F(SslServerContextImplTicketTest, CRLInvalid) {
  std::string json = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem",
    "ca_cert_file": "{{ test_rundir }}/test/common/ssl/test_data/ca_cert.pem",
    "crl_file": "{{ test_rundir }}/test/common/ssl/test_data/not_a_crl.crl"
  }
  )EOF";

  EXPECT_THROW_WITH_REGEX(loadConfigJson(json), EnvoyException,
                          "^Failed to load CRL from .*/not_a_crl.crl$");
}

TEST_F(SslServerContextImplTicketTest, CRLWithNoCA) {
  std::string json = R"EOF(
  {
    "cert_chain_file": "{{ test_rundir }}/test/common/ssl/test_data/san_dns_cert.pem",
    "private_key_file": "{{ test_rundir }}/test/common/ssl/test_data/san_dns_key.pem",
    "crl_file": "{{ test_rundir }}/test/common/ssl/test_data/not_a_crl.crl"
  }
  )EOF";

  EXPECT_THROW_WITH_REGEX(loadConfigJson(json), EnvoyException,
                          "^Failed to load CRL from .* without trusted CA certificates$");
}

// Validate that empty SNI (according to C string rules) fails config validation.
TEST(ClientContextConfigImplTest, EmptyServerNameIndication) {
  envoy::api::v2::auth::UpstreamTlsContext tls_context;
  tls_context.set_sni(std::string("\000", 1));
  EXPECT_THROW_WITH_MESSAGE(ClientContextConfigImpl client_context_config(tls_context),
                            EnvoyException, "SNI names containing NULL-byte are not allowed");
  tls_context.set_sni(std::string("a\000b", 3));
  EXPECT_THROW_WITH_MESSAGE(ClientContextConfigImpl client_context_config(tls_context),
                            EnvoyException, "SNI names containing NULL-byte are not allowed");
}

// Multiple certificate hashes are not yet supported.
// TODO(htuch): Support multiple hashes.
TEST(ClientContextConfigImplTest, MultipleValidationHashes) {
  envoy::api::v2::auth::UpstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->add_verify_certificate_hash();
  tls_context.mutable_common_tls_context()
      ->mutable_validation_context()
      ->add_verify_certificate_hash();
  EXPECT_THROW_WITH_MESSAGE(ClientContextConfigImpl client_context_config(tls_context),
                            EnvoyException,
                            "Multiple TLS certificate verification hashes are not supported");
}

// Multiple TLS certificates are not yet supported.
// TODO(PiotrSikora): Support multiple TLS certificates.
TEST(ClientContextConfigImplTest, MultipleTlsCertificates) {
  envoy::api::v2::auth::UpstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()->add_tls_certificates();
  tls_context.mutable_common_tls_context()->add_tls_certificates();
  EXPECT_THROW_WITH_MESSAGE(ClientContextConfigImpl client_context_config(tls_context),
                            EnvoyException,
                            "Multiple TLS certificates are not supported for client contexts");
}

// Multiple TLS certificates are not yet supported, but one is expected for
// server.
// TODO(PiotrSikora): Support multiple TLS certificates.
TEST(ServerContextConfigImplTest, MultipleTlsCertificates) {
  envoy::api::v2::auth::DownstreamTlsContext tls_context;
  EXPECT_THROW_WITH_MESSAGE(ServerContextConfigImpl client_context_config(tls_context),
                            EnvoyException,
                            "A single TLS certificate is required for server contexts");
  tls_context.mutable_common_tls_context()->add_tls_certificates();
  tls_context.mutable_common_tls_context()->add_tls_certificates();
  EXPECT_THROW_WITH_MESSAGE(ServerContextConfigImpl client_context_config(tls_context),
                            EnvoyException,
                            "A single TLS certificate is required for server contexts");
}

// TlsCertificate messages must have a cert for servers.
TEST(ServerContextImplTest, TlsCertificateNonEmpty) {
  envoy::api::v2::auth::DownstreamTlsContext tls_context;
  tls_context.mutable_common_tls_context()->add_tls_certificates();
  ServerContextConfigImpl client_context_config(tls_context);
  Runtime::MockLoader runtime;
  ContextManagerImpl manager(runtime);
  Stats::IsolatedStoreImpl store;
  EXPECT_THROW_WITH_MESSAGE(ServerContextPtr server_ctx(manager.createSslServerContext(
                                store, client_context_config, std::vector<std::string>{})),
                            EnvoyException,
                            "Server TlsCertificates must have a certificate specified");
}

} // namespace Ssl
} // namespace Envoy
