#include <algorithm>
#include <memory>

#include "extensions/quic_listeners/quiche/envoy_quic_proof_verifier.h"
#include "extensions/transport_sockets/tls/context_config_impl.h"

#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/test_time.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/quic/core/crypto/certificate_view.h"
#include "quiche/quic/test_tools/test_certificates.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Quic {

class EnvoyQuicProofVerifierTest : public testing::Test {
public:
  EnvoyQuicProofVerifierTest()
      : leaf_cert_([=]() {
          std::stringstream pem_stream(cert_chain_);
          std::vector<std::string> chain = quic::CertificateView::LoadPemFromStream(&pem_stream);
          return chain[0];
        }()) {
    ON_CALL(client_context_config_, cipherSuites)
        .WillByDefault(ReturnRef(
            Extensions::TransportSockets::Tls::ClientContextConfigImpl::DEFAULT_CIPHER_SUITES));
    ON_CALL(client_context_config_, ecdhCurves)
        .WillByDefault(
            ReturnRef(Extensions::TransportSockets::Tls::ClientContextConfigImpl::DEFAULT_CURVES));
    ON_CALL(client_context_config_, alpnProtocols()).WillByDefault(ReturnRef(alpn_));
    ;
    const std::string empty_string;
    ON_CALL(client_context_config_, serverNameIndication()).WillByDefault(ReturnRef(empty_string));
    ON_CALL(client_context_config_, signingAlgorithmsForTest()).WillByDefault(ReturnRef(sig_algs_));
    ON_CALL(client_context_config_, certificateValidationContext())
        .WillByDefault(Return(&cert_validation_ctx_config_));
  }

  // Since this cert chain contains an expired cert, we can flip allow_expired_cert to test the code
  // paths for BoringSSL cert verification success and failure.
  void configCertVerificationDetails(bool allow_expired_cert) {
    // Getting the last cert in the chain as the root CA cert.
    const std::string& root_ca_cert =
        cert_chain_.substr(cert_chain_.rfind("-----BEGIN CERTIFICATE-----"));
    const std::string path_string("some_path");
    EXPECT_CALL(cert_validation_ctx_config_, caCert()).WillRepeatedly(ReturnRef(root_ca_cert));
    EXPECT_CALL(cert_validation_ctx_config_, caCertPath()).WillRepeatedly(ReturnRef(path_string));
    EXPECT_CALL(cert_validation_ctx_config_, trustChainVerification)
        .WillRepeatedly(Return(envoy::extensions::transport_sockets::tls::v3::
                                   CertificateValidationContext::VERIFY_TRUST_CHAIN));
    EXPECT_CALL(cert_validation_ctx_config_, allowExpiredCertificate())
        .WillRepeatedly(Return(allow_expired_cert));
    const std::string crl_list;
    EXPECT_CALL(cert_validation_ctx_config_, certificateRevocationList())
        .WillRepeatedly(ReturnRef(crl_list));
    EXPECT_CALL(cert_validation_ctx_config_, certificateRevocationListPath())
        .WillRepeatedly(ReturnRef(path_string));
    const std::vector<std::string> empty_string_list;
    EXPECT_CALL(cert_validation_ctx_config_, verifySubjectAltNameList())
        .WillRepeatedly(ReturnRef(empty_string_list));
    const std::vector<envoy::type::matcher::v3::StringMatcher> san_matchers;
    EXPECT_CALL(cert_validation_ctx_config_, subjectAltNameMatchers())
        .WillRepeatedly(ReturnRef(san_matchers));
    EXPECT_CALL(cert_validation_ctx_config_, verifyCertificateHashList())
        .WillRepeatedly(ReturnRef(empty_string_list));
    EXPECT_CALL(cert_validation_ctx_config_, verifyCertificateSpkiList())
        .WillRepeatedly(ReturnRef(empty_string_list));

    verifier_ =
        std::make_unique<EnvoyQuicProofVerifier>(store_, client_context_config_, time_system_);
  }

protected:
  const std::string alpn_{"h2,http/1.1"};
  const std::string sig_algs_{"rsa_pss_rsae_sha256"};
  const std::string cert_chain_{quic::test::kTestCertificateChainPem};
  std::string leaf_cert_;
  NiceMock<Stats::MockStore> store_;
  Event::GlobalTimeSystem time_system_;
  NiceMock<Ssl::MockClientContextConfig> client_context_config_;
  Ssl::MockCertificateValidationContextConfig cert_validation_ctx_config_;
  std::unique_ptr<EnvoyQuicProofVerifier> verifier_;
};

TEST_F(EnvoyQuicProofVerifierTest, VerifyFilterChainSuccess) {
  configCertVerificationDetails(true);
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(leaf_cert_);
  const std::string ocsp_response;
  const std::string cert_sct;
  std::string error_details;
  EXPECT_EQ(quic::QUIC_SUCCESS,
            verifier_->VerifyCertChain(std::string(cert_view->subject_alt_name_domains()[0]), 54321,
                                       {leaf_cert_}, ocsp_response, cert_sct, nullptr,
                                       &error_details, nullptr, nullptr))
      << error_details;
}

TEST_F(EnvoyQuicProofVerifierTest, VerifyFilterChainFailureFromSsl) {
  configCertVerificationDetails(false);
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(leaf_cert_);
  const std::string ocsp_response;
  const std::string cert_sct;
  std::string error_details;
  EXPECT_EQ(quic::QUIC_FAILURE,
            verifier_->VerifyCertChain(std::string(cert_view->subject_alt_name_domains()[0]), 54321,
                                       {leaf_cert_}, ocsp_response, cert_sct, nullptr,
                                       &error_details, nullptr, nullptr))
      << error_details;
  EXPECT_EQ("X509_verify_cert: certificate verification error at depth 1: certificate has expired",
            error_details);
}

TEST_F(EnvoyQuicProofVerifierTest, VerifyFilterChainFailureInvalidHost) {
  configCertVerificationDetails(true);
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(leaf_cert_);
  const std::string ocsp_response;
  const std::string cert_sct;
  std::string error_details;
  EXPECT_EQ(quic::QUIC_FAILURE,
            verifier_->VerifyCertChain("unknown.org", 54321, {leaf_cert_}, ocsp_response, cert_sct,
                                       nullptr, &error_details, nullptr, nullptr))
      << error_details;
  EXPECT_EQ("Leaf certificate doesn't match hostname: unknown.org", error_details);
}
} // namespace Quic
} // namespace Envoy
