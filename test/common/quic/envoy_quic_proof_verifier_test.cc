#include <algorithm>
#include <memory>

#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/extensions/transport_sockets/tls/context_config_impl.h"

#include "test/common/config/dummy_config.pb.h"
#include "test/common/quic/test_utils.h"
#include "test/extensions/transport_sockets/tls/cert_validator/timed_cert_validator.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/quic/core/crypto/certificate_view.h"
#include "quiche/quic/test_tools/test_certificates.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Quic {

class MockProofVerifierCallback : public quic::ProofVerifierCallback {
public:
  MOCK_METHOD(void, Run, (bool, const std::string&, std::unique_ptr<quic::ProofVerifyDetails>*));
};

class EnvoyQuicProofVerifierTest : public ::testing::Test,
                                   public testing::WithParamInterface<bool> {
public:
  EnvoyQuicProofVerifierTest()
      : root_ca_cert_(cert_chain_.substr(cert_chain_.rfind("-----BEGIN CERTIFICATE-----"))),
        leaf_cert_([=]() {
          std::stringstream pem_stream(cert_chain_);
          std::vector<std::string> chain = quic::CertificateView::LoadPemFromStream(&pem_stream);
          return chain[0];
        }()) {
    Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.tls_async_cert_validation",
                                  GetParam());
    ON_CALL(client_context_config_, cipherSuites)
        .WillByDefault(ReturnRef(
            Extensions::TransportSockets::Tls::ClientContextConfigImpl::DEFAULT_CIPHER_SUITES));
    ON_CALL(client_context_config_, ecdhCurves)
        .WillByDefault(
            ReturnRef(Extensions::TransportSockets::Tls::ClientContextConfigImpl::DEFAULT_CURVES));
    ON_CALL(client_context_config_, alpnProtocols()).WillByDefault(ReturnRef(alpn_));
    ON_CALL(client_context_config_, serverNameIndication()).WillByDefault(ReturnRef(empty_string_));
    ON_CALL(client_context_config_, signingAlgorithmsForTest()).WillByDefault(ReturnRef(sig_algs_));
    ON_CALL(client_context_config_, certificateValidationContext())
        .WillByDefault(Return(&cert_validation_ctx_config_));
    ON_CALL(verify_context_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
    ON_CALL(verify_context_, transportSocketOptions())
        .WillByDefault(ReturnRef(transport_socket_options_));
  }

  // Since this cert chain contains an expired cert, we can flip allow_expired_cert to test the code
  // paths for BoringSSL cert verification success and failure.
  void configCertVerificationDetails(bool allow_expired_cert) {
    // Getting the last cert in the chain as the root CA cert.
    EXPECT_CALL(cert_validation_ctx_config_, caCert()).WillRepeatedly(ReturnRef(root_ca_cert_));
    EXPECT_CALL(cert_validation_ctx_config_, caCertPath()).WillRepeatedly(ReturnRef(path_string_));
    EXPECT_CALL(cert_validation_ctx_config_, trustChainVerification)
        .WillRepeatedly(Return(envoy::extensions::transport_sockets::tls::v3::
                                   CertificateValidationContext::VERIFY_TRUST_CHAIN));
    EXPECT_CALL(cert_validation_ctx_config_, allowExpiredCertificate())
        .WillRepeatedly(Return(allow_expired_cert));
    EXPECT_CALL(cert_validation_ctx_config_, certificateRevocationList())
        .WillRepeatedly(ReturnRef(empty_string_));
    EXPECT_CALL(cert_validation_ctx_config_, certificateRevocationListPath())
        .WillRepeatedly(ReturnRef(path_string_));
    EXPECT_CALL(cert_validation_ctx_config_, subjectAltNameMatchers())
        .WillRepeatedly(ReturnRef(san_matchers_));
    EXPECT_CALL(cert_validation_ctx_config_, verifyCertificateHashList())
        .WillRepeatedly(ReturnRef(empty_string_list_));
    EXPECT_CALL(cert_validation_ctx_config_, verifyCertificateSpkiList())
        .WillRepeatedly(ReturnRef(empty_string_list_));
    EXPECT_CALL(cert_validation_ctx_config_, customValidatorConfig())
        .WillRepeatedly(ReturnRef(custom_validator_config_));
    auto context = std::make_shared<Extensions::TransportSockets::Tls::ClientContextImpl>(
        store_, client_context_config_, time_system_);
    verifier_ = std::make_unique<EnvoyQuicProofVerifier>(std::move(context));
  }

protected:
  const std::string path_string_{"some_path"};
  const std::string alpn_{"h2,http/1.1"};
  const std::string sig_algs_{"rsa_pss_rsae_sha256"};
  const std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher>
      san_matchers_;
  const std::string empty_string_;
  const std::vector<std::string> empty_string_list_;
  const std::string cert_chain_{quic::test::kTestCertificateChainPem};
  std::string root_ca_cert_;
  const std::string leaf_cert_;
  absl::optional<envoy::config::core::v3::TypedExtensionConfig> custom_validator_config_{
      absl::nullopt};
  NiceMock<Stats::MockStore> store_;
  Event::GlobalTimeSystem time_system_;
  NiceMock<Ssl::MockClientContextConfig> client_context_config_;
  Ssl::MockCertificateValidationContextConfig cert_validation_ctx_config_;
  std::unique_ptr<EnvoyQuicProofVerifier> verifier_;
  NiceMock<Ssl::MockContextManager> tls_context_manager_;
  Event::MockDispatcher dispatcher_;
  Network::TransportSocketOptionsConstSharedPtr transport_socket_options_;
  NiceMock<MockProofVerifyContext> verify_context_;
};

