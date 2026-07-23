#include <openssl/ssl.h>

#include "source/common/quic/quic_ssl_connection_info.h"

#include "test/common/tls/ssl_test_utility.h"
#include "test/common/tls/test_data/ca_cert_info.h"
#include "test/common/tls/test_data/san_uri_cert_info.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

namespace Envoy {
namespace Quic {
namespace {

// QuicSslConnectionInfo whose SSL object is supplied by the test instead of a QUIC crypto stream.
class TestQuicSslConnectionInfo : public QuicSslConnectionInfo {
public:
  TestQuicSslConnectionInfo(quic::QuicSession& session, SSL* ssl)
      : QuicSslConnectionInfo(session), ssl_(ssl) {}

  SSL* ssl() const override { return ssl_; }

  // Simulates QUICHE releasing the SSL object after the handshake completion is acknowledged
  // (reset-after-handshake).
  void clearSsl() { ssl_ = nullptr; }

private:
  SSL* ssl_;
};

// Exercises QuicSslConnectionInfo against an SSL object created with the same
// CRYPTO_BUFFER-based method QUICHE uses (TLS_with_buffers_method), performing a real in-memory
// handshake so the peer certificates are delivered the same way as on a QUIC connection.
class QuicSslConnectionInfoTest : public testing::Test {
protected:
  QuicSslConnectionInfoTest()
      : connection_(new quic::test::MockQuicConnection(&helper_, &alarm_factory_,
                                                       quic::Perspective::IS_SERVER)),
        session_(connection_) {}

  static std::string testDataPath(absl::string_view file) {
    return TestEnvironment::substitute(
        absl::StrCat("{{ test_rundir }}/test/common/tls/test_data/", file));
  }

  // Loads a PEM cert chain + key into a CRYPTO_BUFFER-based SSL_CTX via the buffers-friendly API.
  static void setCertChainAndKey(SSL_CTX* ctx, absl::string_view cert_file,
                                 absl::string_view key_file,
                                 absl::string_view extra_chain_file = "") {
    std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> buffers;
    auto append_certs = [&buffers](const std::string& path) {
      bssl::UniquePtr<STACK_OF(X509)> certs =
          Extensions::TransportSockets::Tls::readCertChainFromFile(path);
      for (size_t i = 0; i < sk_X509_num(certs.get()); i++) {
        uint8_t* der = nullptr;
        const int len = i2d_X509(sk_X509_value(certs.get(), i), &der);
        ASSERT_GT(len, 0);
        bssl::UniquePtr<uint8_t> free_der(der);
        buffers.emplace_back(CRYPTO_BUFFER_new(der, len, nullptr));
        ASSERT_NE(buffers.back(), nullptr);
      }
    };
    append_certs(testDataPath(cert_file));
    if (!extra_chain_file.empty()) {
      append_certs(testDataPath(extra_chain_file));
    }

    const std::string key_content =
        TestEnvironment::readFileToStringForTest(testDataPath(key_file));
    bssl::UniquePtr<BIO> key_bio(BIO_new_mem_buf(key_content.data(), key_content.size()));
    bssl::UniquePtr<EVP_PKEY> key(
        PEM_read_bio_PrivateKey(key_bio.get(), nullptr, nullptr, nullptr));
    ASSERT_NE(key, nullptr);

    std::vector<CRYPTO_BUFFER*> raw_buffers;
    raw_buffers.reserve(buffers.size());
    for (auto& buffer : buffers) {
      raw_buffers.push_back(buffer.get());
    }
    ASSERT_EQ(
        SSL_CTX_set_chain_and_key(ctx, raw_buffers.data(), raw_buffers.size(), key.get(), nullptr),
        1);
  }

