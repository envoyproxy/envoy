#include <openssl/ssl3.h>

#include "envoy/network/transport_socket.h"
#include "envoy/ssl/handshaker.h"

#include "common/stream_info/stream_info_impl.h"

#include "extensions/transport_sockets/tls/ssl_handshaker.h"

#include "test/extensions/transport_sockets/tls/ssl_certs_test.h"
#include "test/mocks/network/connection.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/evp.h"
#include "openssl/hmac.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace {

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

// A callback shaped like pem_password_cb.
// See https://www.openssl.org/docs/man1.1.0/man3/pem_password_cb.html.
int pemPasswordCallback(char* buf, int buf_size, int, void* u) {
  if (u == nullptr) {
    return 0;
  }
  std::string passphrase = *reinterpret_cast<std::string*>(u);
  RELEASE_ASSERT(buf_size >= static_cast<int>(passphrase.size()),
                 "Passphrase was larger than buffer.");
  memcpy(buf, passphrase.data(), passphrase.size());
  return passphrase.size();
}

class MockHandshakeCallbacks : public Ssl::HandshakeCallbacks {
public:
  ~MockHandshakeCallbacks() override = default;
  MOCK_METHOD(Network::Connection&, connection, (), (const, override));
  MOCK_METHOD(void, onSuccess, (SSL*), (override));
  MOCK_METHOD(void, onFailure, (), (override));
  MOCK_METHOD(Network::TransportSocketCallbacks*, transportSocketCallbacks, (), (override));
};

class HandshakerTest : public SslCertsTest {
protected:
  HandshakerTest()
      : dispatcher_(api_->allocateDispatcher("test_thread")), stream_info_(api_->timeSource()),
        client_ctx_(SSL_CTX_new(TLS_method())), server_ctx_(SSL_CTX_new(TLS_method())) {}

  void SetUp() override {
    // Set up key and cert, initialize two SSL objects and a pair of BIOs for
    // handshaking.
    auto key = makeKey();
    auto cert = makeCert();
    auto chain = std::vector<CRYPTO_BUFFER*>{cert.get()};

    server_ssl_ = bssl::UniquePtr<SSL>(SSL_new(server_ctx_.get()));
    SSL_set_accept_state(server_ssl_.get());
    ASSERT_NE(key, nullptr);
    ASSERT_EQ(1, SSL_set_chain_and_key(server_ssl_.get(), chain.data(), chain.size(), key.get(),
                                       nullptr));

    client_ssl_ = bssl::UniquePtr<SSL>(SSL_new(client_ctx_.get()));
    SSL_set_connect_state(client_ssl_.get());

    ASSERT_EQ(1, BIO_new_bio_pair(&client_bio_, kBufferLength, &server_bio_, kBufferLength));

    BIO_up_ref(client_bio_);
    BIO_up_ref(server_bio_);
    SSL_set0_rbio(client_ssl_.get(), client_bio_);
    SSL_set0_wbio(client_ssl_.get(), client_bio_);
    SSL_set0_rbio(server_ssl_.get(), server_bio_);
    SSL_set0_wbio(server_ssl_.get(), server_bio_);
  }

  // Read in key.pem and return a new private key.
  bssl::UniquePtr<EVP_PKEY> makeKey() {
    std::string file = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/unittest_key.pem"));
    std::string passphrase = "";
    bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(file.data(), file.size()));

    bssl::UniquePtr<EVP_PKEY> key(EVP_PKEY_new());

    RSA* rsa = PEM_read_bio_RSAPrivateKey(bio.get(), nullptr, &pemPasswordCallback, &passphrase);
    RELEASE_ASSERT(rsa != nullptr, "PEM_read_bio_RSAPrivateKey failed.");
    RELEASE_ASSERT(1 == EVP_PKEY_assign_RSA(key.get(), rsa), "EVP_PKEY_assign_RSA failed.");
    return key;
  }

  // Read in cert.pem and return a certificate.
  bssl::UniquePtr<CRYPTO_BUFFER> makeCert() {
    std::string file = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/unittest_cert.pem"));
    bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(file.data(), file.size()));

    uint8_t* data = nullptr;
    long len = 0;
    RELEASE_ASSERT(
        PEM_bytes_read_bio(&data, &len, nullptr, PEM_STRING_X509, bio.get(), nullptr, nullptr),
        "PEM_bytes_read_bio failed");
    bssl::UniquePtr<uint8_t> tmp(data); // Prevents memory leak.
    return bssl::UniquePtr<CRYPTO_BUFFER>(CRYPTO_BUFFER_new(data, len, nullptr));
  }

  const size_t kBufferLength{100};

  Event::DispatcherPtr dispatcher_;
  StreamInfo::StreamInfoImpl stream_info_;

  BIO *client_bio_, *server_bio_;
  bssl::UniquePtr<SSL_CTX> client_ctx_, server_ctx_;
  bssl::UniquePtr<SSL> client_ssl_, server_ssl_;
};

