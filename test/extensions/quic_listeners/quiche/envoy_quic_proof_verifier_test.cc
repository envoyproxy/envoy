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
      : root_ca_cert_(cert_chain_.substr(cert_chain_.rfind("-----BEGIN CERTIFICATE-----"))),
        leaf_cert_([=]() {
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
    ON_CALL(client_context_config_, serverNameIndication()).WillByDefault(ReturnRef(empty_string_));
    ON_CALL(client_context_config_, signingAlgorithmsForTest()).WillByDefault(ReturnRef(sig_algs_));
    ON_CALL(client_context_config_, certificateValidationContext())
        .WillByDefault(Return(&cert_validation_ctx_config_));
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
    EXPECT_CALL(cert_validation_ctx_config_, verifySubjectAltNameList())
        .WillRepeatedly(ReturnRef(empty_string_list_));
    EXPECT_CALL(cert_validation_ctx_config_, subjectAltNameMatchers())
        .WillRepeatedly(ReturnRef(san_matchers_));
    EXPECT_CALL(cert_validation_ctx_config_, verifyCertificateHashList())
        .WillRepeatedly(ReturnRef(empty_string_list_));
    EXPECT_CALL(cert_validation_ctx_config_, verifyCertificateSpkiList())
        .WillRepeatedly(ReturnRef(empty_string_list_));
    verifier_ =
        std::make_unique<EnvoyQuicProofVerifier>(store_, client_context_config_, time_system_);
  }

protected:
  const std::string path_string_{"some_path"};
  const std::string alpn_{"h2,http/1.1"};
  const std::string sig_algs_{"rsa_pss_rsae_sha256"};
  const std::vector<envoy::type::matcher::v3::StringMatcher> san_matchers_;
  const std::string empty_string_;
  const std::vector<std::string> empty_string_list_;
  const std::string cert_chain_{quic::test::kTestCertificateChainPem};
  const std::string root_ca_cert_;
  const std::string leaf_cert_;
  NiceMock<Stats::MockStore> store_;
  Event::GlobalTimeSystem time_system_;
  NiceMock<Ssl::MockClientContextConfig> client_context_config_;
  Ssl::MockCertificateValidationContextConfig cert_validation_ctx_config_;
  std::unique_ptr<EnvoyQuicProofVerifier> verifier_;
};

TEST_F(EnvoyQuicProofVerifierTest, VerifyCertChainSuccess) {
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

TEST_F(EnvoyQuicProofVerifierTest, VerifyCertChainFailureFromSsl) {
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

TEST_F(EnvoyQuicProofVerifierTest, VerifyCertChainFailureInvalidLeafCert) {
  configCertVerificationDetails(true);
  const std::string ocsp_response;
  const std::string cert_sct;
  std::string error_details;
  const std::vector<std::string> certs{"invalid leaf cert"};
  EXPECT_EQ(quic::QUIC_FAILURE,
            verifier_->VerifyCertChain("www.google.com", 54321, certs, ocsp_response, cert_sct,
                                       nullptr, &error_details, nullptr, nullptr));
  EXPECT_EQ("d2i_X509: fail to parse DER", error_details);
}

TEST_F(EnvoyQuicProofVerifierTest, VerifyCertChainFailureLeafCertWithGarbage) {
  configCertVerificationDetails(true);
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(leaf_cert_);
  const std::string ocsp_response;
  const std::string cert_sct;
  std::string cert_with_trailing_garbage = absl::StrCat(leaf_cert_, "AAAAAA");
  std::string error_details;
  EXPECT_EQ(quic::QUIC_FAILURE,
            verifier_->VerifyCertChain(std::string(cert_view->subject_alt_name_domains()[0]), 54321,
                                       {cert_with_trailing_garbage}, ocsp_response, cert_sct,
                                       nullptr, &error_details, nullptr, nullptr))
      << error_details;
  EXPECT_EQ("There is trailing garbage in DER.", error_details);
}

TEST_F(EnvoyQuicProofVerifierTest, VerifyCertChainFailureInvalidHost) {
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

TEST_F(EnvoyQuicProofVerifierTest, VerifyProofFailureEmptyCertChain) {
  configCertVerificationDetails(true);
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(leaf_cert_);
  quic::QuicTransportVersion version{quic::QUIC_VERSION_UNSUPPORTED};
  quiche::QuicheStringPiece chlo_hash{"aaaaa"};
  std::string server_config{"Server Config"};
  const std::string ocsp_response;
  const std::string cert_sct;
  std::string error_details;
  const std::vector<std::string> certs;
  EXPECT_EQ(quic::QUIC_FAILURE,
            verifier_->VerifyProof(std::string(cert_view->subject_alt_name_domains()[0]), 54321,
                                   server_config, version, chlo_hash, certs, cert_sct, "signature",
                                   nullptr, &error_details, nullptr, nullptr));
  EXPECT_EQ("Received empty cert chain.", error_details);
}

TEST_F(EnvoyQuicProofVerifierTest, VerifyProofFailureInvalidLeafCert) {
  configCertVerificationDetails(true);
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(leaf_cert_);
  quic::QuicTransportVersion version{quic::QUIC_VERSION_UNSUPPORTED};
  quiche::QuicheStringPiece chlo_hash{"aaaaa"};
  std::string server_config{"Server Config"};
  const std::string ocsp_response;
  const std::string cert_sct;
  std::string error_details;
  const std::vector<std::string> certs{"invalid leaf cert"};
  EXPECT_EQ(quic::QUIC_FAILURE,
            verifier_->VerifyProof(std::string(cert_view->subject_alt_name_domains()[0]), 54321,
                                   server_config, version, chlo_hash, certs, cert_sct, "signature",
                                   nullptr, &error_details, nullptr, nullptr));
  EXPECT_EQ("Invalid leaf cert.", error_details);
}

TEST_F(EnvoyQuicProofVerifierTest, VerifyProofFailureUnsupportedECKey) {
  configCertVerificationDetails(true);
  quic::QuicTransportVersion version{quic::QUIC_VERSION_UNSUPPORTED};
  quiche::QuicheStringPiece chlo_hash{"aaaaa"};
  std::string server_config{"Server Config"};
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
  EXPECT_EQ(quic::QUIC_FAILURE,
            verifier_->VerifyProof("www.google.com", 54321, server_config, version, chlo_hash,
                                   chain, cert_sct, "signature", nullptr, &error_details, nullptr,
                                   nullptr));
  EXPECT_EQ("Invalid leaf cert, only P-256 ECDSA certificates are supported", error_details);
}

TEST_F(EnvoyQuicProofVerifierTest, VerifyProofFailureInvalidSignature) {
  configCertVerificationDetails(true);
  std::unique_ptr<quic::CertificateView> cert_view =
      quic::CertificateView::ParseSingleCertificate(leaf_cert_);
  quic::QuicTransportVersion version{quic::QUIC_VERSION_UNSUPPORTED};
  quiche::QuicheStringPiece chlo_hash{"aaaaa"};
  std::string server_config{"Server Config"};
  const std::string ocsp_response;
  const std::string cert_sct;
  std::string error_details;
  EXPECT_EQ(quic::QUIC_FAILURE,
            verifier_->VerifyProof(std::string(cert_view->subject_alt_name_domains()[0]), 54321,
                                   server_config, version, chlo_hash, {leaf_cert_}, cert_sct,
                                   "signature", nullptr, &error_details, nullptr, nullptr));
  EXPECT_EQ("Signature is not valid.", error_details);
}

} // namespace Quic
} // namespace Envoy