INSTANTIATE_TEST_SUITE_P(EnvoyQuicProofVerifierTests, EnvoyQuicProofVerifierTest, testing::Bool());

TEST_P(EnvoyQuicProofVerifierTest, VerifyCertChainSuccess) {
  configCertVerificationDetails(true);
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(leaf_cert_);
  const std::string ocsp_response;
  const std::string cert_sct;
  std::string error_details;
  std::unique_ptr<quic::ProofVerifyDetails> verify_details;
  EXPECT_EQ(quic::QUIC_SUCCESS,
            verifier_->VerifyCertChain(std::string(cert_view->subject_alt_name_domains()[0]), 54321,
                                       {leaf_cert_}, ocsp_response, cert_sct, &verify_context_,
                                       &error_details, &verify_details, nullptr, nullptr))
      << error_details;
  EXPECT_NE(verify_details, nullptr);
  EXPECT_TRUE(static_cast<CertVerifyResult&>(*verify_details).isValid());
  std::unique_ptr<CertVerifyResult> cloned(static_cast<CertVerifyResult*>(verify_details->Clone()));
  EXPECT_TRUE(cloned->isValid());
}

TEST_P(EnvoyQuicProofVerifierTest, AsyncVerifyCertChainSuccess) {
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.tls_async_cert_validation")) {
    return;
  }
  custom_validator_config_ = envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: "envoy.tls.cert_validator.timed_cert_validator"
typed_config:
  "@type": type.googleapis.com/test.common.config.DummyConfig
  )EOF"),
                            custom_validator_config_.value());

  configCertVerificationDetails(true);
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(leaf_cert_);
  const std::string ocsp_response;
  const std::string cert_sct;
  std::string error_details;
  std::unique_ptr<quic::ProofVerifyDetails> verify_details;
  auto* quic_verify_callback = new MockProofVerifierCallback();
  Event::MockTimer* verify_timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_EQ(quic::QUIC_PENDING,
            verifier_->VerifyCertChain(
                std::string(cert_view->subject_alt_name_domains()[0]), 54321, {leaf_cert_},
                ocsp_response, cert_sct, &verify_context_, &error_details, &verify_details, nullptr,
                std::unique_ptr<MockProofVerifierCallback>(quic_verify_callback)));
  EXPECT_EQ(verify_details, nullptr);
  EXPECT_TRUE(verify_timer->enabled());

  EXPECT_CALL(*quic_verify_callback, Run(true, _, _))
      .WillOnce(Invoke(
          [](bool, const std::string&, std::unique_ptr<quic::ProofVerifyDetails>* verify_details) {
            EXPECT_NE(verify_details, nullptr);
            auto details = std::unique_ptr<quic::ProofVerifyDetails>((*verify_details)->Clone());
            EXPECT_TRUE(static_cast<CertVerifyResult&>(*details).isValid());
          }));
  verify_timer->invokeCallback();
}

