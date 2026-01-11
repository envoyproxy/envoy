#include "source/common/quic/envoy_quic_proof_verifier.h"
#include "source/common/quic/envoy_quic_server_proof_verifier.h"
#include "source/common/quic/quic_server_transport_socket_factory.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/quic/test_tools/test_certificates.h"

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

  Network::DownstreamTransportSocketFactoryPtr createQuicServerFactory(const std::string& yaml) {
    envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    return THROW_OR_RETURN_VALUE(
        config_factory_.createTransportSocketFactory(proto_config, context_, {}),
        Network::DownstreamTransportSocketFactoryPtr);
  }

protected:
  NiceMock<Network::MockListenSocket> listen_socket_;
  NiceMock<Network::MockFilterChainManager> filter_chain_manager_;
  Stats::TestUtil::TestStore server_stats_store_;
  Api::ApiPtr server_api_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
  QuicServerTransportSocketConfigFactory config_factory_;
};

// Test that verifier returns success when no filter chain is found.
TEST_F(EnvoyQuicServerProofVerifierTest, NoFilterChainFound) {
  auto verifier = std::make_unique<EnvoyQuicServerProofVerifier>(listen_socket_,
                                                                 filter_chain_manager_, simTime());

  EXPECT_CALL(filter_chain_manager_, findFilterChain(_, _)).WillOnce(Return(nullptr));

  std::string error_details;
  std::unique_ptr<quic::ProofVerifyDetails> details;
  std::vector<std::string> certs;

  quic::QuicAsyncStatus status = verifier->VerifyCertChain(
      "hostname", 443, certs, "", "", nullptr, &error_details, &details, nullptr, nullptr);

  EXPECT_EQ(quic::QUIC_SUCCESS, status);
  EXPECT_NE(details, nullptr);
  EXPECT_TRUE(static_cast<CertVerifyResult&>(*details).isValid());
}

// Test that verifier returns success when transport socket factory is not QUIC type.
TEST_F(EnvoyQuicServerProofVerifierTest, NonQuicTransportSocketFactory) {
  auto verifier = std::make_unique<EnvoyQuicServerProofVerifier>(listen_socket_,
                                                                 filter_chain_manager_, simTime());

  NiceMock<Network::MockFilterChain> filter_chain;
  NiceMock<Network::MockDownstreamTransportSocketFactory> socket_factory;

  EXPECT_CALL(filter_chain_manager_, findFilterChain(_, _)).WillOnce(Return(&filter_chain));
  EXPECT_CALL(filter_chain, transportSocketFactory()).WillOnce(ReturnRef(socket_factory));

  std::string error_details;
  std::unique_ptr<quic::ProofVerifyDetails> details;
  std::vector<std::string> certs;

  quic::QuicAsyncStatus status = verifier->VerifyCertChain(
      "hostname", 443, certs, "", "", nullptr, &error_details, &details, nullptr, nullptr);

  // Not a QUIC factory, so should return success.
  EXPECT_EQ(quic::QUIC_SUCCESS, status);
  EXPECT_NE(details, nullptr);
  EXPECT_TRUE(static_cast<CertVerifyResult&>(*details).isValid());
}

// Test updateFilterChainManager method.
TEST_F(EnvoyQuicServerProofVerifierTest, UpdateFilterChainManager) {
  auto verifier = std::make_unique<EnvoyQuicServerProofVerifier>(listen_socket_,
                                                                 filter_chain_manager_, simTime());

  NiceMock<Network::MockFilterChainManager> new_filter_chain_manager;

  // Update to a new filter chain manager.
  verifier->updateFilterChainManager(new_filter_chain_manager);

  // Verify the new filter chain manager is used.
  EXPECT_CALL(new_filter_chain_manager, findFilterChain(_, _)).WillOnce(Return(nullptr));

  std::string error_details;
  std::unique_ptr<quic::ProofVerifyDetails> details;
  std::vector<std::string> certs;

  quic::QuicAsyncStatus status = verifier->VerifyCertChain(
      "hostname", 443, certs, "", "", nullptr, &error_details, &details, nullptr, nullptr);

  EXPECT_EQ(quic::QUIC_SUCCESS, status);
}

// Test CertVerifyResult cloning functionality.
TEST_F(EnvoyQuicServerProofVerifierTest, CertVerifyResultClone) {
  CertVerifyResult result_valid(true);
  EXPECT_TRUE(result_valid.isValid());

  auto cloned =
      std::unique_ptr<CertVerifyResult>(static_cast<CertVerifyResult*>(result_valid.Clone()));
  EXPECT_TRUE(cloned->isValid());

  CertVerifyResult result_invalid(false);
  EXPECT_FALSE(result_invalid.isValid());

  auto cloned_invalid =
      std::unique_ptr<CertVerifyResult>(static_cast<CertVerifyResult*>(result_invalid.Clone()));
  EXPECT_FALSE(cloned_invalid->isValid());
}

} // namespace Quic
} // namespace Envoy
