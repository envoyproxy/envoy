#include <openssl/ssl.h>

#include "envoy/registry/registry.h"

#include "source/common/quic/envoy_quic_proof_source.h"
#include "source/common/quic/envoy_quic_proof_source_factory_interface.h"
#include "source/common/tls/connection_info_impl_base.h"

#include "test/integration/http_protocol_integration.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace {

// A proof source for the fake upstream which requests, but does not validate, a client
// certificate on every connection: QUICHE installs no certificate verifier on the server SSL
// context by default, so installing a permissive one here makes every server connection send a
// CertificateRequest and accept whatever is (or isn't) presented.
class ClientCertRequestingProofSource : public Quic::EnvoyQuicProofSource {
public:
  using Quic::EnvoyQuicProofSource::EnvoyQuicProofSource;

  void OnNewSslCtx(SSL_CTX* ssl_ctx) override {
    Quic::EnvoyQuicProofSource::OnNewSslCtx(ssl_ctx);
    SSL_CTX_set_custom_verify(ssl_ctx, SSL_VERIFY_PEER,
                              [](SSL*, uint8_t*) { return ssl_verify_ok; });
  }
};

class ClientCertRequestingProofSourceFactory : public Quic::EnvoyQuicProofSourceFactoryInterface {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

  std::string name() const override {
    return "envoy.quic.proof_source.request_client_certificate_for_test";
  }

  std::unique_ptr<quic::ProofSource>
  createQuicProofSource(Network::Socket& listen_socket,
                        Network::FilterChainManager& filter_chain_manager,
                        Server::ListenerStats& listener_stats, TimeSource& time_source) override {
    return std::make_unique<ClientCertRequestingProofSource>(listen_socket, filter_chain_manager,
                                                             listener_stats, time_source);
  }
};

REGISTER_FACTORY(ClientCertRequestingProofSourceFactory,
                 Quic::EnvoyQuicProofSourceFactoryInterface);

// Tests that upstream QUIC connections present the configured client certificate. The fake
// upstream requests the certificate but does not validate it; validation is exercised by the
// server-side mTLS support in a follow-up.
class QuicUpstreamClientCertIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    upstream_tls_ = true;
    // Make the fake upstream request (but not validate) a client certificate.
    auto* proof_source_config = upstreamConfig().quic_options_.mutable_proof_source_config();
    proof_source_config->set_name("envoy.quic.proof_source.request_client_certificate_for_test");
    std::ignore = proof_source_config->mutable_typed_config()->PackFrom(Protobuf::Struct());

    // Configure the cluster with a client certificate.
    config_helper_.configureUpstreamTls(
        /*use_alpn=*/false, /*http3=*/true, /*alternate_protocol_cache_config=*/{},
        [](envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext& tls_context) {
          auto* certs = tls_context.mutable_common_tls_context()->add_tls_certificates();
          certs->mutable_certificate_chain()->set_filename(
              TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
          certs->mutable_private_key()->set_filename(
              TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
        });
    HttpProtocolIntegrationTest::initialize();
  }

  // Returns whether the fake upstream received a client certificate on the current upstream
  // connection. Runs on the fake upstream's connection thread for thread safety.
  bool upstreamSawClientCert() {
    bool cert_presented = false;
    absl::Notification done;
    Network::Connection& connection = fake_upstream_connection_->connection();
    fake_upstream_connection_->postToConnectionThread([&connection, &cert_presented, &done]() {
      auto ssl_info = std::dynamic_pointer_cast<
          const Extensions::TransportSockets::Tls::ConnectionInfoImplBase>(connection.ssl());
      ASSERT(ssl_info != nullptr);
      const STACK_OF(CRYPTO_BUFFER)* certs = SSL_get0_peer_certificates(ssl_info->ssl());
      cert_presented = certs != nullptr && sk_CRYPTO_BUFFER_num(certs) > 0;
      done.Notify();
    });
    done.WaitForNotification();
    return cert_presented;
  }

  void sendRequestAndExpectResponse() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("200", response->headers().getStatusValue());
  }
};

INSTANTIATE_TEST_SUITE_P(Protocols, QuicUpstreamClientCertIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1}, {Http::CodecType::HTTP3})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// The configured client certificate is presented on the upstream QUIC connection when the
// upstream requests one.
TEST_P(QuicUpstreamClientCertIntegrationTest, ClientCertificatePresented) {
  initialize();
  sendRequestAndExpectResponse();
  EXPECT_TRUE(upstreamSawClientCert());
}

// With the runtime guard disabled, the client certificate is not installed on the QUIC client
// SSL context, so nothing is presented (the previous behavior).
TEST_P(QuicUpstreamClientCertIntegrationTest, NoClientCertificateWhenRuntimeDisabled) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.quic_upstream_client_certificates",
                                    "false");
  initialize();
  sendRequestAndExpectResponse();
  EXPECT_FALSE(upstreamSawClientCert());
}

} // namespace
} // namespace Envoy