TEST_P(EnvoyQuicProofVerifierTest, VerifyCertChainFailureFromSsl) {
  configCertVerificationDetails(false);
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(leaf_cert_);
  const std::string ocsp_response;
  const std::string cert_sct;
  std::string error_details;
  std::unique_ptr<quic::ProofVerifyDetails> verify_details;
  EXPECT_EQ(quic::QUIC_FAILURE,
            verifier_->VerifyCertChain(std::string(cert_view->subject_alt_name_domains()[0]), 54321,
                                       {leaf_cert_}, ocsp_response, cert_sct, &verify_context_,
                                       &error_details, &verify_details, nullptr, nullptr))
      << error_details;
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.tls_async_cert_validation")) {
    EXPECT_EQ("verify cert failed: X509_verify_cert: certificate verification error at depth 1: "
              "certificate has expired",
              error_details);
  } else {
    EXPECT_EQ("X509_verify_cert: certificate verification error at depth 1: "
              "certificate has expired",
              error_details);
  }
  EXPECT_NE(verify_details, nullptr);
  EXPECT_FALSE(static_cast<CertVerifyResult&>(*verify_details).isValid());
}

TEST_P(EnvoyQuicProofVerifierTest, VerifyCertChainFailureInvalidCA) {
  root_ca_cert_ = "invalid root CA";
  EXPECT_THROW_WITH_REGEX(configCertVerificationDetails(true), EnvoyException,
                          "Failed to load trusted CA certificates from");
}

TEST_P(EnvoyQuicProofVerifierTest, VerifyCertChainFailureInvalidLeafCert) {
  configCertVerificationDetails(true);
  const std::string ocsp_response;
  const std::string cert_sct;
  std::string error_details;
  const std::vector<std::string> certs{"invalid leaf cert"};
  std::unique_ptr<quic::ProofVerifyDetails> verify_details;
  EXPECT_EQ(quic::QUIC_FAILURE,
            verifier_->VerifyCertChain("www.google.com", 54321, certs, ocsp_response, cert_sct,
                                       &verify_context_, &error_details, &verify_details, nullptr,
                                       nullptr));
  EXPECT_EQ("d2i_X509: fail to parse DER", error_details);
}

TEST_P(EnvoyQuicProofVerifierTest, VerifyCertChainFailureLeafCertWithGarbage) {
  configCertVerificationDetails(true);
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(leaf_cert_);
  const std::string ocsp_response;
  const std::string cert_sct;
  std::string cert_with_trailing_garbage = absl::StrCat(leaf_cert_, "AAAAAA");
  std::string error_details;
  std::unique_ptr<quic::ProofVerifyDetails> verify_details;
  EXPECT_EQ(quic::QUIC_FAILURE,
            verifier_->VerifyCertChain(std::string(cert_view->subject_alt_name_domains()[0]), 54321,
                                       {cert_with_trailing_garbage}, ocsp_response, cert_sct,
                                       &verify_context_, &error_details, &verify_details, nullptr,
                                       nullptr))
      << error_details;
  EXPECT_EQ("There is trailing garbage in DER.", error_details);
}