TEST_F(HandshakerTest, NormalOperation) {
  NiceMock<Network::MockConnection> mock_connection;
  ON_CALL(mock_connection, state).WillByDefault(Return(Network::Connection::State::Closed));

  NiceMock<MockHandshakeCallbacks> handshake_callbacks;
  EXPECT_CALL(handshake_callbacks, onSuccess).Times(1);
  ON_CALL(handshake_callbacks, connection()).WillByDefault(ReturnRef(mock_connection));

  SslHandshakerImpl handshaker(std::move(server_ssl_), 0, &handshake_callbacks);

  auto post_io_action = Network::PostIoAction::KeepOpen; // default enum

  // Run the handshakes from the client and server until SslHandshakerImpl decides
  // we're done and returns PostIoAction::Close.
  while (post_io_action != Network::PostIoAction::Close) {
    SSL_do_handshake(client_ssl_.get());
    post_io_action = handshaker.doHandshake();
  }

  EXPECT_EQ(post_io_action, Network::PostIoAction::Close);
}

// We induce some kind of BIO mismatch and force the SSL_do_handshake to
// return an error code without error handling, i.e. not SSL_ERROR_WANT_READ
// or _WRITE or _PRIVATE_KEY_OPERATION.
TEST_F(HandshakerTest, ErrorCbOnAbnormalOperation) {
  // We make a new BIO, set it as the `rbio`/`wbio` for the client SSL object,
  // and break the BIO pair connecting the two SSL objects. Now handshaking will
  // fail, likely with SSL_ERROR_SSL.
  BIO* bio = BIO_new(BIO_s_socket());
  SSL_set_bio(client_ssl_.get(), bio, bio);

  StrictMock<MockHandshakeCallbacks> handshake_callbacks;
  EXPECT_CALL(handshake_callbacks, onFailure).Times(1);

  SslHandshakerImpl handshaker(std::move(server_ssl_), 0, &handshake_callbacks);

  auto post_io_action = Network::PostIoAction::KeepOpen; // default enum

  while (post_io_action != Network::PostIoAction::Close) {
    SSL_do_handshake(client_ssl_.get());
    post_io_action = handshaker.doHandshake();
  }

  // In the error case, SslHandshakerImpl also closes the connection.
  EXPECT_EQ(post_io_action, Network::PostIoAction::Close);
}

// Example SslHandshakerImpl demonstrating special-case behavior which necessitates
// extra SSL_ERROR case handling. Here, we induce an SSL_ERROR_WANT_X509_LOOKUP,
// check for it in the handshaker, faux-trigger the lookup, and then proceed as
// normal.
class SslHandshakerImplForTest : public SslHandshakerImpl {
public:
  SslHandshakerImplForTest(bssl::UniquePtr<SSL> ssl_ptr,
                           Ssl::HandshakeCallbacks* handshake_callbacks,
                           std::function<void()> requested_cert_cb)
      : SslHandshakerImpl(std::move(ssl_ptr), 0, handshake_callbacks),
        requested_cert_cb_(requested_cert_cb) {
    SSL_set_cert_cb(
        ssl(), [](SSL*, void* arg) -> int { return *static_cast<bool*>(arg) ? 1 : -1; },
        &cert_cb_ok_);
  }

  Network::PostIoAction doHandshake() override {
    RELEASE_ASSERT(state() != Ssl::SocketState::HandshakeComplete &&
                       state() != Ssl::SocketState::ShutdownSent,
                   "Handshaker state was either complete or sent.");

    int rc = SSL_do_handshake(ssl());
    if (rc == 1) {
      setState(Ssl::SocketState::HandshakeComplete);
      handshakeCallbacks()->onSuccess(ssl());
      return Network::PostIoAction::Close;
    } else {
      switch (SSL_get_error(ssl(), rc)) {
      case SSL_ERROR_WANT_READ:
      case SSL_ERROR_WANT_WRITE:
        return Network::PostIoAction::KeepOpen;
      case SSL_ERROR_WANT_X509_LOOKUP:
        // Special case. Once this lookup is requested, we flip the bit and allow
        // the handshake to proceed.
        requested_cert_cb_();
        return Network::PostIoAction::KeepOpen;
      default:
        handshakeCallbacks()->onFailure();
        return Network::PostIoAction::Close;
      }
    }
  }

  void setCertCbOk() { cert_cb_ok_ = true; }

private:
  std::function<void()> requested_cert_cb_;
  bool cert_cb_ok_{false};
};

TEST_F(HandshakerTest, NormalOperationWithSslHandshakerImplForTest) {
  ::testing::MockFunction<void()> requested_cert_cb;

  StrictMock<MockHandshakeCallbacks> handshake_callbacks;
  EXPECT_CALL(handshake_callbacks, onSuccess).Times(1);

  SslHandshakerImplForTest handshaker(std::move(server_ssl_), &handshake_callbacks,
                                      requested_cert_cb.AsStdFunction());

  EXPECT_CALL(requested_cert_cb, Call).WillOnce([&]() { handshaker.setCertCbOk(); });

  auto post_io_action = Network::PostIoAction::KeepOpen; // default enum

  while (post_io_action != Network::PostIoAction::Close) {
    SSL_do_handshake(client_ssl_.get());
    post_io_action = handshaker.doHandshake();
  }

  EXPECT_EQ(post_io_action, Network::PostIoAction::Close);
}

} // namespace
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
