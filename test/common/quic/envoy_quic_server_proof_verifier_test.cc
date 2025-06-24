#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/quic/envoy_quic_server_proof_verifier.h"
#include "source/common/quic/quic_server_transport_socket_factory.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Quic {

class EnvoyQuicServerProofVerifierTest : public Event::TestUsingSimulatedTime,
                                         public testing::Test {
public:
  EnvoyQuicServerProofVerifierTest()
      : server_api_(Api::createApiForTest(server_stats_store_, simTime())) {
    ON_CALL(context_.server_context_, api()).WillByDefault(ReturnRef(*server_api_));
    ON_CALL(context_.server_context_, threadLocal()).WillByDefault(ReturnRef(tls_));
  }

protected:
  NiceMock<Network::MockListenSocket> listen_socket_;
  NiceMock<Network::MockFilterChainManager> filter_chain_manager_;
  Stats::TestUtil::TestStore server_stats_store_;
  Api::ApiPtr server_api_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
};

// The verifier must reject the handshake when no filter chain matches.
TEST_F(EnvoyQuicServerProofVerifierTest, NoFilterChainFound) {
  auto verifier = std::make_unique<EnvoyQuicServerProofVerifier>(listen_socket_,
                                                                 filter_chain_manager_, simTime());
  EXPECT_CALL(filter_chain_manager_, findFilterChain(_, _)).WillOnce(Return(nullptr));

  std::string error_details;
  std::unique_ptr<quic::ProofVerifyDetails> details;
  std::vector<absl::string_view> certs;
  quic::QuicAsyncStatus status = verifier->VerifyCertChain(
      "hostname", 443, certs, "", "", nullptr, &error_details, &details, nullptr, nullptr);

  EXPECT_EQ(quic::QUIC_FAILURE, status);
  ASSERT_NE(details, nullptr);
  EXPECT_FALSE(static_cast<CertVerifyResult&>(*details).isValid());
  EXPECT_THAT(error_details, testing::HasSubstr("no filter chain matched"));
}

// The verifier must reject when the matched filter chain is not a QUIC
// transport socket factory.
TEST_F(EnvoyQuicServerProofVerifierTest, NonQuicTransportSocketFactory) {
  auto verifier = std::make_unique<EnvoyQuicServerProofVerifier>(listen_socket_,
                                                                 filter_chain_manager_, simTime());
  NiceMock<Network::MockFilterChain> filter_chain;
  NiceMock<Network::MockDownstreamTransportSocketFactory> socket_factory;
  EXPECT_CALL(filter_chain_manager_, findFilterChain(_, _)).WillOnce(Return(&filter_chain));
  EXPECT_CALL(filter_chain, transportSocketFactory()).WillOnce(ReturnRef(socket_factory));

  std::string error_details;
  std::unique_ptr<quic::ProofVerifyDetails> details;
  std::vector<absl::string_view> certs;
  quic::QuicAsyncStatus status = verifier->VerifyCertChain(
      "hostname", 443, certs, "", "", nullptr, &error_details, &details, nullptr, nullptr);

  EXPECT_EQ(quic::QUIC_FAILURE, status);
  ASSERT_NE(details, nullptr);
  EXPECT_FALSE(static_cast<CertVerifyResult&>(*details).isValid());
}

// Updating the filter chain manager redirects subsequent matches to the new manager.
TEST_F(EnvoyQuicServerProofVerifierTest, UpdateFilterChainManager) {
  auto verifier = std::make_unique<EnvoyQuicServerProofVerifier>(listen_socket_,
                                                                 filter_chain_manager_, simTime());
  NiceMock<Network::MockFilterChainManager> new_filter_chain_manager;
  verifier->updateFilterChainManager(new_filter_chain_manager);

  EXPECT_CALL(new_filter_chain_manager, findFilterChain(_, _)).WillOnce(Return(nullptr));
  std::string error_details;
  std::unique_ptr<quic::ProofVerifyDetails> details;
  std::vector<absl::string_view> certs;
  quic::QuicAsyncStatus status = verifier->VerifyCertChain(
      "hostname", 443, certs, "", "", nullptr, &error_details, &details, nullptr, nullptr);

  EXPECT_EQ(quic::QUIC_FAILURE, status);
}

// Disabling the `quic_mtls_server_enabled` runtime guard rejects every
// handshake with an explanatory error.
TEST_F(EnvoyQuicServerProofVerifierTest, RuntimeGuardDisabledRejects) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.quic_mtls_server_enabled", "false"}});

  auto verifier = std::make_unique<EnvoyQuicServerProofVerifier>(listen_socket_,
                                                                 filter_chain_manager_, simTime());
  std::string error_details;
  std::unique_ptr<quic::ProofVerifyDetails> details;
  std::vector<absl::string_view> certs;
  quic::QuicAsyncStatus status = verifier->VerifyCertChain(
      "hostname", 443, certs, "", "", nullptr, &error_details, &details, nullptr, nullptr);

  EXPECT_EQ(quic::QUIC_FAILURE, status);
  ASSERT_NE(details, nullptr);
  EXPECT_FALSE(static_cast<CertVerifyResult&>(*details).isValid());
  EXPECT_THAT(error_details, testing::HasSubstr("quic_mtls_server_enabled"));
}

// `CertVerifyResult::Clone` preserves the validity bit so `quiche's` internal
// copies carry the same decision.
TEST_F(EnvoyQuicServerProofVerifierTest, CertVerifyResultClone) {
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