TEST_P(EnvoyQuicProofVerifierTest, VerifyCertChainFailureInvalidHost) {
  configCertVerificationDetails(true);
  const std::string ocsp_response;
  const std::string cert_sct;
  std::string error_details;
  std::unique_ptr<quic::ProofVerifyDetails> verify_details;
  EXPECT_EQ(quic::QUIC_FAILURE,
            verifier_->VerifyCertChain("unknown.org", 54321, {leaf_cert_}, ocsp_response, cert_sct,
                                       &verify_context_, &error_details, &verify_details, nullptr,
                                       nullptr))
      << error_details;
  EXPECT_EQ("Leaf certificate doesn't match hostname: unknown.org", error_details);
}

TEST_P(EnvoyQuicProofVerifierTest, AsyncVerifyCertChainFailureInvalidHost) {
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.tls_async_cert_validation")) {
    return;
  }

  custom_validator_config_ = envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: "envoy.tls.cert_validator.timed_cert_validator"
typed_config:
  "@type": type.googleapis.com/test.common.config.DummyConfig
  )EOF"),
                            custom_validator_config_.value());

  configCertVerificationDetails(true);
  const std::string ocsp_response;
  const std::string cert_sct;
  std::string error_details;
  std::unique_ptr<quic::ProofVerifyDetails> verify_details;
  auto* quic_verify_callback = new MockProofVerifierCallback();
  Event::MockTimer* verify_timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_EQ(
      quic::QUIC_PENDING,
      verifier_->VerifyCertChain("unknown.org", 54321, {leaf_cert_}, ocsp_response, cert_sct,
                                 &verify_context_, &error_details, &verify_details, nullptr,
                                 std::unique_ptr<MockProofVerifierCallback>(quic_verify_callback)));
  EXPECT_EQ(verify_details, nullptr);
  EXPECT_TRUE(verify_timer->enabled());

  EXPECT_CALL(*quic_verify_callback,
              Run(false, "Leaf certificate doesn't match hostname: unknown.org", _))
      .WillOnce(Invoke(
          [](bool, const std::string&, std::unique_ptr<quic::ProofVerifyDetails>* verify_details) {
            EXPECT_NE(verify_details, nullptr);
            auto details = std::unique_ptr<quic::ProofVerifyDetails>((*verify_details)->Clone());
            EXPECT_FALSE(static_cast<CertVerifyResult&>(*details).isValid());
          }));
  verify_timer->invokeCallback();
}

TEST_P(EnvoyQuicProofVerifierTest, VerifyCertChainFailureUnsupportedECKey) {
  configCertVerificationDetails(true);
  const std::string ocsp_response;
  const std::string cert_sct;
  std::string error_details;
  // This is a EC cert with secp384r1 curve which is not supported by Envoy.
  const std::string certs{R"(-----BEGIN CERTIFICATE-----
MIICkDCCAhagAwIBAgIUTZbykU9eQL3GdrNlodxrOJDecIQwCgYIKoZIzj0EAwIw
fzELMAkGA1UEBhMCVVMxCzAJBgNVBAgMAk1BMRIwEAYDVQQHDAlDYW1icmlkZ2Ux
DzANBgNVBAoMBkdvb2dsZTEOMAwGA1UECwwFZW52b3kxDTALBgNVBAMMBHRlc3Qx
HzAdBgkqhkiG9w0BCQEWEGRhbnpoQGdvb2dsZS5jb20wHhcNMjAwODA1MjAyMDI0
WhcNMjIwODA1MjAyMDI0WjB/MQswCQYDVQQGEwJVUzELMAkGA1UECAwCTUExEjAQ
BgNVBAcMCUNhbWJyaWRnZTEPMA0GA1UECgwGR29vZ2xlMQ4wDAYDVQQLDAVlbnZv
eTENMAsGA1UEAwwEdGVzdDEfMB0GCSqGSIb3DQEJARYQZGFuemhAZ29vZ2xlLmNv
bTB2MBAGByqGSM49AgEGBSuBBAAiA2IABGRaEAtVq+xHXfsF4R/j+mqVN2E29ZYL
oFlvnelKeeT2B51bSfUv+X+Ci1BSa2OxPCVS6o0vpcF6YOlz4CS7QcXZIoRfhsv7
O2Hz/IdxAPhX/gdK/70T1x+V/6nvIHiiw6NTMFEwHQYDVR0OBBYEFF75rDce6xNJ
GfpKbUg4emG2KWRMMB8GA1UdIwQYMBaAFF75rDce6xNJGfpKbUg4emG2KWRMMA8G
A1UdEwEB/wQFMAMBAf8wCgYIKoZIzj0EAwIDaAAwZQIxAIyZghTK3cmyrRWkxfQ7
xEc11gujcT8nbytYbM6jodKwcbtR6SOmLx2ychXrCMm2ZAIwXqmrTYBtrbqb3mBx
VdGXMAjeXhnOnPvmDi5hUz/uvI+Pg6cNmUoCRwSCnK/DazhA
-----END CERTIFICATE-----)"};
  std::stringstream pem_stream(certs);
  std::vector<std::string> chain = quic::CertificateView::LoadPemFromStream(&pem_stream);
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(chain[0]);
  ASSERT(cert_view);
  std::unique_ptr<quic::ProofVerifyDetails> verify_details;
  EXPECT_EQ(quic::QUIC_FAILURE,
            verifier_->VerifyCertChain("www.google.com", 54321, chain, ocsp_response, cert_sct,
                                       &verify_context_, &error_details, &verify_details, nullptr,
                                       nullptr));
  EXPECT_EQ("Invalid leaf cert, only P-256 ECDSA certificates are supported", error_details);
}