  // Creates the SSL pair. The server always requests a client certificate; whether the client
  // presents one depends on with_client_cert.
  void createSslPair(bool with_client_cert, absl::string_view client_extra_chain = "") {
    client_ctx_.reset(SSL_CTX_new(TLS_with_buffers_method()));
    server_ctx_.reset(SSL_CTX_new(TLS_with_buffers_method()));
    setCertChainAndKey(server_ctx_.get(), "no_san_cert.pem", "no_san_key.pem");
    if (with_client_cert) {
      setCertChainAndKey(client_ctx_.get(), "san_uri_cert.pem", "san_uri_key.pem",
                         client_extra_chain);
    }
    auto accept_all = [](SSL*, uint8_t*) { return ssl_verify_ok; };
    SSL_CTX_set_custom_verify(client_ctx_.get(), SSL_VERIFY_PEER, accept_all);
    SSL_CTX_set_custom_verify(server_ctx_.get(), SSL_VERIFY_PEER, accept_all);

    client_ssl_.reset(SSL_new(client_ctx_.get()));
    server_ssl_.reset(SSL_new(server_ctx_.get()));
    BIO* client_bio = nullptr;
    BIO* server_bio = nullptr;
    ASSERT_EQ(BIO_new_bio_pair(&client_bio, 0, &server_bio, 0), 1);
    SSL_set_bio(client_ssl_.get(), client_bio, client_bio);
    SSL_set_bio(server_ssl_.get(), server_bio, server_bio);
    SSL_set_connect_state(client_ssl_.get());
    SSL_set_accept_state(server_ssl_.get());

    ssl_info_ = std::make_unique<TestQuicSslConnectionInfo>(session_, server_ssl_.get());
  }

  void completeHandshake() {
    bool client_done = false;
    bool server_done = false;
    for (int i = 0; i < 100 && !(client_done && server_done); i++) {
      if (!client_done && SSL_do_handshake(client_ssl_.get()) == 1) {
        client_done = true;
      }
      if (!server_done && SSL_do_handshake(server_ssl_.get()) == 1) {
        server_done = true;
      }
    }
    ASSERT_TRUE(client_done && server_done);
  }

