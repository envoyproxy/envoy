#include "source/common/quic/envoy_quic_downstream_cert_verifier.h"
#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/tls/context_manager_impl.h"
#include "source/common/tls/server_context_config_impl.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/quic/core/crypto/certificate_view.h"
#include "quiche/quic/test_tools/test_certificates.h"

using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Quic {

class EnvoyQuicDownstreamCertVerifierTest : public testing::Test {
public:
  EnvoyQuicDownstreamCertVerifierTest() : api_(Api::createApiForTest(store_)) {
    ON_CALL(factory_context_.server_context_, api()).WillByDefault(ReturnRef(*api_));
    ON_CALL(factory_context_.server_context_, threadLocal())
        .WillByDefault(ReturnRef(thread_local_));
  }

protected:
  // Builds a server context that requires a client certificate and trusts the test-data CA.
  Extensions::TransportSockets::Tls::ContextImpl& serverContext() {
    const std::string yaml = R"EOF(
common_tls_context:
  tls_certificates:
  - certificate_chain:
      filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"
    private_key:
      filename: "{{ test_rundir }}/test/common/tls/test_data/san_uri_key.pem"
  validation_context:
    trusted_ca:
      filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
require_client_certificate: true
)EOF";
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    TestUtility::loadFromYaml(TestEnvironment::substitute(yaml), tls_context);
    auto config = THROW_OR_RETURN_VALUE(
        Extensions::TransportSockets::Tls::ServerContextConfigImpl::create(tls_context,
                                                                           factory_context_, {},
                                                                           /*for_quic=*/true),
        std::unique_ptr<Extensions::TransportSockets::Tls::ServerContextConfigImpl>);
    server_context_ = THROW_OR_RETURN_VALUE(
        manager_.createSslServerContext(*store_.rootScope(), *config, nullptr),
        Ssl::ServerContextSharedPtr);
    return dynamic_cast<Extensions::TransportSockets::Tls::ContextImpl&>(*server_context_);
  }

  Stats::TestUtil::TestStore store_;
  Api::ApiPtr api_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
  Extensions::TransportSockets::Tls::ContextManagerImpl manager_{factory_context_.server_context_};
  Ssl::ServerContextSharedPtr server_context_;
};

// An empty chain is rejected because the client presented no certificate.
TEST_F(EnvoyQuicDownstreamCertVerifierTest, EmptyChainRejected) {
  std::string error_details;
  std::unique_ptr<quic::ProofVerifyDetails> details;
  std::vector<absl::string_view> certs;
  EXPECT_EQ(quic::QUIC_FAILURE,
            verifyQuicClientCertChain(certs, serverContext(), &error_details, &details));
  ASSERT_NE(details, nullptr);
  EXPECT_FALSE(static_cast<CertVerifyResult&>(*details).isValid());
  EXPECT_THAT(error_details, testing::HasSubstr("client certificate required but not provided"));
}

// A chain that is not valid DER is rejected with a parse error.
TEST_F(EnvoyQuicDownstreamCertVerifierTest, MalformedCertificateRejected) {
  std::string error_details;
  std::unique_ptr<quic::ProofVerifyDetails> details;
  std::vector<absl::string_view> certs{"not-a-valid-der-certificate"};
  EXPECT_EQ(quic::QUIC_FAILURE,
            verifyQuicClientCertChain(certs, serverContext(), &error_details, &details));
  ASSERT_NE(details, nullptr);
  EXPECT_FALSE(static_cast<CertVerifyResult&>(*details).isValid());
  EXPECT_THAT(error_details, testing::HasSubstr("failed to parse client certificate 0"));
}

// A well-formed certificate that does not chain to the trusted CA is rejected.
TEST_F(EnvoyQuicDownstreamCertVerifierTest, UntrustedCertificateRejected) {
  std::stringstream pem_stream{std::string(quic::test::kTestCertificateChainPem)};
  std::vector<std::string> chain = quic::CertificateView::LoadPemFromStream(&pem_stream);
  ASSERT_FALSE(chain.empty());
  std::vector<absl::string_view> certs{chain[0]};

  std::string error_details;
  std::unique_ptr<quic::ProofVerifyDetails> details;
  EXPECT_EQ(quic::QUIC_FAILURE,
            verifyQuicClientCertChain(certs, serverContext(), &error_details, &details));
  ASSERT_NE(details, nullptr);
  EXPECT_FALSE(static_cast<CertVerifyResult&>(*details).isValid());
}

// A certificate that chains to the trusted CA is accepted and the details are marked valid.
TEST_F(EnvoyQuicDownstreamCertVerifierTest, TrustedCertificateAccepted) {
  const std::string pem = TestEnvironment::readFileToStringForTest(
      TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/san_uri_cert.pem"));
  std::stringstream pem_stream{pem};
  std::vector<std::string> chain = quic::CertificateView::LoadPemFromStream(&pem_stream);
  ASSERT_FALSE(chain.empty());
  std::vector<absl::string_view> certs{chain[0]};

  std::string error_details;
  std::unique_ptr<quic::ProofVerifyDetails> details;
  EXPECT_EQ(quic::QUIC_SUCCESS,
            verifyQuicClientCertChain(certs, serverContext(), &error_details, &details))
      << error_details;
  ASSERT_NE(details, nullptr);
  EXPECT_TRUE(static_cast<CertVerifyResult&>(*details).isValid());
  EXPECT_TRUE(error_details.empty());
}

// `CertVerifyResult::Clone` preserves the validity bit so `quiche's` internal copies carry the same
// decision.
TEST_F(EnvoyQuicDownstreamCertVerifierTest, CertVerifyResultClone) {
  CertVerifyResult valid(true);
  EXPECT_TRUE(valid.isValid());
  auto valid_cloned =
      std::unique_ptr<CertVerifyResult>(static_cast<CertVerifyResult*>(valid.Clone()));
  EXPECT_TRUE(valid_cloned->isValid());

  CertVerifyResult invalid(false);
  EXPECT_FALSE(invalid.isValid());
  auto invalid_cloned =
      std::unique_ptr<CertVerifyResult>(static_cast<CertVerifyResult*>(invalid.Clone()));
  EXPECT_FALSE(invalid_cloned->isValid());
}

} // namespace Quic
} // namespace Envoy