TEST_P(EnvoyQuicProofVerifierTest, VerifyCertChainFailureNonServerAuthEKU) {
  // Override the CA cert with cert copied from test/config/integration/certs/cacert.pem.
  root_ca_cert_ = R"(-----BEGIN CERTIFICATE-----
MIID3TCCAsWgAwIBAgIUdCu/mLip3X/We37vh3BA9u/nxakwDQYJKoZIhvcNAQEL
BQAwdjELMAkGA1UEBhMCVVMxEzARBgNVBAgMCkNhbGlmb3JuaWExFjAUBgNVBAcM
DVNhbiBGcmFuY2lzY28xDTALBgNVBAoMBEx5ZnQxGTAXBgNVBAsMEEx5ZnQgRW5n
aW5lZXJpbmcxEDAOBgNVBAMMB1Rlc3QgQ0EwHhcNMjAwODA1MTkxNjAwWhcNMjIw
ODA1MTkxNjAwWjB2MQswCQYDVQQGEwJVUzETMBEGA1UECAwKQ2FsaWZvcm5pYTEW
MBQGA1UEBwwNU2FuIEZyYW5jaXNjbzENMAsGA1UECgwETHlmdDEZMBcGA1UECwwQ
THlmdCBFbmdpbmVlcmluZzEQMA4GA1UEAwwHVGVzdCBDQTCCASIwDQYJKoZIhvcN
AQEBBQADggEPADCCAQoCggEBALu2Ihi4DmaQG7zySZlWyM9SjxOXCI5840V7Hn0C
XoiI8sQQmKSC2YCzsaphQoJ0lXCi6Y47o5FkooYyLeNDQTGS0nh+IWm5RCyochtO
fnaKPv/hYxhpyFQEwkJkbF1Zt1s6j2rq5MzmbWZx090uXZEE82DNZ9QJaMPu6VWt
iwGoGoS5HF5HNlUVxLNUsklNH0ZfDafR7/LC2ty1vO1c6EJ6yCGiyJZZ7Ilbz27Q
HPAUd8CcDNKCHZDoMWkLSLN3Nj1MvPVZ5HDsHiNHXthP+zV8FQtloAuZ8Srsmlyg
rJREkc7gF3f6HrH5ShNhsRFFc53NUjDbYZuha1u4hiOE8lcCAwEAAaNjMGEwDwYD
VR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwHQYDVR0OBBYEFJZL2ixTtL6V
xpNz4qekny4NchiHMB8GA1UdIwQYMBaAFJZL2ixTtL6VxpNz4qekny4NchiHMA0G
CSqGSIb3DQEBCwUAA4IBAQAcgG+AaCdrUFEVJDn9UsO7zqzQ3c1VOp+WAtAU8OQK
Oc4vJYVVKpDs8OZFxmukCeqm1gz2zDeH7TfgCs5UnLtkplx1YO1bd9qvserJVHiD
LAK+Yl24ZEbrHPaq0zI1RLchqYUOGWmi51pcXi1gsfc8DQ3GqIXoai6kYJeV3jFJ
jxpQSR32nx6oNN/6kVKlgmBjlWrOy7JyDXGim6Z97TzmS6Clctewmw/5gZ9g+M8e
g0ZdFbFkNUjzSNm44hiDX8nR6yJRn+gLaARaJvp1dnT+MlvofZuER17WYKH4OyMs
ie3qKR3an4KC20CtFbpZfv540BVuTTOCtQ5xqZ/LTE78
-----END CERTIFICATE-----)";
  configCertVerificationDetails(true);
  const std::string ocsp_response;
  const std::string cert_sct;
  std::string error_details;
  // This is a cert generated with the test/config/integration/certs/certs.sh. And the config that
  // used to generate this cert is same as test/config/integration/certs/servercert.cfg but with
  // 'extKeyUsage: clientAuth'.
  const std::string certs{R"(-----BEGIN CERTIFICATE-----
MIIEYjCCA0qgAwIBAgIUWzmfQSTX8xfzUzdByjCjCJN8E/wwDQYJKoZIhvcNAQEL
BQAwdjELMAkGA1UEBhMCVVMxEzARBgNVBAgMCkNhbGlmb3JuaWExFjAUBgNVBAcM
DVNhbiBGcmFuY2lzY28xDTALBgNVBAoMBEx5ZnQxGTAXBgNVBAsMEEx5ZnQgRW5n
aW5lZXJpbmcxEDAOBgNVBAMMB1Rlc3QgQ0EwHhcNMjEwOTI5MTY0NTM3WhcNMjMw
OTI5MTY0NTM3WjCBpjELMAkGA1UEBhMCVVMxEzARBgNVBAgMCkNhbGlmb3JuaWEx
FjAUBgNVBAcMDVNhbiBGcmFuY2lzY28xDTALBgNVBAoMBEx5ZnQxGTAXBgNVBAsM
EEx5ZnQgRW5naW5lZXJpbmcxGjAYBgNVBAMMEVRlc3QgQmFja2VuZCBUZWFtMSQw
IgYJKoZIhvcNAQkBFhViYWNrZW5kLXRlYW1AbHlmdC5jb20wggEiMA0GCSqGSIb3
DQEBAQUAA4IBDwAwggEKAoIBAQC9JgaI7hxjPM0tsUna/QmivBdKbCrLnLW9Teak
RH/Ebg68ovyvrRIlybDT6XhKi+iVpzVY9kqxhGHgrFDgGLBakVMiYJ5EjIgHfoo4
UUAHwIYbunJluYCgANzpprBsvTC/yFYDVMqUrjvwHsoYYVm36io994k9+t813b70
o0l7/PraBsKkz8NcY2V2mrd/yHn/0HAhv3hl6iiJme9yURuDYQrae2ACSrQtsbel
KwdZ/Re71Z1awz0OQmAjMa2HuCop+Q/1QLnqBekT5+DH1qKUzJ3Jkq6NRkERXOpi
87j04rtCBteCogrO67qnuBZ2lH3jYEMb+lQdLkyNMLltBSdLAgMBAAGjgbYwgbMw
DAYDVR0TAQH/BAIwADALBgNVHQ8EBAMCBeAwEwYDVR0lBAwwCgYIKwYBBQUHAwIw
QQYDVR0RBDowOIYec3BpZmZlOi8vbHlmdC5jb20vYmFja2VuZC10ZWFtgghseWZ0
LmNvbYIMd3d3Lmx5ZnQuY29tMB0GA1UdDgQWBBTZdxNltzTEpl+A1UpK8BsxkkIG
hjAfBgNVHSMEGDAWgBSWS9osU7S+lcaTc+KnpJ8uDXIYhzANBgkqhkiG9w0BAQsF
AAOCAQEAhiXkQJZ53L3uoQMX6xNhAFThomirnLm2RT10kPIbr5mmf3wcR8+EKrWX
dWCj56bk1tSDbQZqx33DSGbhvNaydggbo69Pkie5b7J9O7AWzT21NME6Jis9hHED
VUI63L+7SgJ2oZs0o8xccUaLFeknuNdQL4qUEwhMwCC8kYLz+c6g0qwDwZi1MtdL
YR4qm2S6KveVPGzBHpUjfWf/whSCM3JN5Fm8gWfC6d6XEYz6z1dZrj3lpwmhRgF6
Wb72f68jzCQ3BFqKRFsJI2xz3EP6PoQ+e6EQjMpjQLomxIhIN/aTsgrKwA5wf6vQ
ZCFbredVxDBZuoVsfrKPSQa407Jj1Q==
-----END CERTIFICATE-----)"};
  std::stringstream pem_stream(certs);
  std::vector<std::string> chain = quic::CertificateView::LoadPemFromStream(&pem_stream);
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(chain[0]);
  ASSERT(cert_view);
  std::unique_ptr<quic::ProofVerifyDetails> verify_details;
  EXPECT_EQ(quic::QUIC_FAILURE,
            verifier_->VerifyCertChain("lyft.com", 54321, chain, ocsp_response, cert_sct,
                                       &verify_context_, &error_details, &verify_details, nullptr,
                                       nullptr));
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.tls_async_cert_validation")) {
    EXPECT_EQ("verify cert failed: X509_verify_cert: certificate verification error at depth 0: "
              "unsupported certificate "
              "purpose",
              error_details);
  } else {
    EXPECT_EQ("X509_verify_cert: certificate verification error at depth 0: "
              "unsupported certificate "
              "purpose",
              error_details);
  }
}