  quic::test::MockQuicConnectionHelper helper_;
  quic::test::MockAlarmFactory alarm_factory_;
  quic::test::MockQuicConnection* connection_; // Owned by session_.
  quic::test::MockQuicSession session_;
  bssl::UniquePtr<SSL_CTX> client_ctx_;
  bssl::UniquePtr<SSL_CTX> server_ctx_;
  bssl::UniquePtr<SSL> client_ssl_;
  bssl::UniquePtr<SSL> server_ssl_;
  std::unique_ptr<TestQuicSslConnectionInfo> ssl_info_;
};

TEST_F(QuicSslConnectionInfoTest, PeerCertificateAccessors) {
  createSslPair(/*with_client_cert=*/true);
  completeHandshake();

  EXPECT_TRUE(ssl_info_->peerCertificatePresented());
  EXPECT_EQ(TEST_SAN_URI_CERT_256_HASH, ssl_info_->sha256PeerCertificateDigest());
  EXPECT_EQ(TEST_SAN_URI_CERT_1_HASH, ssl_info_->sha1PeerCertificateDigest());
  EXPECT_EQ(TEST_SAN_URI_CERT_SERIAL, ssl_info_->serialNumberPeerCertificate());
  EXPECT_EQ(std::vector<std::string>{TEST_SAN_URI_CERT_256_HASH},
            ssl_info_->sha256PeerCertificateChainDigests());
  EXPECT_EQ(std::vector<std::string>{TEST_SAN_URI_CERT_SERIAL},
            ssl_info_->serialNumbersPeerCertificates());
  EXPECT_EQ(std::vector<std::string>{"spiffe://lyft.com/test-team"},
            ssl_info_->uriSanPeerCertificate());
  EXPECT_TRUE(ssl_info_->dnsSansPeerCertificate().empty());
  EXPECT_FALSE(ssl_info_->subjectPeerCertificate().empty());
  EXPECT_FALSE(ssl_info_->issuerPeerCertificate().empty());
  EXPECT_TRUE(ssl_info_->parsedSubjectPeerCertificate().has_value());
  EXPECT_FALSE(ssl_info_->pemEncodedPeerCertificate().empty());
  EXPECT_FALSE(ssl_info_->urlEncodedPemEncodedPeerCertificate().empty());
  EXPECT_FALSE(ssl_info_->urlEncodedPemEncodedPeerCertificateChain().empty());
  EXPECT_EQ(1, ssl_info_->pemEncodedPeerCertificateChain().size());
  EXPECT_TRUE(ssl_info_->validFromPeerCertificate().has_value());
  EXPECT_TRUE(ssl_info_->expirationPeerCertificate().has_value());

  // Local certificate accessors are not supported on QUIC connections.
  EXPECT_TRUE(ssl_info_->subjectLocalCertificate().empty());
  EXPECT_TRUE(ssl_info_->uriSanLocalCertificate().empty());
  EXPECT_TRUE(ssl_info_->dnsSansLocalCertificate().empty());
  EXPECT_TRUE(ssl_info_->ipSansLocalCertificate().empty());
  EXPECT_TRUE(ssl_info_->emailSansLocalCertificate().empty());
  EXPECT_TRUE(ssl_info_->othernameSansLocalCertificate().empty());
  EXPECT_TRUE(ssl_info_->oidsLocalCertificate().empty());
}

TEST_F(QuicSslConnectionInfoTest, NoPeerCertificate) {
  createSslPair(/*with_client_cert=*/false);
  completeHandshake();

  EXPECT_FALSE(ssl_info_->peerCertificatePresented());
  EXPECT_TRUE(ssl_info_->sha256PeerCertificateDigest().empty());
  EXPECT_TRUE(ssl_info_->uriSanPeerCertificate().empty());
  EXPECT_TRUE(ssl_info_->subjectPeerCertificate().empty());
  EXPECT_TRUE(ssl_info_->pemEncodedPeerCertificateChain().empty());
  EXPECT_FALSE(ssl_info_->validFromPeerCertificate().has_value());
  EXPECT_FALSE(ssl_info_->expirationPeerCertificate().has_value());
}

// Values queried before the handshake completes are empty, and get recomputed once the peer
// certificates become available.
TEST_F(QuicSslConnectionInfoTest, PreHandshakeQueryRecomputedAfterHandshake) {
  createSslPair(/*with_client_cert=*/true);

  EXPECT_FALSE(ssl_info_->peerCertificatePresented());
  EXPECT_TRUE(ssl_info_->sha256PeerCertificateDigest().empty());
  EXPECT_TRUE(ssl_info_->uriSanPeerCertificate().empty());

  completeHandshake();

  EXPECT_TRUE(ssl_info_->peerCertificatePresented());
  EXPECT_EQ(TEST_SAN_URI_CERT_256_HASH, ssl_info_->sha256PeerCertificateDigest());
  EXPECT_EQ(std::vector<std::string>{"spiffe://lyft.com/test-team"},
            ssl_info_->uriSanPeerCertificate());
}

// The issuer accessors are served from the chain built during verification (passed to
// onCertValidated()), not from the peer-presented chain.
TEST_F(QuicSslConnectionInfoTest, ValidatedIssuerFromValidatedChain) {
  createSslPair(/*with_client_cert=*/true);
  completeHandshake();

  std::vector<bssl::UniquePtr<X509>> validated_chain;
  validated_chain.push_back(
      Extensions::TransportSockets::Tls::readCertFromFile(testDataPath("san_uri_cert.pem")));
  validated_chain.push_back(
      Extensions::TransportSockets::Tls::readCertFromFile(testDataPath("ca_cert.pem")));
  ASSERT_NE(validated_chain[0], nullptr);
  ASSERT_NE(validated_chain[1], nullptr);
  ssl_info_->onCertValidated(validated_chain);

  EXPECT_TRUE(ssl_info_->peerCertificateValidated());
  EXPECT_EQ(TEST_CA_CERT_256_HASH, ssl_info_->sha256PeerCertificateIssuerDigest());
  EXPECT_EQ(TEST_CA_CERT_SERIAL, ssl_info_->serialNumberPeerCertificateIssuer());

  // The validated chain is exposed through validatedPeerCertChain() and the PEM accessor built
  // on it.
  ASSERT_TRUE(ssl_info_->validatedPeerCertChain().has_value());
  EXPECT_EQ(2, ssl_info_->validatedPeerCertChain()->size());
  EXPECT_EQ(2, ssl_info_->pemEncodedValidatedPeerCertificateChain().size());
}

// Security regression test: a peer that presents extra certificates does NOT influence the
// validated-issuer accessors. Without a validated chain the issuer is unknown, even though the
// peer sent a full chain in the handshake. This prevents a peer from controlling what is reported
// as the certificate issuer (used in RBAC and access logs).
TEST_F(QuicSslConnectionInfoTest, ValidatedIssuerIgnoresPresentedChain) {
  createSslPair(/*with_client_cert=*/true, /*client_extra_chain=*/"ca_cert.pem");
  completeHandshake();

  // The peer presented a two-cert chain...
  EXPECT_EQ(2, ssl_info_->pemEncodedPeerCertificateChain().size());
  // ...but no validated chain was set, so the issuer and validated-chain accessors report
  // nothing.
  EXPECT_TRUE(ssl_info_->sha256PeerCertificateIssuerDigest().empty());
  EXPECT_TRUE(ssl_info_->serialNumberPeerCertificateIssuer().empty());
  EXPECT_FALSE(ssl_info_->validatedPeerCertChain().has_value());
  EXPECT_TRUE(ssl_info_->pemEncodedValidatedPeerCertificateChain().empty());

  // A validated chain containing only the leaf still yields no issuer.
  std::vector<bssl::UniquePtr<X509>> leaf_only;
  leaf_only.push_back(
      Extensions::TransportSockets::Tls::readCertFromFile(testDataPath("san_uri_cert.pem")));
  ASSERT_NE(leaf_only[0], nullptr);
  ssl_info_->onCertValidated(leaf_only);
  EXPECT_TRUE(ssl_info_->sha256PeerCertificateIssuerDigest().empty());
  EXPECT_TRUE(ssl_info_->serialNumberPeerCertificateIssuer().empty());
}

// When reset-after-handshake is enabled the peer chain is cached at handshake completion, so
// peer certificate queries keep working after the SSL object has been released.
TEST_F(QuicSslConnectionInfoTest, PeerChainCachedBeforeSslRelease) {
  createSslPair(/*with_client_cert=*/true);
  completeHandshake();

  ssl_info_->cachePeerCertificateChain();
  ssl_info_->clearSsl();

  EXPECT_TRUE(ssl_info_->peerCertificatePresented());
  EXPECT_EQ(TEST_SAN_URI_CERT_256_HASH, ssl_info_->sha256PeerCertificateDigest());
  EXPECT_EQ(std::vector<std::string>{"spiffe://lyft.com/test-team"},
            ssl_info_->uriSanPeerCertificate());
  EXPECT_FALSE(ssl_info_->subjectPeerCertificate().empty());
  EXPECT_EQ(1, ssl_info_->pemEncodedPeerCertificateChain().size());
}

// Same as above without a client certificate: the cache records that no chain was presented, so
// queries after the SSL object has been released return empty instead of touching it.
TEST_F(QuicSslConnectionInfoTest, NoPeerChainCachedBeforeSslRelease) {
  createSslPair(/*with_client_cert=*/false);
  completeHandshake();

  ssl_info_->cachePeerCertificateChain();
  ssl_info_->clearSsl();

  EXPECT_FALSE(ssl_info_->peerCertificatePresented());
  EXPECT_TRUE(ssl_info_->sha256PeerCertificateDigest().empty());
  EXPECT_TRUE(ssl_info_->subjectPeerCertificate().empty());
  EXPECT_TRUE(ssl_info_->pemEncodedPeerCertificateChain().empty());
}

// peerCertificateValidated() only flips when the handshake reports successful validation.
TEST_F(QuicSslConnectionInfoTest, CertValidatedFlag) {
  createSslPair(/*with_client_cert=*/true);
  completeHandshake();

  EXPECT_FALSE(ssl_info_->peerCertificateValidated());
  ssl_info_->onCertValidated();
  EXPECT_TRUE(ssl_info_->peerCertificateValidated());
}

} // namespace
} // namespace Quic
} // namespace Envoy