TEST_P(EnvoyQuicProofVerifierTest, VerifySubjectAltNameListOverrideFailure) {
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.tls_async_cert_validation")) {
    return;
  }

  transport_socket_options_.reset(new Network::TransportSocketOptionsImpl("", {"non-example.com"}));
  configCertVerificationDetails(true);
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(leaf_cert_);
  const std::string ocsp_response;
  const std::string cert_sct;
  std::string error_details;
  std::unique_ptr<quic::ProofVerifyDetails> verify_details;
  EXPECT_EQ(quic::QUIC_FAILURE,
            verifier_->VerifyCertChain(std::string(cert_view->subject_alt_name_domains()[0]), 54321,
                                       {leaf_cert_}, ocsp_response, cert_sct, &verify_context_,
                                       &error_details, &verify_details, nullptr, nullptr))
      << error_details;
  EXPECT_EQ("verify cert failed: verify SAN list", error_details);
  EXPECT_NE(verify_details, nullptr);
  EXPECT_FALSE(static_cast<CertVerifyResult&>(*verify_details).isValid());
}

TEST_P(EnvoyQuicProofVerifierTest, VerifyProof) {
  configCertVerificationDetails(true);
  EXPECT_DEATH(verifier_->VerifyProof("", 0, "", quic::QUIC_VERSION_IETF_RFC_V1, "", {}, "", "",
                                      nullptr, nullptr, nullptr, {}),
               "not implemented");
}

TEST_P(EnvoyQuicProofVerifierTest, CreateDefaultContext) {
  configCertVerificationDetails(true);
  EXPECT_EQ(nullptr, verifier_->CreateDefaultContext());
}

} // namespace Quic
} // namespace Envoy
