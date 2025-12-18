#include <gtest/gtest.h>
#include <openssl/base.h>
#include <openssl/bytestring.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include <openssl/pem.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <future>

#include "certs/client_2_cert_chain.pem.h"
#include "certs/client_2_key.pem.h"
#include "certs/server_1_cert.pem.h"
#include "certs/server_1_key.pem.h"
#include "certs/server_2_cert.pem.h"
#include "certs/server_2_cert_chain.pem.h"
#include "certs/server_2_key.pem.h"
#include "certs/root_ca_cert.pem.h"
#include "certs/intermediate_ca_1_cert.pem.h"
#include "certs/intermediate_ca_2_cert.pem.h"


class TempFile {
  public:

    TempFile(const char *content) : m_path { "/tmp/XXXXXX" }
    {
      int fd { mkstemp(m_path.data()) };
      if (fd == -1) {
        perror("mkstemp()");
      }
      else {
        for (int n, written = 0, len = strlen(content); written < len; written += n) {
          if ((n = write(fd, content + written, len - written)) < 0) {
            if (errno == EINTR || errno == EAGAIN) {
              continue;
            }
            perror("write()");
          }
        }
        if (close(fd) == -1) {
          perror("close()");
        }
      }
    }

    ~TempFile() {
      if (unlink(m_path.c_str()) == -1) {
        perror("unlink()");
      }
    }

    const char *path() const {
      return m_path.c_str();
    }

  private:

    std::string m_path { "/tmp/XXXXXX" };
};



TEST(SSLTest, test_SSL_CTX_set_select_certificate_cb) {
  signal(SIGPIPE, SIG_IGN);

  std::promise<in_port_t> server_port;
  std::set<uint16_t> received_ext_types;

  // Start a TLS server with a (SSL_CTX_set_select_certificate_cb()) callback installed.
  std::thread server([&]() {
    bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_server_method()));
    SSL_CTX_set_ex_data(ctx.get(), 0, &received_ext_types); // So the callback can fill it in

    // Install the callback.
    SSL_CTX_set_select_certificate_cb(ctx.get(), [](const SSL_CLIENT_HELLO *client_hello) -> enum ssl_select_cert_result_t {
      std::set<uint16_t> *received_ext_types = static_cast<std::set<uint16_t>*>(SSL_CTX_get_ex_data(SSL_get_SSL_CTX(client_hello->ssl), 0));

      CBS extensions;
      CBS_init(&extensions, client_hello->extensions, client_hello->extensions_len);

      while (CBS_len(&extensions)) {
        uint16_t type, length;
        CBS_get_u16(&extensions, &type);
        CBS_get_u16(&extensions, &length);
        CBS_skip(&extensions, length);
        received_ext_types->insert(type);
      }

      return ssl_select_cert_success;
    });

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t addrlen = sizeof(addr);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_EQ(0, bind(sock, (struct sockaddr*)&addr, sizeof(addr)));
    ASSERT_EQ(0, listen(sock, 1));
    ASSERT_EQ(0, getsockname(sock, (struct sockaddr*)&addr, &addrlen));
    server_port.set_value(ntohs(addr.sin_port));

    int client = accept(sock, nullptr, nullptr);
    SSL *ssl = SSL_new(ctx.get());
    SSL_set_fd(ssl, client);
    SSL_accept(ssl);
    SSL_write(ssl, "test", 4);
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(client);
    close(sock);
  });

  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_client_method()));
  bssl::UniquePtr<SSL> ssl (SSL_new(ctx.get()));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  addr.sin_port = htons(server_port.get_future().get());

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_EQ(0, connect(sock, (const struct sockaddr *)&addr, sizeof(addr)));
  SSL_set_fd(ssl.get(), sock);
  SSL_connect(ssl.get());

  // OpenSSL and BoringSSL clients send a slightly different set of client hello
  // extensions by default, but this is the common set that we should expect.
  std::set<uint16_t> expected_ext_types {
    TLSEXT_TYPE_extended_master_secret,
    TLSEXT_TYPE_supported_groups,
    TLSEXT_TYPE_ec_point_formats,
    TLSEXT_TYPE_session_ticket,
    TLSEXT_TYPE_signature_algorithms,
    TLSEXT_TYPE_key_share,
    TLSEXT_TYPE_psk_key_exchange_modes,
    TLSEXT_TYPE_supported_versions
  };

  ASSERT_TRUE (std::includes(received_ext_types.begin(), received_ext_types.end(),
                             expected_ext_types.begin(), expected_ext_types.end()));

  server.join();
}


static const char *version_str(uint16_t version) {
    switch (version) {
      case SSL3_VERSION :   { return "SSL3_VERSION  "; break; }
      case TLS1_VERSION :   { return "TLS1_VERSION  "; break; }
      case TLS1_1_VERSION : { return "TLS1_1_VERSION"; break; }
      case TLS1_2_VERSION : { return "TLS1_2_VERSION"; break; }
      case TLS1_3_VERSION : { return "TLS1_3_VERSION"; break; }
      default:              { return "<UNKNOWN>     "; break; }
    };
}

TEST(SSLTest,SSL_CIPHER_get_min_version) {
  std::map<std::string,uint16_t> boring_cipher_versions {
    {"ECDHE-ECDSA-AES128-GCM-SHA256", TLS1_2_VERSION},
    {"ECDHE-RSA-AES128-GCM-SHA256", TLS1_2_VERSION},
    {"ECDHE-ECDSA-AES256-GCM-SHA384", TLS1_2_VERSION},
    {"ECDHE-RSA-AES256-GCM-SHA384", TLS1_2_VERSION},
    {"ECDHE-ECDSA-CHACHA20-POLY1305", TLS1_2_VERSION},
    {"ECDHE-RSA-CHACHA20-POLY1305", TLS1_2_VERSION},
    {"ECDHE-PSK-CHACHA20-POLY1305", TLS1_2_VERSION},
    {"ECDHE-ECDSA-AES128-SHA", SSL3_VERSION  },
    {"ECDHE-RSA-AES128-SHA", SSL3_VERSION  },
    {"ECDHE-PSK-AES128-CBC-SHA", SSL3_VERSION  },
    {"ECDHE-ECDSA-AES256-SHA", SSL3_VERSION  },
    {"ECDHE-RSA-AES256-SHA", SSL3_VERSION  },
    {"ECDHE-PSK-AES256-CBC-SHA", SSL3_VERSION  },
    {"AES128-GCM-SHA256", TLS1_2_VERSION},
    {"AES256-GCM-SHA384", TLS1_2_VERSION},
    {"AES128-SHA", SSL3_VERSION  },
    {"PSK-AES128-CBC-SHA", SSL3_VERSION  },
    {"AES256-SHA", SSL3_VERSION  },
    {"PSK-AES256-CBC-SHA", SSL3_VERSION  },
    {"DES-CBC3-SHA", SSL3_VERSION  },
  };

  bssl::UniquePtr<SSL_CTX> ctx {SSL_CTX_new(TLS_server_method())};
  bssl::UniquePtr<SSL> ssl {SSL_new(ctx.get())};

  STACK_OF(SSL_CIPHER) *ciphers = SSL_get_ciphers(ssl.get());
  for (int i = 0; i < sk_SSL_CIPHER_num(ciphers); i++) {
    const SSL_CIPHER *cipher {sk_SSL_CIPHER_value(ciphers, i)};
    const char *name {SSL_CIPHER_get_name(cipher)};
    uint16_t min_version {SSL_CIPHER_get_min_version(cipher)};

    if (boring_cipher_versions.find(name) != boring_cipher_versions.end()) {
      EXPECT_STREQ(version_str(boring_cipher_versions[name]), version_str(min_version)) << " for " << name;
    }
  }
}

TEST(SSLTest, SSL_error_description) {
  EXPECT_STREQ("WANT_ACCEPT", SSL_error_description(SSL_ERROR_WANT_ACCEPT));
  EXPECT_EQ(nullptr, SSL_error_description(123456));
}

TEST(SSLTest, SSL_enable_ocsp_stapling) {
  bssl::UniquePtr<SSL_CTX> ctx {SSL_CTX_new(TLS_server_method())};
  bssl::UniquePtr<SSL> ssl {SSL_new(ctx.get())};
  SSL_enable_ocsp_stapling(ssl.get());
}

TEST(SSLTest, SSL_set_SSL_CTX) {
  bssl::UniquePtr<SSL_CTX> ctx1 {SSL_CTX_new(TLS_server_method())};
  bssl::UniquePtr<SSL_CTX> ctx2 {SSL_CTX_new(TLS_server_method())};
  bssl::UniquePtr<SSL> ssl {SSL_new(ctx1.get())};

  EXPECT_EQ(ctx1.get(), SSL_get_SSL_CTX(ssl.get()));
  EXPECT_EQ(ctx2.get(), SSL_set_SSL_CTX(ssl.get(), ctx2.get()));
  EXPECT_EQ(ctx2.get(), SSL_get_SSL_CTX(ssl.get()));
  EXPECT_EQ(ctx2.get(), SSL_set_SSL_CTX(ssl.get(), ctx2.get()));
  EXPECT_EQ(ctx2.get(), SSL_get_SSL_CTX(ssl.get()));
}

TEST(SSLTest, SSL_set_renegotiate_mode) {
  bssl::UniquePtr<SSL_CTX> ctx {SSL_CTX_new(TLS_server_method())};
  bssl::UniquePtr<SSL> ssl {SSL_new(ctx.get())};

  SSL_set_renegotiate_mode(ssl.get(), ssl_renegotiate_never);
  SSL_set_renegotiate_mode(ssl.get(), ssl_renegotiate_freely);
}

TEST(SSLTest, SSL_set_ocsp_response) {
  bssl::UniquePtr<SSL_CTX> ctx {SSL_CTX_new(TLS_server_method())};
  bssl::UniquePtr<SSL> ssl {SSL_new(ctx.get())};

  const uint8_t response1[] { 1, 2, 3, 4 };
  const uint8_t response2[] { 1, 2, 3, 4 };

  EXPECT_TRUE(SSL_set_ocsp_response(ssl.get(), nullptr, 0));
  EXPECT_TRUE(SSL_set_ocsp_response(ssl.get(), response1, sizeof(response1)));
  EXPECT_TRUE(SSL_set_ocsp_response(ssl.get(), response2, sizeof(response2)));
  EXPECT_TRUE(SSL_set_ocsp_response(ssl.get(), nullptr, 0));
}

TEST(SSLTest, SSL_SESSION_should_be_single_use) {
  bssl::UniquePtr<SSL_CTX> ctx {SSL_CTX_new(TLS_server_method())};
  bssl::UniquePtr<SSL_SESSION> session {SSL_SESSION_new(ctx.get())};

  ASSERT_TRUE(SSL_SESSION_set_protocol_version(session.get(), TLS1_3_VERSION));
  ASSERT_EQ(1, SSL_SESSION_should_be_single_use(session.get()));
  ASSERT_TRUE(SSL_SESSION_set_protocol_version(session.get(), TLS1_2_VERSION));
  ASSERT_EQ(0, SSL_SESSION_should_be_single_use(session.get()));
}

TEST(SSLTest, test_SSL_get_peer_full_cert_chain) {
  TempFile root_ca_cert_pem        { root_ca_cert_pem_str };
  TempFile client_2_key_pem        { client_2_key_pem_str };
  TempFile client_2_cert_chain_pem { client_2_cert_chain_pem_str };
  TempFile server_2_key_pem        { server_2_key_pem_str };
  TempFile server_2_cert_chain_pem { server_2_cert_chain_pem_str };

  const char MESSAGE[] { "HELLO" };
  std::promise<in_port_t> server_port;

  signal(SIGPIPE, SIG_IGN);

  // Start a TLS server
  std::thread server([&]() {
    bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_server_method()));

    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, nullptr);
    ASSERT_EQ(1, SSL_CTX_load_verify_locations(ctx.get(), root_ca_cert_pem.path(), nullptr)) << (ERR_print_errors_fp(stderr), "");

    STACK_OF(X509_NAME) *cert_names { sk_X509_NAME_new_null() };
    ASSERT_EQ(1, SSL_add_file_cert_subjects_to_stack(cert_names, root_ca_cert_pem.path()));
    SSL_CTX_set_client_CA_list(ctx.get(), cert_names);

    ASSERT_EQ(1, SSL_CTX_use_certificate_chain_file(ctx.get(), server_2_cert_chain_pem.path()));
    ASSERT_EQ(1, SSL_CTX_use_PrivateKey_file(ctx.get(), server_2_key_pem.path(), SSL_FILETYPE_PEM));

    bssl::UniquePtr<SSL> ssl { SSL_new(ctx.get()) };

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t addrlen = sizeof(addr);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_LT(0, sock);
    ASSERT_EQ(0, bind(sock, (struct sockaddr*)&addr, sizeof(addr)));
    ASSERT_EQ(0, listen(sock, 1));
    ASSERT_EQ(0, getsockname(sock, (struct sockaddr*)&addr, &addrlen));
    server_port.set_value(ntohs(addr.sin_port)); // Tell the client our port number
    int client = accept(sock, nullptr, nullptr);
    ASSERT_LT(0, client);

    ASSERT_EQ(1, SSL_set_fd(ssl.get(), client));
    ASSERT_EQ(1, SSL_accept(ssl.get())) << (ERR_print_errors_fp(stderr), "");
    ASSERT_EQ(1, SSL_is_server(ssl.get()));

    STACK_OF(X509) *client_certs { SSL_get_peer_full_cert_chain(ssl.get()) };
    ASSERT_TRUE(client_certs);
    ASSERT_EQ(4, sk_X509_num(client_certs));

    char buf[sizeof(MESSAGE)];
    ASSERT_EQ(sizeof(MESSAGE), SSL_read(ssl.get(), buf, sizeof(buf)));
    ASSERT_EQ(sizeof(MESSAGE), SSL_write(ssl.get(), MESSAGE, sizeof(MESSAGE)));

    SSL_shutdown(ssl.get());
    close(client);
    close(sock);
  });

  {
    bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_client_method()));

    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, nullptr);
    ASSERT_EQ(1, SSL_CTX_load_verify_locations(ctx.get(), root_ca_cert_pem.path(), nullptr));

    ASSERT_EQ(1, SSL_CTX_use_certificate_chain_file(ctx.get(), client_2_cert_chain_pem.path())) << (ERR_print_errors_fp(stderr), "");
    ASSERT_EQ(1, SSL_CTX_use_PrivateKey_file(ctx.get(), client_2_key_pem.path(), SSL_FILETYPE_PEM));

    bssl::UniquePtr<SSL> ssl (SSL_new(ctx.get()));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(server_port.get_future().get());

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_EQ(0, connect(sock, (const struct sockaddr *)&addr, sizeof(addr)));
    ASSERT_EQ(1, SSL_set_fd(ssl.get(), sock));
    ASSERT_TRUE(SSL_connect(ssl.get()) > 0) << (ERR_print_errors_fp(stderr), "");

    STACK_OF(X509) *server_certs = SSL_get_peer_full_cert_chain(ssl.get());
    ASSERT_TRUE(server_certs);
    ASSERT_EQ(4, sk_X509_num(server_certs));

    char buf[sizeof(MESSAGE)];
    ASSERT_EQ(sizeof(MESSAGE), SSL_write(ssl.get(), MESSAGE, sizeof(MESSAGE)));
    ASSERT_EQ(sizeof(MESSAGE), SSL_read(ssl.get(), buf, sizeof(buf)));
  }

  server.join();
}

TEST(SSLTest, test_SSL_CTX_set_strict_cipher_list) {
  bssl::UniquePtr<SSL_CTX> ctx {SSL_CTX_new(TLS_server_method())};
  STACK_OF(SSL_CIPHER) *ciphers {SSL_CTX_get_ciphers(ctx.get())};

  std::string cipherstr;

  for(const SSL_CIPHER *cipher : ciphers) {
    cipherstr += (cipherstr.size() ? ":" : "");
    cipherstr += SSL_CIPHER_get_name(cipher);
  }

  ASSERT_EQ(1, SSL_CTX_set_strict_cipher_list(ctx.get(), cipherstr.c_str()));
  ASSERT_EQ(0, SSL_CTX_set_strict_cipher_list(ctx.get(), "rubbish:garbage"));
}

TEST(SSLTest, test_SSL_CTX_set_verify_algorithm_prefs) {
  bssl::UniquePtr<SSL_CTX> ctx {SSL_CTX_new(TLS_server_method())};

  uint16_t prefs[] {
    SSL_SIGN_RSA_PSS_RSAE_SHA256,
    SSL_SIGN_ECDSA_SECP256R1_SHA256
  };

  ASSERT_EQ(1, SSL_CTX_set_verify_algorithm_prefs(ctx.get(), prefs, (sizeof(prefs) / sizeof(prefs[0]))));
}

TEST(SSLTest, test_SSL_early_callback_ctx_extension_get) {
  TempFile server_2_key_pem        { server_2_key_pem_str };
  TempFile server_2_cert_chain_pem { server_2_cert_chain_pem_str };

  static const char MESSAGE[] { "HELLO" };
  static const char SERVERNAME[] { "www.example.com" };
  std::promise<in_port_t> server_port;

  signal(SIGPIPE, SIG_IGN);

  // Start a TLS server
  std::thread server([&]() {
    bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_server_method()));

    ASSERT_EQ(1, SSL_CTX_use_certificate_chain_file(ctx.get(), server_2_cert_chain_pem.path()));
    ASSERT_EQ(1, SSL_CTX_use_PrivateKey_file(ctx.get(), server_2_key_pem.path(), SSL_FILETYPE_PEM));

    // Install a callback that will use SSL_early_callback_ctx_extension_get()
    // to check the server name extension that we configured the client to send.
    // The extension bytes should contain: u16->[u8, u16->[www.example.com]]
    SSL_CTX_set_select_certificate_cb(ctx.get(), [](const SSL_CLIENT_HELLO *client_hello) -> ssl_select_cert_result_t {
      const uint8_t *tlsext_data;
      size_t         tlsext_len;

      if(SSL_early_callback_ctx_extension_get(client_hello, TLSEXT_TYPE_server_name, &tlsext_data, &tlsext_len)) {
        CBS     server_name_extension;
        CBS     server_name_extension_bytes;
        uint8_t server_name_extension_nametype;
        CBS     server_name_extension_name;

        CBS_init(&server_name_extension, tlsext_data, tlsext_len);

        if (CBS_len(&server_name_extension) == (2 + 1 + 2 + strlen(SERVERNAME)) &&
            CBS_get_u16_length_prefixed(&server_name_extension, &server_name_extension_bytes) &&
            CBS_len(&server_name_extension_bytes) == (1 + 2 + strlen(SERVERNAME)) &&
            CBS_get_u8(&server_name_extension_bytes, &server_name_extension_nametype) &&
            server_name_extension_nametype == TLSEXT_NAMETYPE_host_name &&
            CBS_len(&server_name_extension_bytes) == (2 + strlen(SERVERNAME)) &&
            CBS_get_u16_length_prefixed(&server_name_extension_bytes, &server_name_extension_name) &&
            CBS_len(&server_name_extension_name) == strlen(SERVERNAME) &&
            memcmp(CBS_data(&server_name_extension_name), SERVERNAME, strlen(SERVERNAME)) == 0) {
              return ssl_select_cert_success;
        }
      }

      return ssl_select_cert_error; // Causes SSL_connect() to fail in the client
    });

    bssl::UniquePtr<SSL> ssl { SSL_new(ctx.get()) };

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t addrlen = sizeof(addr);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_LT(0, sock);
    ASSERT_EQ(0, bind(sock, (struct sockaddr*)&addr, sizeof(addr)));
    ASSERT_EQ(0, listen(sock, 1));
    ASSERT_EQ(0, getsockname(sock, (struct sockaddr*)&addr, &addrlen));
    server_port.set_value(ntohs(addr.sin_port)); // Tell the client our port number
    int client = accept(sock, nullptr, nullptr);
    ASSERT_LT(0, client);

    ASSERT_EQ(1, SSL_set_fd(ssl.get(), client));
    ASSERT_EQ(1, SSL_accept(ssl.get())) << ERR_error_string(ERR_get_error(), nullptr);
    ASSERT_EQ(1, SSL_is_server(ssl.get()));

    char buf[sizeof(MESSAGE)];
    ASSERT_EQ(sizeof(MESSAGE), SSL_read(ssl.get(), buf, sizeof(buf)));
    ASSERT_EQ(sizeof(MESSAGE), SSL_write(ssl.get(), MESSAGE, sizeof(MESSAGE)));

    SSL_shutdown(ssl.get());
    close(client);
    close(sock);
  });

  {
    bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_client_method()));
    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_NONE, nullptr);
    bssl::UniquePtr<SSL> ssl (SSL_new(ctx.get()));

    // Send a TLSEXT_TYPE_server_name extension. The server will inspect this
    // extension using SSL_early_callback_ctx_extension_get(), and fail the
    // handshake if there's an error.
    ASSERT_EQ(1, SSL_set_tlsext_host_name(ssl.get(), SERVERNAME));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(server_port.get_future().get());

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_EQ(0, connect(sock, (const struct sockaddr *)&addr, sizeof(addr)));
    ASSERT_EQ(1, SSL_set_fd(ssl.get(), sock));
    ASSERT_TRUE(SSL_connect(ssl.get()) > 0) << (ERR_print_errors_fp(stderr), "");

    char buf[sizeof(MESSAGE)];
    ASSERT_EQ(sizeof(MESSAGE), SSL_write(ssl.get(), MESSAGE, sizeof(MESSAGE)));
    ASSERT_EQ(sizeof(MESSAGE), SSL_read(ssl.get(), buf, sizeof(buf)));
  }

  server.join();
}

TEST(SSLTest, test_SSL_get_cipher_by_value) {
  struct {
    uint16_t value; // IANA number
    const char *name; // IETF name
    uint32_t id; // 
  }
  ciphers[] {
    { 0x1302, "TLS_AES_256_GCM_SHA384", TLS1_3_CK_AES_256_GCM_SHA384 },
    { 0x1303, "TLS_CHACHA20_POLY1305_SHA256", TLS1_3_CK_CHACHA20_POLY1305_SHA256 },
    { 0x1301, "TLS_AES_128_GCM_SHA256", TLS1_3_CK_AES_128_GCM_SHA256 },
    { 0xc02c, "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384", TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384 },
    { 0xcca9, "TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256", TLS1_CK_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256 },
    { 0xc02b, "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256", TLS1_CK_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 },
    { 0xc00a, "TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA", TLS1_CK_ECDHE_ECDSA_WITH_AES_256_CBC_SHA },
    { 0xc009, "TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA", TLS1_CK_ECDHE_ECDSA_WITH_AES_128_CBC_SHA },
    { 0x009d, "TLS_RSA_WITH_AES_256_GCM_SHA384", TLS1_CK_RSA_WITH_AES_256_GCM_SHA384 },
    { 0x009c, "TLS_RSA_WITH_AES_128_GCM_SHA256", TLS1_CK_RSA_WITH_AES_128_GCM_SHA256 },
    { 0x0035, "TLS_RSA_WITH_AES_256_CBC_SHA", TLS1_CK_RSA_WITH_AES_256_SHA },
    { 0x002f, "TLS_RSA_WITH_AES_128_CBC_SHA", TLS1_CK_RSA_WITH_AES_128_SHA },
    { 0x008d, "TLS_PSK_WITH_AES_256_CBC_SHA", TLS1_CK_PSK_WITH_AES_256_CBC_SHA },
    { 0x008c, "TLS_PSK_WITH_AES_128_CBC_SHA", TLS1_CK_PSK_WITH_AES_128_CBC_SHA }
  };

  for(auto &c : ciphers) {
    const SSL_CIPHER *cipher = SSL_get_cipher_by_value(c.value);
    ASSERT_TRUE(cipher) << "Failed to get cipher " << c.value << " (" << c.name << ")";
    EXPECT_STREQ(c.name, SSL_CIPHER_standard_name(cipher));
    EXPECT_EQ(c.id, SSL_CIPHER_get_id(cipher));
  }
}


struct Curves {
  const char *server_curves;
  const char *client_curves;
  uint16_t expected_curve;
};

class SSLTestWithCurves : public testing::TestWithParam<Curves> {
};

TEST_P(SSLTestWithCurves, test_SSL_get_curve_id) {
  TempFile server_2_key_pem        { server_2_key_pem_str };
  TempFile server_2_cert_chain_pem { server_2_cert_chain_pem_str };

  static const char MESSAGE[] { "HELLO" };
  std::promise<in_port_t> server_port;

  signal(SIGPIPE, SIG_IGN);

  const auto &curves {GetParam()};

  // Start a TLS server
  std::thread server([&]() {
    bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_server_method()));

    ASSERT_EQ(1, SSL_CTX_use_certificate_chain_file(ctx.get(), server_2_cert_chain_pem.path()));
    ASSERT_EQ(1, SSL_CTX_use_PrivateKey_file(ctx.get(), server_2_key_pem.path(), SSL_FILETYPE_PEM));
    ASSERT_EQ(1, SSL_CTX_set1_curves_list(ctx.get(), curves.server_curves));

    bssl::UniquePtr<SSL> ssl { SSL_new(ctx.get()) };

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t addrlen = sizeof(addr);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_LT(0, sock);
    ASSERT_EQ(0, bind(sock, (struct sockaddr*)&addr, sizeof(addr)));
    ASSERT_EQ(0, listen(sock, 1));
    ASSERT_EQ(0, getsockname(sock, (struct sockaddr*)&addr, &addrlen));
    server_port.set_value(ntohs(addr.sin_port)); // Tell the client our port number
    int client = accept(sock, nullptr, nullptr);
    ASSERT_LT(0, client);

    ASSERT_EQ(1, SSL_set_fd(ssl.get(), client));
    ASSERT_EQ(1, SSL_accept(ssl.get())) << ERR_error_string(ERR_get_error(), nullptr);
    ASSERT_EQ(1, SSL_is_server(ssl.get()));

    char buf[sizeof(MESSAGE)];
    ASSERT_EQ(sizeof(MESSAGE), SSL_read(ssl.get(), buf, sizeof(buf)));
    ASSERT_EQ(sizeof(MESSAGE), SSL_write(ssl.get(), MESSAGE, sizeof(MESSAGE)));

    SSL_shutdown(ssl.get());
    close(client);
    close(sock);
  });

  {
    bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_client_method()));
    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_NONE, nullptr);
    SSL_CTX_set_max_proto_version(ctx.get(), TLS1_2_VERSION);
    ASSERT_EQ(1, SSL_CTX_set1_curves_list(ctx.get(), curves.client_curves));
    bssl::UniquePtr<SSL> ssl (SSL_new(ctx.get()));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(server_port.get_future().get());

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_EQ(0, connect(sock, (const struct sockaddr *)&addr, sizeof(addr)));
    ASSERT_EQ(1, SSL_set_fd(ssl.get(), sock));
    ASSERT_TRUE(SSL_connect(ssl.get()) > 0) << (ERR_print_errors_fp(stderr), "");

    ASSERT_EQ(curves.expected_curve, SSL_get_curve_id(ssl.get()));

    char buf[sizeof(MESSAGE)];
    ASSERT_EQ(sizeof(MESSAGE), SSL_write(ssl.get(), MESSAGE, sizeof(MESSAGE)));
    ASSERT_EQ(sizeof(MESSAGE), SSL_read(ssl.get(), buf, sizeof(buf)));
  }

  server.join();
}

INSTANTIATE_TEST_SUITE_P(
        SSLTestWithCurves,
        SSLTestWithCurves,
        ::testing::Values(
          Curves {
            SN_secp224r1 ":" SN_secp384r1 ":" SN_secp521r1 ":" SN_X25519 ":" SN_X9_62_prime256v1,
            SN_secp224r1,
            SSL_CURVE_SECP224R1
          },
          Curves {
            SN_secp224r1 ":" SN_secp384r1 ":" SN_secp521r1 ":" SN_X25519 ":" SN_X9_62_prime256v1,
            SN_secp521r1,
            SSL_CURVE_SECP521R1
          },
          Curves {
            SN_secp224r1 ":" SN_secp384r1 ":" SN_secp521r1 ":" SN_X25519 ":" SN_X9_62_prime256v1,
            SN_X9_62_prime256v1,
            SSL_CURVE_SECP256R1
          }
        ));

TEST(SSLTest, test_SSL_get_curve_name) {
  EXPECT_STREQ("P-224", SSL_get_curve_name(SSL_CURVE_SECP224R1));
  EXPECT_STREQ("P-256", SSL_get_curve_name(SSL_CURVE_SECP256R1));
  EXPECT_STREQ("P-384", SSL_get_curve_name(SSL_CURVE_SECP384R1));
  EXPECT_STREQ("P-521", SSL_get_curve_name(SSL_CURVE_SECP521R1));
  EXPECT_STREQ("X25519", SSL_get_curve_name(SSL_CURVE_X25519));
  EXPECT_STREQ("X25519MLKEM768", SSL_get_curve_name(SSL_GROUP_X25519_MLKEM768));
  EXPECT_STREQ("X25519Kyber768Draft00", SSL_get_curve_name(SSL_GROUP_X25519_KYBER768_DRAFT00));
}


struct Sigalgs {
  const char *server_sigalgs;
  const char *client_sigalgs;
  uint16_t expected_sigalg;
};

class SSLTestWithSigalgs : public testing::TestWithParam<Sigalgs> {
};

TEST_P(SSLTestWithSigalgs, test_SSL_get_peer_signature_algorithm) {
  TempFile server_2_key_pem        { server_2_key_pem_str };
  TempFile server_2_cert_chain_pem { server_2_cert_chain_pem_str };

  static const char MESSAGE[] { "HELLO" };
  std::promise<in_port_t> server_port;

  signal(SIGPIPE, SIG_IGN);

  const auto &sigalgs {GetParam()};

  // Start a TLS server
  std::thread server([&]() {
    bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_server_method()));
    ASSERT_EQ(1, SSL_CTX_use_certificate_chain_file(ctx.get(), server_2_cert_chain_pem.path()));
    ASSERT_EQ(1, SSL_CTX_use_PrivateKey_file(ctx.get(), server_2_key_pem.path(), SSL_FILETYPE_PEM));
    if(sigalgs.server_sigalgs) {
      ASSERT_EQ(1, SSL_CTX_set1_sigalgs_list(ctx.get(), sigalgs.server_sigalgs)) << ERR_error_string(ERR_get_error(), nullptr);
    }
    bssl::UniquePtr<SSL> ssl { SSL_new(ctx.get()) };

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t addrlen = sizeof(addr);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_LT(0, sock);
    ASSERT_EQ(0, bind(sock, (struct sockaddr*)&addr, sizeof(addr)));
    ASSERT_EQ(0, listen(sock, 1));
    ASSERT_EQ(0, getsockname(sock, (struct sockaddr*)&addr, &addrlen));
    server_port.set_value(ntohs(addr.sin_port)); // Tell the client our port number
    int client = accept(sock, nullptr, nullptr);
    ASSERT_LT(0, client);

    ASSERT_EQ(1, SSL_set_fd(ssl.get(), client));
    ASSERT_EQ(1, SSL_accept(ssl.get())) << ERR_error_string(ERR_get_error(), nullptr);
    ASSERT_EQ(1, SSL_is_server(ssl.get()));

    char buf[sizeof(MESSAGE)];
    ASSERT_EQ(sizeof(MESSAGE), SSL_read(ssl.get(), buf, sizeof(buf)));
    ASSERT_EQ(sizeof(MESSAGE), SSL_write(ssl.get(), MESSAGE, sizeof(MESSAGE)));

    SSL_shutdown(ssl.get());
    close(client);
    close(sock);
  });

  {
    bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_client_method()));
    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_NONE, nullptr);
    if(sigalgs.client_sigalgs) {
      ASSERT_EQ(1, SSL_CTX_set1_sigalgs_list(ctx.get(), sigalgs.client_sigalgs)) << ERR_error_string(ERR_get_error(), nullptr);
    }
    bssl::UniquePtr<SSL> ssl (SSL_new(ctx.get()));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(server_port.get_future().get());

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_EQ(0, connect(sock, (const struct sockaddr *)&addr, sizeof(addr)));
    ASSERT_EQ(1, SSL_set_fd(ssl.get(), sock));
    ASSERT_TRUE(SSL_connect(ssl.get()) > 0) << (ERR_print_errors_fp(stderr), "");

    ASSERT_EQ(sigalgs.expected_sigalg, SSL_get_peer_signature_algorithm(ssl.get()));

    char buf[sizeof(MESSAGE)];
    ASSERT_EQ(sizeof(MESSAGE), SSL_write(ssl.get(), MESSAGE, sizeof(MESSAGE)));
    ASSERT_EQ(sizeof(MESSAGE), SSL_read(ssl.get(), buf, sizeof(buf)));
  }

  server.join();
}

INSTANTIATE_TEST_SUITE_P(
        SSLTestWithSigalgs,
        SSLTestWithSigalgs,
        ::testing::Values(
          Sigalgs {
            "rsa_pss_rsae_sha256",
            "rsa_pss_rsae_sha256",
            SSL_SIGN_RSA_PSS_RSAE_SHA256
          },
          Sigalgs {
            "rsa_pss_rsae_sha384",
            "rsa_pss_rsae_sha384",
            SSL_SIGN_RSA_PSS_RSAE_SHA384
          },
          Sigalgs {
            "rsa_pss_rsae_sha512",
            "rsa_pss_rsae_sha512",
            SSL_SIGN_RSA_PSS_RSAE_SHA512
          }
        )
);

TEST(SSLTest, test_SSL_get_signature_algorithm_name) {
    EXPECT_STREQ(SSL_get_signature_algorithm_name(SSL_SIGN_RSA_PKCS1_SHA1, 0), "rsa_pkcs1_sha1");
    EXPECT_STREQ(SSL_get_signature_algorithm_name(SSL_SIGN_RSA_PKCS1_SHA256, 0), "rsa_pkcs1_sha256");
    EXPECT_STREQ(SSL_get_signature_algorithm_name(SSL_SIGN_RSA_PKCS1_SHA384, 0), "rsa_pkcs1_sha384");
    EXPECT_STREQ(SSL_get_signature_algorithm_name(SSL_SIGN_RSA_PKCS1_SHA512, 0), "rsa_pkcs1_sha512");
    EXPECT_STREQ(SSL_get_signature_algorithm_name(SSL_SIGN_ECDSA_SHA1, 0), "ecdsa_sha1");
    EXPECT_STREQ(SSL_get_signature_algorithm_name(SSL_SIGN_ECDSA_SECP256R1_SHA256, 0), "ecdsa_sha256");
    EXPECT_STREQ(SSL_get_signature_algorithm_name(SSL_SIGN_ECDSA_SECP256R1_SHA256, 1), "ecdsa_secp256r1_sha256");
    EXPECT_STREQ(SSL_get_signature_algorithm_name(SSL_SIGN_ECDSA_SECP384R1_SHA384, 0), "ecdsa_sha384");
    EXPECT_STREQ(SSL_get_signature_algorithm_name(SSL_SIGN_ECDSA_SECP384R1_SHA384, 1), "ecdsa_secp384r1_sha384");
    EXPECT_STREQ(SSL_get_signature_algorithm_name(SSL_SIGN_ECDSA_SECP521R1_SHA512, 0), "ecdsa_sha512");
    EXPECT_STREQ(SSL_get_signature_algorithm_name(SSL_SIGN_ECDSA_SECP521R1_SHA512, 1), "ecdsa_secp521r1_sha512");
    EXPECT_STREQ(SSL_get_signature_algorithm_name(SSL_SIGN_RSA_PSS_RSAE_SHA256, 0), "rsa_pss_rsae_sha256");
    EXPECT_STREQ(SSL_get_signature_algorithm_name(SSL_SIGN_RSA_PSS_RSAE_SHA384, 0), "rsa_pss_rsae_sha384");
    EXPECT_STREQ(SSL_get_signature_algorithm_name(SSL_SIGN_RSA_PSS_RSAE_SHA512, 0), "rsa_pss_rsae_sha512");
    EXPECT_STREQ(SSL_get_signature_algorithm_name(SSL_SIGN_ED25519, 0), "ed25519");
}


TEST(SSLTest, test_SSL_get0_ocsp_response) {
  TempFile server_2_key_pem        { server_2_key_pem_str };
  TempFile server_2_cert_chain_pem { server_2_cert_chain_pem_str };

  static const char MESSAGE[] { "HELLO" };
  static const uint8_t OCSP_RESPONSE[] { 'H', 'E', 'L', 'P' };
  std::promise<in_port_t> server_port;

  signal(SIGPIPE, SIG_IGN);

  // Start a TLS server
  std::thread server([&]() {
    bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_server_method()));
    ASSERT_EQ(1, SSL_CTX_use_certificate_chain_file(ctx.get(), server_2_cert_chain_pem.path()));
    ASSERT_EQ(1, SSL_CTX_use_PrivateKey_file(ctx.get(), server_2_key_pem.path(), SSL_FILETYPE_PEM));

    SSL_CTX_set_tlsext_status_cb(ctx.get(), [](SSL* ssl, void* arg) -> int {
      if(!SSL_set_ocsp_response(ssl, OCSP_RESPONSE, sizeof(OCSP_RESPONSE))) {
        return SSL_TLSEXT_ERR_ALERT_FATAL;
      }
      return SSL_TLSEXT_ERR_OK;
    });

    bssl::UniquePtr<SSL> ssl { SSL_new(ctx.get()) };

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t addrlen = sizeof(addr);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_LT(0, sock);
    ASSERT_EQ(0, bind(sock, (struct sockaddr*)&addr, sizeof(addr)));
    ASSERT_EQ(0, listen(sock, 1));
    ASSERT_EQ(0, getsockname(sock, (struct sockaddr*)&addr, &addrlen));
    server_port.set_value(ntohs(addr.sin_port)); // Tell the client our port number
    int client = accept(sock, nullptr, nullptr);
    ASSERT_LT(0, client);

    ASSERT_EQ(1, SSL_set_fd(ssl.get(), client));
    ASSERT_EQ(1, SSL_accept(ssl.get())) << ERR_error_string(ERR_get_error(), nullptr);
    ASSERT_EQ(1, SSL_is_server(ssl.get()));

    char buf[sizeof(MESSAGE)];
    ASSERT_EQ(sizeof(MESSAGE), SSL_read(ssl.get(), buf, sizeof(buf)));
    ASSERT_EQ(sizeof(MESSAGE), SSL_write(ssl.get(), MESSAGE, sizeof(MESSAGE)));

    SSL_shutdown(ssl.get());
    close(client);
    close(sock);
  });

  {
    bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_client_method()));
    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_NONE, nullptr);
    bssl::UniquePtr<SSL> ssl (SSL_new(ctx.get()));
    SSL_enable_ocsp_stapling(ssl.get());

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(server_port.get_future().get());

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_EQ(0, connect(sock, (const struct sockaddr *)&addr, sizeof(addr)));
    ASSERT_EQ(1, SSL_set_fd(ssl.get(), sock));
    ASSERT_TRUE(SSL_connect(ssl.get()) > 0) << (ERR_print_errors_fp(stderr), "");

    const uint8_t *ocsp_resp_data;
    size_t ocsp_resp_len;

    SSL_get0_ocsp_response(ssl.get(), &ocsp_resp_data, &ocsp_resp_len);

    ASSERT_EQ(sizeof(OCSP_RESPONSE), ocsp_resp_len);
    ASSERT_EQ(0, memcmp(OCSP_RESPONSE, ocsp_resp_data, ocsp_resp_len));

    char buf[sizeof(MESSAGE)];
    ASSERT_EQ(sizeof(MESSAGE), SSL_write(ssl.get(), MESSAGE, sizeof(MESSAGE)));
    ASSERT_EQ(sizeof(MESSAGE), SSL_read(ssl.get(), buf, sizeof(buf)));
  }

  server.join();
}

TEST(SSLTest, test_SSL_set_chain_and_key) {
  TempFile root_ca_cert_pem { root_ca_cert_pem_str };

  static const char MESSAGE[] { "HELLO" };
  std::promise<in_port_t> server_port;

  signal(SIGPIPE, SIG_IGN);

  // Start a TLS server
  std::thread server([&]() {
    bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_server_method()));
    bssl::UniquePtr<SSL> ssl { SSL_new(ctx.get()) };

    bssl::UniquePtr<EVP_PKEY> key;
    {
      bssl::UniquePtr<BIO> bio {BIO_new_mem_buf(server_2_key_pem_str, strlen(server_2_key_pem_str))};
      ASSERT_TRUE(bio);
      bssl::UniquePtr<RSA> rsa {PEM_read_bio_RSAPrivateKey(bio.get(), nullptr, nullptr, nullptr)};
      ASSERT_TRUE(rsa);
      bssl::UniquePtr<EVP_PKEY> tmp(EVP_PKEY_new());
      ASSERT_EQ(1, EVP_PKEY_assign_RSA(tmp.get(), rsa.get()));
      rsa.release();
      key = std::move(tmp);
    }

    std::vector<CRYPTO_BUFFER*> chain;
    for(const char *pem_str : { server_2_cert_pem_str, intermediate_ca_2_cert_pem_str, intermediate_ca_1_cert_pem_str }) {
      bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(pem_str, strlen(pem_str)));
      long der_len = 0;
      uint8_t* der_data = nullptr;
      ASSERT_EQ(1, PEM_bytes_read_bio(&der_data, &der_len, nullptr, PEM_STRING_X509, bio.get(), nullptr, nullptr));
      bssl::UniquePtr<uint8_t> tmp(der_data); // Prevents memory leak.
      chain.push_back(CRYPTO_BUFFER_new(der_data, der_len, nullptr));
    }

    ASSERT_EQ(1, SSL_set_chain_and_key(ssl.get(), chain.data(), chain.size(), key.get(), nullptr));
    
    while(chain.size()) {
      CRYPTO_BUFFER_free(chain.back());
      chain.pop_back();
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t addrlen = sizeof(addr);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_LT(0, sock);
    ASSERT_EQ(0, bind(sock, (struct sockaddr*)&addr, sizeof(addr)));
    ASSERT_EQ(0, listen(sock, 1));
    ASSERT_EQ(0, getsockname(sock, (struct sockaddr*)&addr, &addrlen));
    server_port.set_value(ntohs(addr.sin_port)); // Tell the client our port number
    int client = accept(sock, nullptr, nullptr);
    ASSERT_LT(0, client);

    ASSERT_EQ(1, SSL_set_fd(ssl.get(), client));
    ASSERT_EQ(1, SSL_accept(ssl.get())) << ERR_error_string(ERR_get_error(), nullptr);
    ASSERT_EQ(1, SSL_is_server(ssl.get()));

    char buf[sizeof(MESSAGE)];
    ASSERT_EQ(sizeof(MESSAGE), SSL_read(ssl.get(), buf, sizeof(buf)));
    ASSERT_EQ(sizeof(MESSAGE), SSL_write(ssl.get(), MESSAGE, sizeof(MESSAGE)));

    SSL_shutdown(ssl.get());
    close(client);
    close(sock);
  });

  {
    bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_client_method()));
    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, nullptr);
    ASSERT_EQ(1, SSL_CTX_load_verify_locations(ctx.get(), root_ca_cert_pem.path(), nullptr));
    bssl::UniquePtr<SSL> ssl (SSL_new(ctx.get()));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(server_port.get_future().get());

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_EQ(0, connect(sock, (const struct sockaddr *)&addr, sizeof(addr)));
    ASSERT_EQ(1, SSL_set_fd(ssl.get(), sock));
    ASSERT_TRUE(SSL_connect(ssl.get()) > 0)  << ERR_error_string(ERR_get_error(), nullptr);

    char buf[sizeof(MESSAGE)];
    ASSERT_EQ(sizeof(MESSAGE), SSL_write(ssl.get(), MESSAGE, sizeof(MESSAGE)));
    ASSERT_EQ(sizeof(MESSAGE), SSL_read(ssl.get(), buf, sizeof(buf)));
  }

  server.join();
}

TEST(SSLTest, test_SSL_SESSION_from_bytes) {
  TempFile root_ca_cert_pem { root_ca_cert_pem_str };
  TempFile server_2_cert_chain_pem { server_2_cert_chain_pem_str };
  TempFile server_2_key_pem { server_2_key_pem_str };

  static const char MESSAGE[] { "HELLO" };
  std::promise<in_port_t> server_port;

  signal(SIGPIPE, SIG_IGN);

  // Start a TLS server
  std::thread server([&]() {
    bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_server_method()));
    ASSERT_EQ(1, SSL_CTX_use_certificate_chain_file(ctx.get(), server_2_cert_chain_pem.path()));
    ASSERT_EQ(1, SSL_CTX_use_PrivateKey_file(ctx.get(), server_2_key_pem.path(), SSL_FILETYPE_PEM));
    bssl::UniquePtr<SSL> ssl { SSL_new(ctx.get()) };

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t addrlen = sizeof(addr);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_LT(0, sock);
    ASSERT_EQ(0, bind(sock, (struct sockaddr*)&addr, sizeof(addr)));
    ASSERT_EQ(0, listen(sock, 1));
    ASSERT_EQ(0, getsockname(sock, (struct sockaddr*)&addr, &addrlen));
    server_port.set_value(ntohs(addr.sin_port)); // Tell the client our port number
    int client = accept(sock, nullptr, nullptr);
    ASSERT_LT(0, client);

    ASSERT_EQ(1, SSL_set_fd(ssl.get(), client));
    ASSERT_EQ(1, SSL_accept(ssl.get())) << ERR_error_string(ERR_get_error(), nullptr);
    ASSERT_EQ(1, SSL_is_server(ssl.get()));

    char buf[sizeof(MESSAGE)];
    ASSERT_EQ(sizeof(MESSAGE), SSL_read(ssl.get(), buf, sizeof(buf)));
    ASSERT_EQ(sizeof(MESSAGE), SSL_write(ssl.get(), MESSAGE, sizeof(MESSAGE)));

    SSL_shutdown(ssl.get());
    close(client);
    close(sock);
  });

  {
    bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_client_method()));
    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, nullptr);
    ASSERT_EQ(1, SSL_CTX_load_verify_locations(ctx.get(), root_ca_cert_pem.path(), nullptr));
    bssl::UniquePtr<SSL> ssl (SSL_new(ctx.get()));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(server_port.get_future().get());

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_EQ(0, connect(sock, (const struct sockaddr *)&addr, sizeof(addr)));
    ASSERT_EQ(1, SSL_set_fd(ssl.get(), sock));
    ASSERT_TRUE(SSL_connect(ssl.get()) > 0)  << ERR_error_string(ERR_get_error(), nullptr);

    // Obtain the SSL_SESSION
    bssl::UniquePtr<SSL_SESSION> session1 {SSL_get1_session(ssl.get())};
    ASSERT_TRUE(session1);

    // SSL_SESSION_to_bytes()
    uint8_t *session1_buf;
    size_t session1_len;
    ASSERT_TRUE(SSL_SESSION_to_bytes(session1.get(), &session1_buf, &session1_len));
    bssl::UniquePtr<uint8_t> tmp1 {session1_buf}; // We own the returned buffer
    ASSERT_TRUE(session1_buf);
    ASSERT_TRUE(session1_len);

    // SSL_SESSION_from_bytes()
    bssl::UniquePtr<SSL_SESSION> session2 {SSL_SESSION_from_bytes(session1_buf, session1_len, ctx.get())};
    ASSERT_TRUE(session2);

    // Compare SSL_SESSION_get_id() on session1 and session2
    unsigned int session1_id_len;
    const uint8_t *session1_id_buf = SSL_SESSION_get_id(session1.get(), &session1_id_len);
    unsigned int session2_id_len;
    const uint8_t *session2_id_buf = SSL_SESSION_get_id(session2.get(), &session2_id_len);
    ASSERT_EQ(session1_id_len, session2_id_len);
    for(size_t i = 0; i < session1_id_len; i++) {
      ASSERT_EQ(session1_id_buf[i], session2_id_buf[i]);
    }

    // Compare SSL_SESSION_is_resumable() on session1 and session2
    int session1_resumable = SSL_SESSION_is_resumable(session1.get());
    int session2_resumable = SSL_SESSION_is_resumable(session2.get());
    ASSERT_EQ(session1_resumable, session2_resumable);

    // SSL_SESSION_to_bytes()
    uint8_t *session2_buf;
    size_t session2_len;
    ASSERT_TRUE(SSL_SESSION_to_bytes(session2.get(), &session2_buf, &session2_len));
    bssl::UniquePtr<uint8_t> tmp2 {session2_buf}; // We own the returned buffer
    ASSERT_TRUE(session2_buf);
    ASSERT_TRUE(session2_len);

    // Compare session2_len/buf & session1_len/buf
    ASSERT_EQ(session1_len, session2_len);
    for(size_t i = 0; i < session1_len; i++) {
      ASSERT_EQ(session1_buf[i], session2_buf[i]);
    }

    char buf[sizeof(MESSAGE)];
    ASSERT_EQ(sizeof(MESSAGE), SSL_write(ssl.get(), MESSAGE, sizeof(MESSAGE)));
    ASSERT_EQ(sizeof(MESSAGE), SSL_read(ssl.get(), buf, sizeof(buf)));
  }

  server.join();
}

TEST(SSLTest, test_SSL_set1_curves_list) {
  struct {
    const char *curves;
    int expected_result;
  }
  test_strings[] {
    { SN_secp224r1 ":" SN_secp384r1 ":" SN_secp521r1 ":" SN_X25519 ":" SN_X9_62_prime256v1, 1},
    { "P-224", 1 },
    { "P-224:P-521:P-256:X25519", 1 },
    { "P-224:P-521:P-25:X25519", 0 },
    { "no:valid:curves", 0 },
    { ":", 0 },
    { ":::", 0 },
    { "", 0 }
  };

  for(const auto &test_string : test_strings) {
    bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_server_method()));
    bssl::UniquePtr<SSL> ssl { SSL_new(ctx.get()) };
    ASSERT_EQ(test_string.expected_result, SSL_set1_curves_list(ssl.get(), test_string.curves));
  }
}





static bool CompleteHandshakes(SSL *client, SSL *server) {
  bool result {true};

  while(result) {
    int client_ret = SSL_do_handshake(client);
    int client_err = SSL_get_error(client, client_ret);

    int server_ret = SSL_do_handshake(server);
    int server_err = SSL_get_error(server, server_ret);

    if (client_err != SSL_ERROR_NONE && client_err != SSL_ERROR_WANT_READ && client_err != SSL_ERROR_WANT_WRITE) {
      fprintf(stderr, "Client error: %s\n", SSL_error_description(client_err));
      ERR_print_errors_fp(stderr);
      result = false;
    }

    if (server_err != SSL_ERROR_NONE && server_err != SSL_ERROR_WANT_READ && server_err != SSL_ERROR_WANT_WRITE) {
      fprintf(stderr, "Server error: %s\n", SSL_error_description(server_err));
      ERR_print_errors_fp(stderr);
      result = false;
    }

    if (client_ret == 1 && server_ret == 1) {
      break;
    }
  }

  return result;
}

class SocketCloser {
  public:
    SocketCloser(int socket) : m_socket{socket} {}
    ~SocketCloser() { ::close(m_socket); }
  private:
    SocketCloser(const SocketCloser&) = default;
    SocketCloser &operator=(const SocketCloser&) = default;
  private:
    int m_socket;
};

TEST(SSLTest, test_SSL_set_fd) {
  TempFile root_ca_cert_pem        { root_ca_cert_pem_str };
  TempFile client_2_key_pem        { client_2_key_pem_str };
  TempFile client_2_cert_chain_pem { client_2_cert_chain_pem_str };
  TempFile server_2_key_pem        { server_2_key_pem_str };
  TempFile server_2_cert_chain_pem { server_2_cert_chain_pem_str };

  int sockets[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets));
  SocketCloser close[] { sockets[0], sockets[1] };

  bssl::UniquePtr<SSL_CTX> server_ctx(SSL_CTX_new(TLS_method()));
  bssl::UniquePtr<SSL_CTX> client_ctx(SSL_CTX_new(TLS_method()));

  SSL_CTX_set_verify(client_ctx.get(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
  ASSERT_TRUE(SSL_CTX_use_certificate_chain_file(server_ctx.get(), server_2_cert_chain_pem.path()));
  ASSERT_TRUE(SSL_CTX_use_PrivateKey_file(server_ctx.get(), server_2_key_pem.path(), SSL_FILETYPE_PEM));
  ASSERT_TRUE(SSL_CTX_load_verify_locations(client_ctx.get(), root_ca_cert_pem.path(), nullptr));

  bssl::UniquePtr<SSL> server_ssl(SSL_new(server_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(server_ssl.get(), sockets[0]));
  SSL_set_accept_state(server_ssl.get());

  bssl::UniquePtr<SSL> client_ssl(SSL_new(client_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(client_ssl.get(), sockets[1]));
  SSL_set_connect_state(client_ssl.get());

  ASSERT_TRUE(CompleteHandshakes(client_ssl.get(), server_ssl.get()));
}

TEST(SSLTest, test_SSL_ex_data) {
  // A counter of the number of times that our free_func has been called. Since
  // we cannot uninstall our new index, nor the free_func that's associated with
  // it, we need to avoid side effects that may be caused by free_func
  // invocations that will occur during subsequent tests. This is why this
  // counter is static; if it was stack based, the free_func would corrupt the
  // contents of stack at the same address when it's called from other tests.
  static int free_func_calls = 0;

  auto free_func = [](void *parent, void *ptr, CRYPTO_EX_DATA *ad, int index, long argl, void *argp) {
    (*reinterpret_cast<int*>(argp))++;
    if (ptr) {
      ASSERT_EQ(42, *reinterpret_cast<int*>(ptr));
      delete reinterpret_cast<int*>(ptr);
    }
  };

  int my_int_index = SSL_get_ex_new_index(0, &free_func_calls, nullptr, nullptr, free_func);

  ASSERT_EQ(0, free_func_calls);
  ASSERT_NE(-1, my_int_index);

  ASSERT_NE(my_int_index, SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr));

  bssl::UniquePtr<SSL_CTX> ctx {SSL_CTX_new(TLS_method())};
  {
    bssl::UniquePtr<SSL> ssl {SSL_new(ctx.get())};
    int *i {new int{42}};
    SSL_set_ex_data(ssl.get(), my_int_index, i);
    ASSERT_EQ(i, SSL_get_ex_data(ssl.get(), my_int_index));
    // ssl gets deleted, so *i hould get deleted
    ASSERT_EQ(0, free_func_calls);
  }
  ASSERT_EQ(1, free_func_calls);
  {
    int i {42};
    bssl::UniquePtr<SSL> ssl {SSL_new(ctx.get())};
    SSL_set_ex_data(ssl.get(), my_int_index, &i);
    ASSERT_EQ(&i, SSL_get_ex_data(ssl.get(), my_int_index));
    // Setting the data to nullptr should not delete the previous value
    SSL_set_ex_data(ssl.get(), my_int_index, nullptr);
    ASSERT_EQ(1, free_func_calls);
  }
  ASSERT_EQ(2, free_func_calls);
}

TEST(SSLTest, test_SSL_set_cipher_list) {
  bssl::UniquePtr<SSL_CTX> ctx {SSL_CTX_new(TLS_server_method())};
  bssl::UniquePtr<SSL> ssl {SSL_new(ctx.get())};

  ASSERT_TRUE(SSL_set_cipher_list(ssl.get(), "HIGH:!aNULL:!kRSA:!PSK:!SRP:!MD5:!RC4"));
  ASSERT_TRUE(SSL_set_cipher_list(ssl.get(), "HIGH:!aNULL:!kRSA:!PSK:!SRP:!MD5:!RC4"));
  ASSERT_FALSE(SSL_set_cipher_list(ssl.get(), "NO:VALID:CIPHERS"));
  ASSERT_TRUE(SSL_set_cipher_list(ssl.get(), "ONE:VALID:CIPHER:PSK"));
  ASSERT_TRUE(SSL_set_cipher_list(ssl.get(), "HIGH:!aNULL:!kRSA:!PSK:!SRP:!MD5:!RC4"));
}

TEST(SSLTest, test_SSL_set_app_data) {
  bssl::UniquePtr<SSL_CTX> ctx {SSL_CTX_new(TLS_server_method())};
  bssl::UniquePtr<SSL> ssl {SSL_new(ctx.get())};

  int i {42};

  ASSERT_TRUE(SSL_set_app_data(ssl.get(), &i));
  ASSERT_EQ(&i, SSL_get_app_data(ssl.get()));
  ASSERT_TRUE(SSL_set_app_data(ssl.get(), nullptr));
  ASSERT_EQ(nullptr, SSL_get_app_data(ssl.get()));
}

TEST(SSLTest, test_SSL_set_alpn_protos) {
  TempFile root_ca_cert_pem        { root_ca_cert_pem_str };
  TempFile client_2_key_pem        { client_2_key_pem_str };
  TempFile client_2_cert_chain_pem { client_2_cert_chain_pem_str };
  TempFile server_2_key_pem        { server_2_key_pem_str };
  TempFile server_2_cert_chain_pem { server_2_cert_chain_pem_str };

  int sockets[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets));
  SocketCloser close[] { sockets[0], sockets[1] };

  bssl::UniquePtr<SSL_CTX> server_ctx(SSL_CTX_new(TLS_method()));
  bssl::UniquePtr<SSL_CTX> client_ctx(SSL_CTX_new(TLS_method()));

  SSL_CTX_set_verify(client_ctx.get(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
  ASSERT_TRUE(SSL_CTX_use_certificate_chain_file(server_ctx.get(), server_2_cert_chain_pem.path()));
  ASSERT_TRUE(SSL_CTX_use_PrivateKey_file(server_ctx.get(), server_2_key_pem.path(), SSL_FILETYPE_PEM));
  ASSERT_TRUE(SSL_CTX_load_verify_locations(client_ctx.get(), root_ca_cert_pem.path(), nullptr));

  bssl::UniquePtr<SSL> server_ssl(SSL_new(server_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(server_ssl.get(), sockets[0]));
  SSL_set_accept_state(server_ssl.get());

  bssl::UniquePtr<SSL> client_ssl(SSL_new(client_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(client_ssl.get(), sockets[1]));
  SSL_set_connect_state(client_ssl.get());

  static const uint8_t server_protos[] {
    2, 'h', '2',
    8, 'h', 't', 't', 'p', '/', '1', '.', '1',
    6, 's', 'p', 'd', 'y', '/', '1',
  };

  static const uint8_t client_protos[] {
    6, 's', 'p', 'd', 'y', '/', '1',
    8, 'h', 't', 't', 'p', '/', '1', '.', '1'
  };
  
  // Set up a ALPN callback on the server which checks that in == client_protos
  // and then uses SSL_select_next_proto() to make it's selection
  SSL_CTX_set_alpn_select_cb(server_ctx.get(), [](SSL *ssl, const uint8_t **out, uint8_t *out_len, const uint8_t *in, unsigned in_len, void *arg)-> int {
    if (in_len != sizeof(client_protos)) {
      return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
    if (memcmp(client_protos, in, in_len) != 0) {
      return SSL_TLSEXT_ERR_ALERT_FATAL;
    }

    if (SSL_select_next_proto(const_cast<uint8_t**>(out), out_len, in, in_len, server_protos, sizeof(server_protos)) == OPENSSL_NPN_NEGOTIATED) {
      return SSL_TLSEXT_ERR_OK;
    }

    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }, nullptr);

  // Set the clients list of ALPN protocols
  ASSERT_EQ(0, SSL_set_alpn_protos(client_ssl.get(), client_protos, sizeof(client_protos)));

  ASSERT_TRUE(CompleteHandshakes(client_ssl.get(), server_ssl.get()));

  const uint8_t *selected;
  unsigned selected_len;
  SSL_get0_alpn_selected(client_ssl.get(), &selected, &selected_len);
  // const uint8_t **out_data, unsigned *out_len

  const char *expected {"spdy/1"};
  size_t expected_len {strlen(expected)};
  ASSERT_EQ(expected_len, selected_len);
  ASSERT_EQ(0, memcmp(expected, selected, expected_len));
}

TEST(SSLTest, test_SSL_CTX_app_data) {
  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  ASSERT_EQ(1, SSL_CTX_set_app_data(ctx.get(), ctx.get()));
  ASSERT_EQ(ctx.get(), SSL_CTX_get_app_data(ctx.get()));
}

TEST(SSLTest, test_SSL_get_signature_algorithm_digest) {
  EXPECT_EQ(EVP_sha1(), SSL_get_signature_algorithm_digest(SSL_SIGN_RSA_PKCS1_SHA1));
  EXPECT_EQ(EVP_sha256(), SSL_get_signature_algorithm_digest(SSL_SIGN_RSA_PKCS1_SHA256));
  EXPECT_EQ(EVP_sha384(), SSL_get_signature_algorithm_digest(SSL_SIGN_RSA_PKCS1_SHA384));
  EXPECT_EQ(EVP_sha512(), SSL_get_signature_algorithm_digest(SSL_SIGN_RSA_PKCS1_SHA512));
  EXPECT_EQ(EVP_sha1(), SSL_get_signature_algorithm_digest(SSL_SIGN_ECDSA_SHA1));
  EXPECT_EQ(EVP_sha256(), SSL_get_signature_algorithm_digest(SSL_SIGN_ECDSA_SECP256R1_SHA256));
  EXPECT_EQ(EVP_sha384(), SSL_get_signature_algorithm_digest(SSL_SIGN_ECDSA_SECP384R1_SHA384));
  EXPECT_EQ(EVP_sha512(), SSL_get_signature_algorithm_digest(SSL_SIGN_ECDSA_SECP521R1_SHA512));
  EXPECT_EQ(EVP_sha256(), SSL_get_signature_algorithm_digest(SSL_SIGN_RSA_PSS_RSAE_SHA256));
  EXPECT_EQ(EVP_sha384(), SSL_get_signature_algorithm_digest(SSL_SIGN_RSA_PSS_RSAE_SHA384));
  EXPECT_EQ(EVP_sha512(), SSL_get_signature_algorithm_digest(SSL_SIGN_RSA_PSS_RSAE_SHA512));
  EXPECT_EQ(nullptr, SSL_get_signature_algorithm_digest(SSL_SIGN_ED25519));
}

TEST(SSLTest, test_SSL_get_signature_algorithm_key_type) {
  struct {
    uint16_t sigalg;
    int keytype;
  }
  keytypes[] {
    { SSL_SIGN_RSA_PKCS1_SHA1,         EVP_PKEY_RSA },
    { SSL_SIGN_RSA_PKCS1_SHA256,       EVP_PKEY_RSA },
    { SSL_SIGN_RSA_PKCS1_SHA384,       EVP_PKEY_RSA },
    { SSL_SIGN_RSA_PKCS1_SHA512,       EVP_PKEY_RSA },
    { SSL_SIGN_ECDSA_SHA1,             EVP_PKEY_EC },
    { SSL_SIGN_ECDSA_SECP256R1_SHA256, EVP_PKEY_EC },
    { SSL_SIGN_ECDSA_SECP384R1_SHA384, EVP_PKEY_EC },
    { SSL_SIGN_ECDSA_SECP521R1_SHA512, EVP_PKEY_EC },
    { SSL_SIGN_RSA_PSS_RSAE_SHA256,    EVP_PKEY_RSA },
    { SSL_SIGN_RSA_PSS_RSAE_SHA384,    EVP_PKEY_RSA },
    { SSL_SIGN_RSA_PSS_RSAE_SHA512,    EVP_PKEY_RSA },
    { SSL_SIGN_ED25519,                EVP_PKEY_ED25519 },
  };

  for(int i = 0; i < (sizeof(keytypes) / sizeof(keytypes[0])); i++) {
    EXPECT_EQ(keytypes[i].keytype, SSL_get_signature_algorithm_key_type(keytypes[i].sigalg)) << "keytypes[" << i << "]";
  }
}

TEST(SSLTest, test_SSL_is_signature_algorithm_rsa_pss) {
  struct {
    uint16_t sigalg;
    int is_rsa_pss;
  }
  sigalgs[] {
    { SSL_SIGN_RSA_PKCS1_SHA1,         0 },
    { SSL_SIGN_RSA_PKCS1_SHA256,       0 },
    { SSL_SIGN_RSA_PKCS1_SHA384,       0 },
    { SSL_SIGN_RSA_PKCS1_SHA512,       0 },
    { SSL_SIGN_ECDSA_SHA1,             0 },
    { SSL_SIGN_ECDSA_SECP256R1_SHA256, 0 },
    { SSL_SIGN_ECDSA_SECP384R1_SHA384, 0 },
    { SSL_SIGN_ECDSA_SECP521R1_SHA512, 0 },
    { SSL_SIGN_RSA_PSS_RSAE_SHA256,    1 },
    { SSL_SIGN_RSA_PSS_RSAE_SHA384,    1 },
    { SSL_SIGN_RSA_PSS_RSAE_SHA512,    1 },
    { SSL_SIGN_ED25519,                0 },
  };

  for(int i = 0; i < (sizeof(sigalgs) / sizeof(sigalgs[0])); i++) {
    EXPECT_EQ(sigalgs[i].is_rsa_pss, SSL_is_signature_algorithm_rsa_pss(sigalgs[i].sigalg)) << "sigalgs[" << i << "]";
  }
}

TEST(SSLTest, test_SSL_alert_from_verify_result) {
  ASSERT_EQ(SSL_AD_UNKNOWN_CA, SSL_alert_from_verify_result(X509_V_ERR_INVALID_CA));
  ASSERT_EQ(SSL_AD_UNKNOWN_CA, SSL_alert_from_verify_result(X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT));

  ASSERT_EQ(SSL_AD_CERTIFICATE_EXPIRED, SSL_alert_from_verify_result(X509_V_ERR_CERT_HAS_EXPIRED));
  ASSERT_EQ(SSL_AD_CERTIFICATE_EXPIRED, SSL_alert_from_verify_result(X509_V_ERR_CRL_HAS_EXPIRED));
}

TEST(SSLTest, test_SSL_get0_peer_verify_algorithms) {
  TempFile root_ca_cert_pem        { root_ca_cert_pem_str };
  TempFile client_2_key_pem        { client_2_key_pem_str };
  TempFile client_2_cert_chain_pem { client_2_cert_chain_pem_str };
  TempFile server_2_key_pem        { server_2_key_pem_str };
  TempFile server_2_cert_chain_pem { server_2_cert_chain_pem_str };

  int sockets[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets));
  SocketCloser close[] { sockets[0], sockets[1] };

  bssl::UniquePtr<SSL_CTX> server_ctx(SSL_CTX_new(TLS_method()));
  bssl::UniquePtr<SSL_CTX> client_ctx(SSL_CTX_new(TLS_method()));

  ASSERT_EQ(1, SSL_CTX_set1_sigalgs_list(client_ctx.get(), "rsa_pkcs1_sha256:rsa_pss_rsae_sha256:ecdsa_secp256r1_sha256"));

  SSL_CTX_set_verify(client_ctx.get(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
  ASSERT_TRUE(SSL_CTX_use_certificate_chain_file(server_ctx.get(), server_2_cert_chain_pem.path()));
  ASSERT_TRUE(SSL_CTX_use_PrivateKey_file(server_ctx.get(), server_2_key_pem.path(), SSL_FILETYPE_PEM));
  ASSERT_TRUE(SSL_CTX_load_verify_locations(client_ctx.get(), root_ca_cert_pem.path(), nullptr));

  bssl::UniquePtr<SSL> server_ssl(SSL_new(server_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(server_ssl.get(), sockets[0]));
  SSL_set_accept_state(server_ssl.get());

  bssl::UniquePtr<SSL> client_ssl(SSL_new(client_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(client_ssl.get(), sockets[1]));
  SSL_set_connect_state(client_ssl.get());

  auto cert_cb = [](SSL *ssl, void *arg) -> int {
    const uint16_t *sigalgs {nullptr};
    size_t nsigalgs {SSL_get0_peer_verify_algorithms(ssl, &sigalgs)};

    EXPECT_EQ(3, nsigalgs);
    EXPECT_EQ(SSL_SIGN_RSA_PKCS1_SHA256, sigalgs[0]);
    EXPECT_EQ(SSL_SIGN_RSA_PSS_RSAE_SHA256, sigalgs[1]);
    EXPECT_EQ(SSL_SIGN_ECDSA_SECP256R1_SHA256, sigalgs[2]);

    nsigalgs = SSL_get0_peer_verify_algorithms(ssl, &sigalgs);

    EXPECT_EQ(3, nsigalgs);
    EXPECT_EQ(SSL_SIGN_RSA_PKCS1_SHA256, sigalgs[0]);
    EXPECT_EQ(SSL_SIGN_RSA_PSS_RSAE_SHA256, sigalgs[1]);
    EXPECT_EQ(SSL_SIGN_ECDSA_SECP256R1_SHA256, sigalgs[2]);

    return 1;
  };

  SSL_set_cert_cb(server_ssl.get(), cert_cb, reinterpret_cast<void*>(__LINE__));

  ASSERT_TRUE(CompleteHandshakes(client_ssl.get(), server_ssl.get()));
}


TEST(SSLTest, test_SSL_get_servername_inside_select_certificate_cb) {
  static const char SERVERNAME[] { "www.example.com" };

  TempFile server_2_key_pem        { server_2_key_pem_str };
  TempFile server_2_cert_chain_pem { server_2_cert_chain_pem_str };

  int sockets[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets));
  SocketCloser close[] { sockets[0], sockets[1] };

  bssl::UniquePtr<SSL_CTX> server_ctx(SSL_CTX_new(TLS_server_method()));
  bssl::UniquePtr<SSL_CTX> client_ctx(SSL_CTX_new(TLS_client_method()));

  // Set up server with a callback which checks if SSL_get_servername() works
  SSL_CTX_set_select_certificate_cb(server_ctx.get(), [](const SSL_CLIENT_HELLO *client_hello) -> ssl_select_cert_result_t {
    const char *server_name = SSL_get_servername(client_hello->ssl, TLSEXT_NAMETYPE_host_name);
    if (server_name && strcmp(server_name, SERVERNAME) == 0) {
      return ssl_select_cert_success;
    }
    return ssl_select_cert_error; // Will cause handshake failure
  });
  ASSERT_TRUE(SSL_CTX_use_certificate_chain_file(server_ctx.get(), server_2_cert_chain_pem.path()));
  ASSERT_TRUE(SSL_CTX_use_PrivateKey_file(server_ctx.get(), server_2_key_pem.path(), SSL_FILETYPE_PEM));
  bssl::UniquePtr<SSL> server_ssl(SSL_new(server_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(server_ssl.get(), sockets[0]));
  SSL_set_accept_state(server_ssl.get());

  // Set up client
  SSL_CTX_set_verify(client_ctx.get(), SSL_VERIFY_NONE, nullptr);
  bssl::UniquePtr<SSL> client_ssl(SSL_new(client_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(client_ssl.get(), sockets[1]));
  ASSERT_TRUE(SSL_set_tlsext_host_name(client_ssl.get(), SERVERNAME));
  SSL_set_connect_state(client_ssl.get());

  ASSERT_TRUE(CompleteHandshakes(client_ssl.get(), server_ssl.get()));
}


/**
 * @brief This test exercises a leak in SSL_get_servername()
 *
 * If SSL_get_servername() was invoked multiple times from the same certificate
 * selection callback, it was leaking the string value that was returned from
 * the previous invocation(s).
 *
 * Note that the string returned by the _last_ SSL_get_servername() invocation,
 * inside a certificate selection callback, does _not_ leak i.e. if
 * SSL_get_servername() is only called once during a callback, there is no leak.
 * It only leaks when SSL_get_servername() is called more than once during the
 * same callback.
 */
TEST(SSLTest, test_SSL_get_servername_leak_inside_select_certificate_cb) {
  static const char SERVERNAME[] { "www.example.com" };

  TempFile server_2_key_pem        { server_2_key_pem_str };
  TempFile server_2_cert_chain_pem { server_2_cert_chain_pem_str };

  int sockets[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets));
  SocketCloser close[] { sockets[0], sockets[1] };

  bssl::UniquePtr<SSL_CTX> server_ctx(SSL_CTX_new(TLS_server_method()));
  bssl::UniquePtr<SSL_CTX> client_ctx(SSL_CTX_new(TLS_client_method()));

  // Set up a certificate selection callback which calls SSL_get_servername() 5 times.
  // This will result in 4 leaks if the SSL_get_servername() fix is not in place.
  SSL_CTX_set_select_certificate_cb(server_ctx.get(), [](const SSL_CLIENT_HELLO *client_hello) -> ssl_select_cert_result_t {
    SSL_get_servername(client_hello->ssl, TLSEXT_NAMETYPE_host_name);
    SSL_get_servername(client_hello->ssl, TLSEXT_NAMETYPE_host_name);
    SSL_get_servername(client_hello->ssl, TLSEXT_NAMETYPE_host_name);
    SSL_get_servername(client_hello->ssl, TLSEXT_NAMETYPE_host_name);
    SSL_get_servername(client_hello->ssl, TLSEXT_NAMETYPE_host_name);
    return ssl_select_cert_success;
  });
  ASSERT_TRUE(SSL_CTX_use_certificate_chain_file(server_ctx.get(), server_2_cert_chain_pem.path()));
  ASSERT_TRUE(SSL_CTX_use_PrivateKey_file(server_ctx.get(), server_2_key_pem.path(), SSL_FILETYPE_PEM));
  bssl::UniquePtr<SSL> server_ssl(SSL_new(server_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(server_ssl.get(), sockets[0]));
  SSL_set_accept_state(server_ssl.get());

  // Set up client
  SSL_CTX_set_verify(client_ctx.get(), SSL_VERIFY_NONE, nullptr);
  bssl::UniquePtr<SSL> client_ssl(SSL_new(client_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(client_ssl.get(), sockets[1]));
  ASSERT_TRUE(SSL_set_tlsext_host_name(client_ssl.get(), SERVERNAME));
  SSL_set_connect_state(client_ssl.get());

  ASSERT_TRUE(CompleteHandshakes(client_ssl.get(), server_ssl.get()));
}


TEST(SSLTest, test_SSL_get_servername_null_inside_select_certificate_cb) {
  TempFile server_2_key_pem        { server_2_key_pem_str };
  TempFile server_2_cert_chain_pem { server_2_cert_chain_pem_str };

  int sockets[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets));
  SocketCloser close[] { sockets[0], sockets[1] };

  bssl::UniquePtr<SSL_CTX> server_ctx(SSL_CTX_new(TLS_server_method()));
  bssl::UniquePtr<SSL_CTX> client_ctx(SSL_CTX_new(TLS_client_method()));

  // Set up server with a callback to check if SSL_get_servername() returns
  // a nullptr if the client didn't send an SNI host name extension
  SSL_CTX_set_select_certificate_cb(server_ctx.get(), [](const SSL_CLIENT_HELLO *client_hello) -> ssl_select_cert_result_t {
    return SSL_get_servername(client_hello->ssl, TLSEXT_NAMETYPE_host_name) ? ssl_select_cert_error : ssl_select_cert_success;
  });
  ASSERT_TRUE(SSL_CTX_use_certificate_chain_file(server_ctx.get(), server_2_cert_chain_pem.path()));
  ASSERT_TRUE(SSL_CTX_use_PrivateKey_file(server_ctx.get(), server_2_key_pem.path(), SSL_FILETYPE_PEM));
  bssl::UniquePtr<SSL> server_ssl(SSL_new(server_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(server_ssl.get(), sockets[0]));
  SSL_set_accept_state(server_ssl.get());

  // Set up client
  SSL_CTX_set_verify(client_ctx.get(), SSL_VERIFY_NONE, nullptr);
  bssl::UniquePtr<SSL> client_ssl(SSL_new(client_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(client_ssl.get(), sockets[1]));
  SSL_set_connect_state(client_ssl.get());

  ASSERT_TRUE(CompleteHandshakes(client_ssl.get(), server_ssl.get()));
}

#ifdef BSSL_COMPAT
// Tests for segv when calling SSL_CIPHER_get_min_version() on a cipher that is
// known to OpenSSL, but whos implementation engine is not loaded.
// The TLS_GOSTR341001_WITH_28147_CNT_IMIT cipher fits this bill because it is
// known to OpenSSL but it's implementaion is only available when the cgost
// engine is configured in.
TEST(SSLTest,SSL_CIPHER_get_min_version_on_non_loaded_cipher) {
  const SSL_CIPHER *cipher = SSL_get_cipher_by_value(0x0081);
  ASSERT_TRUE(cipher);

  const char *openssl_name {SSL_CIPHER_get_name(cipher)};
  ASSERT_TRUE(openssl_name);
  ASSERT_STREQ("GOST2001-GOST89-GOST89", openssl_name);

  const char *standard_name = SSL_CIPHER_standard_name(cipher);
  ASSERT_TRUE(standard_name);
  ASSERT_STREQ("TLS_GOSTR341001_WITH_28147_CNT_IMIT", standard_name);

  ASSERT_EQ(TLS1_2_VERSION, SSL_CIPHER_get_min_version(cipher));
}
#endif

TEST(SSLTest, test_SSL_set_ocsp_response_inside_select_certificate_cb) {
  TempFile server_2_key_pem        { server_2_key_pem_str };
  TempFile server_2_cert_chain_pem { server_2_cert_chain_pem_str };

  static const uint8_t OCSP_RESPONSE[] { 1, 2, 3, 4, 5 };

  int sockets[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets));
  SocketCloser close[] { sockets[0], sockets[1] };

  bssl::UniquePtr<SSL_CTX> server_ctx(SSL_CTX_new(TLS_server_method()));
  bssl::UniquePtr<SSL_CTX> client_ctx(SSL_CTX_new(TLS_client_method()));

  // Set up server with a select certificate callback that calls SSL_set_ocsp_response()
  SSL_CTX_set_select_certificate_cb(server_ctx.get(), [](const SSL_CLIENT_HELLO *client_hello) -> ssl_select_cert_result_t {
    return (SSL_set_ocsp_response(client_hello->ssl, OCSP_RESPONSE, sizeof(OCSP_RESPONSE)) == 1) ?
      ssl_select_cert_success : ssl_select_cert_error;
  });
  ASSERT_TRUE(SSL_CTX_use_certificate_chain_file(server_ctx.get(), server_2_cert_chain_pem.path()));
  ASSERT_TRUE(SSL_CTX_use_PrivateKey_file(server_ctx.get(), server_2_key_pem.path(), SSL_FILETYPE_PEM));
  bssl::UniquePtr<SSL> server_ssl(SSL_new(server_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(server_ssl.get(), sockets[0]));
  SSL_set_accept_state(server_ssl.get());

  // Set up client with ocsp stapling enabled
  SSL_CTX_set_verify(client_ctx.get(), SSL_VERIFY_NONE, nullptr);
  bssl::UniquePtr<SSL> client_ssl(SSL_new(client_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(client_ssl.get(), sockets[1]));
  SSL_set_connect_state(client_ssl.get());
  SSL_enable_ocsp_stapling(client_ssl.get());

  ASSERT_TRUE(CompleteHandshakes(client_ssl.get(), server_ssl.get()));

  // Check that the client received the OCSP response ok
  const uint8_t *ocsp_resp_data{};
  size_t ocsp_resp_len{};
  SSL_get0_ocsp_response(client_ssl.get(), &ocsp_resp_data, &ocsp_resp_len);
  ASSERT_EQ(sizeof(OCSP_RESPONSE), ocsp_resp_len);
  ASSERT_EQ(0, memcmp(OCSP_RESPONSE, ocsp_resp_data, ocsp_resp_len));
}


/**
 * @brief This test exercises a leak in SSL_set_ocsp_response()
 *
 * This test exercises a leak in SSL_set_ocsp_response() that occurs when it is
 * invoked multiple times from within the same certificate selection callback
 * i.e. without the fix, running this test under valgrind or similar memory
 * checker tool will report the memory leak.
 *
 * Note that the leak does _not_ occur if SSL_set_ocsp_response() is only called
 * _once_ from within the same certificate selection callback. It is only the
 * additional calls that leak.
 */
TEST(SSLTest, test_SSL_set_ocsp_response_leak_inside_select_certificate_cb) {
  TempFile server_2_key_pem        { server_2_key_pem_str };
  TempFile server_2_cert_chain_pem { server_2_cert_chain_pem_str };

  static const uint8_t OCSP_RESPONSE[] { 1, 2, 3, 4, 5 };

  int sockets[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets));
  SocketCloser close[] { sockets[0], sockets[1] };

  bssl::UniquePtr<SSL_CTX> server_ctx(SSL_CTX_new(TLS_server_method()));
  bssl::UniquePtr<SSL_CTX> client_ctx(SSL_CTX_new(TLS_client_method()));

  // Set up server with a select certificate callback that calls
  // SSL_set_ocsp_response() 5 times. This will result in 4 leaks if the
  // SSL_set_ocsp_response() fix is not in place.
  SSL_CTX_set_select_certificate_cb(server_ctx.get(), [](const SSL_CLIENT_HELLO *client_hello) -> ssl_select_cert_result_t {
    SSL_set_ocsp_response(client_hello->ssl, OCSP_RESPONSE, sizeof(OCSP_RESPONSE));
    SSL_set_ocsp_response(client_hello->ssl, OCSP_RESPONSE, sizeof(OCSP_RESPONSE));
    SSL_set_ocsp_response(client_hello->ssl, OCSP_RESPONSE, sizeof(OCSP_RESPONSE));
    SSL_set_ocsp_response(client_hello->ssl, OCSP_RESPONSE, sizeof(OCSP_RESPONSE));
    SSL_set_ocsp_response(client_hello->ssl, OCSP_RESPONSE, sizeof(OCSP_RESPONSE));
    return ssl_select_cert_success;
  });
  ASSERT_TRUE(SSL_CTX_use_certificate_chain_file(server_ctx.get(), server_2_cert_chain_pem.path()));
  ASSERT_TRUE(SSL_CTX_use_PrivateKey_file(server_ctx.get(), server_2_key_pem.path(), SSL_FILETYPE_PEM));
  bssl::UniquePtr<SSL> server_ssl(SSL_new(server_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(server_ssl.get(), sockets[0]));
  SSL_set_accept_state(server_ssl.get());

  // Set up client with ocsp stapling enabled
  SSL_CTX_set_verify(client_ctx.get(), SSL_VERIFY_NONE, nullptr);
  bssl::UniquePtr<SSL> client_ssl(SSL_new(client_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(client_ssl.get(), sockets[1]));
  SSL_set_connect_state(client_ssl.get());
  SSL_enable_ocsp_stapling(client_ssl.get());

  ASSERT_TRUE(CompleteHandshakes(client_ssl.get(), server_ssl.get()));

  // Check that the client received the OCSP response ok
  const uint8_t *ocsp_resp_data{};
  size_t ocsp_resp_len{};
  SSL_get0_ocsp_response(client_ssl.get(), &ocsp_resp_data, &ocsp_resp_len);
  ASSERT_EQ(sizeof(OCSP_RESPONSE), ocsp_resp_len);
  ASSERT_EQ(0, memcmp(OCSP_RESPONSE, ocsp_resp_data, ocsp_resp_len));
}


#ifdef BSSL_COMPAT
/**
 * @brief This test exercises a leak that occurs in SSL_set_ocsp_response() if
 * it returns early due to an error, when it is called from within a certificate
 * selection callback.
 *
 * Without a fix for the leak, running this test under valgrind or similar
 * memory checker tool will report the memory leak.
 *
 * Note that because this test uses knowledge of the internals of the
 * SSL_set_ocsp_response() implementation, in bssl-compat, in order to provoke
 * the leak, it will not work the same on BoringSSL proper.
 */
TEST(SSLTest, test_SSL_set_ocsp_response_early_return_leak_inside_select_certificate_cb) {
  TempFile server_2_key_pem        { server_2_key_pem_str };
  TempFile server_2_cert_chain_pem { server_2_cert_chain_pem_str };

  static const uint8_t OCSP_RESPONSE[] { 1, 2, 3, 4, 5 };

  int sockets[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets));
  SocketCloser close[] { sockets[0], sockets[1] };

  bssl::UniquePtr<SSL_CTX> server_ctx(SSL_CTX_new(TLS_server_method()));
  bssl::UniquePtr<SSL_CTX> client_ctx(SSL_CTX_new(TLS_client_method()));

  // Register a dummy tlsext status callback. This will provoke the
  // SSL_set_ocsp_response() call, inside the certificate selection callback, to
  // fail and return early. This in turn will cause the leak to occur if the fix
  // is not in place.
  SSL_CTX_set_tlsext_status_cb(server_ctx.get(), [](SSL *ssl, void *arg) -> int {return 0;});

  // Set up server with a select certificate callback that calls
  // SSL_set_ocsp_response() - which will return early and leak because of the
  // dummy status callback we installed above.
  SSL_CTX_set_select_certificate_cb(server_ctx.get(), [](const SSL_CLIENT_HELLO *client_hello) -> ssl_select_cert_result_t {
    if (SSL_set_ocsp_response(client_hello->ssl, OCSP_RESPONSE, sizeof(OCSP_RESPONSE)) == 1) {
      return ssl_select_cert_success;
    }
    return ssl_select_cert_error;
  });
  ASSERT_TRUE(SSL_CTX_use_certificate_chain_file(server_ctx.get(), server_2_cert_chain_pem.path()));
  ASSERT_TRUE(SSL_CTX_use_PrivateKey_file(server_ctx.get(), server_2_key_pem.path(), SSL_FILETYPE_PEM));
  bssl::UniquePtr<SSL> server_ssl(SSL_new(server_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(server_ssl.get(), sockets[0]));
  SSL_set_accept_state(server_ssl.get());

  // Set up client with ocsp stapling enabled
  SSL_CTX_set_verify(client_ctx.get(), SSL_VERIFY_NONE, nullptr);
  bssl::UniquePtr<SSL> client_ssl(SSL_new(client_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(client_ssl.get(), sockets[1]));
  SSL_set_connect_state(client_ssl.get());
  SSL_enable_ocsp_stapling(client_ssl.get());

  // We expect this to fail because the SSL_set_ocsp_response() call inside the
  // certificate selection callback above will return early with an error,
  // causing the certificate selection callback to fail, which in turn will
  // cause the handshake to fail.
  ASSERT_FALSE(CompleteHandshakes(client_ssl.get(), server_ssl.get()));
}
#endif // BSSL_COMPAT


/**
 * @brief This test exercises a leak in SSL_CTX_set_select_certificate_cb() when
 * the certificate selection callback throws an exception.
 *
 * Without a fix for the leak, running this test under valgrind or similar
 * memory checker tool will report the memory leak.
 */
TEST(SSLTest, test_SSL_CTX_set_select_certificate_cb_leak_from_callback_exception) {
  TempFile server_2_key_pem        { server_2_key_pem_str };
  TempFile server_2_cert_chain_pem { server_2_cert_chain_pem_str };

  int sockets[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets));
  SocketCloser close[] { sockets[0], sockets[1] };

  bssl::UniquePtr<SSL_CTX> server_ctx(SSL_CTX_new(TLS_server_method()));
  bssl::UniquePtr<SSL_CTX> client_ctx(SSL_CTX_new(TLS_client_method()));

  // Set up server with a select certificate callback that raises an exception
  SSL_CTX_set_select_certificate_cb(server_ctx.get(), [](const SSL_CLIENT_HELLO *client_hello) -> ssl_select_cert_result_t {
    throw std::runtime_error("Intentional exception to test for memory leaks");
  });

  ASSERT_TRUE(SSL_CTX_use_certificate_chain_file(server_ctx.get(), server_2_cert_chain_pem.path()));
  ASSERT_TRUE(SSL_CTX_use_PrivateKey_file(server_ctx.get(), server_2_key_pem.path(), SSL_FILETYPE_PEM));
  bssl::UniquePtr<SSL> server_ssl(SSL_new(server_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(server_ssl.get(), sockets[0]));
  SSL_set_accept_state(server_ssl.get());

  // Set up client
  SSL_CTX_set_verify(client_ctx.get(), SSL_VERIFY_NONE, nullptr);
  bssl::UniquePtr<SSL> client_ssl(SSL_new(client_ctx.get()));
  ASSERT_TRUE(SSL_set_fd(client_ssl.get(), sockets[1]));
  SSL_set_connect_state(client_ssl.get());

  // Handshake will fail because of the exception in the callback
  EXPECT_THROW(
    CompleteHandshakes(client_ssl.get(), server_ssl.get()),
    std::runtime_error
  );
}


/**
 * Test that setting a TLS alert and returning ssl_verify_invalid, from a
 * callback installed via SSL_CTX_set_custom_verify(), results in a handshake
 * failure, and that same TLS alert being received by the peer (subject to our
 * restrictions on mapping from TLS alert to X509 error code and back again)
 */
#ifdef BSSL_COMPAT
TEST(SSLTest, test_SSL_CTX_set_custom_verify_alert_codes) {
  TempFile server_2_key_pem        { server_2_key_pem_str };
  TempFile server_2_cert_chain_pem { server_2_cert_chain_pem_str };

  static const std::map<uint8_t,const char*> alerts {
    // Alerts that we can map
    { SSL_AD_HANDSHAKE_FAILURE, "handshake failure" },
    { SSL_AD_CERTIFICATE_EXPIRED, "certificate expired" },
    { SSL_AD_BAD_CERTIFICATE, "bad certificate" },
    { SSL_AD_CERTIFICATE_REVOKED, "certificate revoked" },
    { SSL_AD_DECRYPT_ERROR, "decrypt error" },
    { SSL_AD_UNKNOWN_CA, "unknown CA" },
    { SSL_AD_CERTIFICATE_UNKNOWN, "certificate unknown" },
    { SSL_AD_UNSUPPORTED_CERTIFICATE, "unsupported certificate" },
    { SSL_AD_INTERNAL_ERROR, "internal error" },
    { SSL_AD_CERTIFICATE_REVOKED, "certificate revoked" },
    // Alerts that we cannot map
    { SSL_AD_ACCESS_DENIED, "handshake failure" },
    { SSL_AD_BAD_CERTIFICATE_HASH_VALUE, "handshake failure" },
    { SSL_AD_BAD_CERTIFICATE_STATUS_RESPONSE, "handshake failure" },
    { SSL_AD_BAD_RECORD_MAC, "handshake failure" },
    { SSL_AD_CERTIFICATE_REQUIRED, "handshake failure" },
    { SSL_AD_CERTIFICATE_UNOBTAINABLE, "handshake failure" },
    { SSL_AD_CLOSE_NOTIFY, "handshake failure" },
    { SSL_AD_DECODE_ERROR, "handshake failure" },
    { SSL_AD_DECOMPRESSION_FAILURE, "handshake failure" },
    // An invalid alert code
    { 255, "handshake failure" },
  };

  for (auto const& [alert_code, alert_string] : alerts) {
    int sockets[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets));
    SocketCloser close[] { sockets[0], sockets[1] };

    bssl::UniquePtr<SSL_CTX> server_ctx(SSL_CTX_new(TLS_server_method()));
    bssl::UniquePtr<SSL_CTX> client_ctx(SSL_CTX_new(TLS_client_method()));

    // Install an info callback on the server to check for the expected tls alert
    // If the server reads a TLS alert, it stores the string description of the
    // alert in the SSL object's app data so that the test code can check for it.
    ossl_SSL_CTX_set_info_callback(server_ctx.get(), [](const SSL *ssl, int type, int val){
      if (type & ossl_SSL_CB_READ_ALERT) {
        SSL_set_app_data(const_cast<SSL*>(ssl), ossl_SSL_alert_desc_string_long(val));
      }
    });
    ASSERT_TRUE(SSL_CTX_use_certificate_chain_file(server_ctx.get(), server_2_cert_chain_pem.path()));
    ASSERT_TRUE(SSL_CTX_use_PrivateKey_file(server_ctx.get(), server_2_key_pem.path(), SSL_FILETYPE_PEM));
    bssl::UniquePtr<SSL> server_ssl(SSL_new(server_ctx.get()));
    ASSERT_TRUE(SSL_set_fd(server_ssl.get(), sockets[0]));
    SSL_set_accept_state(server_ssl.get());

    // Set up client with a custom verify callback that will set out_alert and
    // return ssl_verify_invalid. This should cause the client to send the
    // specified TLS alert to the server and cause a handshake failure.
    SSL_CTX_set_custom_verify(client_ctx.get(), SSL_VERIFY_PEER, [](SSL *ssl, uint8_t *out_alert) -> enum ssl_verify_result_t {
      *out_alert = *static_cast<uint8_t*>(SSL_get_app_data(ssl));
      return ssl_verify_invalid;
    });
    bssl::UniquePtr<SSL> client_ssl(SSL_new(client_ctx.get()));
    ASSERT_TRUE(SSL_set_fd(client_ssl.get(), sockets[1]));
    SSL_set_connect_state(client_ssl.get());
    // Setup the TLS alert code to be sent from the custom verify callback
    SSL_set_app_data(client_ssl.get(), &alert_code);

    ASSERT_FALSE(CompleteHandshakes(client_ssl.get(), server_ssl.get()));

    // Check that the server received the expected TLS alert
    ASSERT_STREQ(alert_string, static_cast<char*>(SSL_get_app_data(server_ssl.get())));
  }
}
#endif


TEST(SSLTest, test_SSL_get_all_cipher_names) {
  // Get the size by passing a zero size input buffer
  size_t size1 = SSL_get_all_cipher_names(nullptr, 0);
  ASSERT_GT(size1, 0);

  // Allocate a buffer of the size returned above
  std::unique_ptr<const char*[]> names1(new const char*[size1]);

  // Call SSL_get_all_cipher_names() with the allocated buffer
  size_t size2 = SSL_get_all_cipher_names(names1.get(), size1);

  // Check that the size returned is the same as the size we allocated
  ASSERT_EQ(size2, size1);

  // Check that the names are not null and have a non-zero length
  for (size_t i = 0; i < size2; i++) {
    ASSERT_NE(names1.get()[i], nullptr);
    ASSERT_GT(strlen(names1.get()[i]), 0);
    // printf("%s\n", names1.get()[i]);
  }

  // Allocate another buffer that is one element too short (size2 - 1)
  std::unique_ptr<const char*[]> names2(new const char*[size2 - 1]);

  // Call SSL_get_all_cipher_names() with the short buffer
  size_t size3 = SSL_get_all_cipher_names(names2.get(), size2 - 1);

  // Check that the size returned is the number of ciphers it
  // would have returned if the buffer had been big enough.
  ASSERT_EQ(size3, size2);

  // Check that the names are not null and have a non-zero length
  for (size_t i = 0; i < (size2 - 1); i++) {
    ASSERT_NE(names2.get()[i], nullptr);
    ASSERT_GT(strlen(names2.get()[i]), 0);
    // printf("%s\n", names2.get()[i]);
  }
}


TEST(SSLTest, test_SSL_get_all_signature_algorithm_names) {
  // Get the size by passing a zero size input buffer
  size_t size1 = SSL_get_all_signature_algorithm_names(nullptr, 0);
  ASSERT_GT(size1, 0);

  // Allocate a buffer of the size returned above
  std::unique_ptr<const char*[]> names1(new const char*[size1]);

  // Call SSL_get_all_signature_algorithm_names() with the allocated buffer
  size_t size2 = SSL_get_all_signature_algorithm_names(names1.get(), size1);

  // Check that the size returned is the same as the size we allocated
  ASSERT_EQ(size2, size1);

  // Check that the names are not null and have a non-zero length
  for (size_t i = 0; i < size2; i++) {
    ASSERT_NE(names1.get()[i], nullptr);
    ASSERT_GT(strlen(names1.get()[i]), 0);
    // printf("%s\n", names1.get()[i]);
  }

  // Allocate another buffer that is one element too short (size2 - 1)
  std::unique_ptr<const char*[]> names2(new const char*[size2 - 1]);

  // Call SSL_get_all_signature_algorithm_names() with the short buffer
  size_t size3 = SSL_get_all_signature_algorithm_names(names2.get(), size2 - 1);

  // Check that the size returned is the number of ciphers it
  // would have returned if the buffer had been big enough.
  ASSERT_EQ(size3, size2);

  // Check that the names are not null and have a non-zero length
  for (size_t i = 0; i < (size2 - 1); i++) {
    ASSERT_NE(names2.get()[i], nullptr);
    ASSERT_GT(strlen(names2.get()[i]), 0);
    // printf("%s\n", names2.get()[i]);
  }
}


TEST(SSLTest, test_SSL_get_all_curve_names) {
  // Get the size by passing a zero size input buffer
  size_t size1 = SSL_get_all_curve_names(nullptr, 0);
  ASSERT_GT(size1, 0);

  // Allocate a buffer of the size returned above
  std::unique_ptr<const char*[]> names1(new const char*[size1]);

  // Call SSL_get_all_curve_names() with the allocated buffer
  size_t size2 = SSL_get_all_curve_names(names1.get(), size1);

  // Check that the size returned is the same as the size we allocated
  ASSERT_EQ(size2, size1);

  // Check that the names are not null and have a non-zero length
  for (size_t i = 0; i < size2; i++) {
    ASSERT_NE(names1.get()[i], nullptr);
    // ASSERT_GT(strlen(names1.get()[i]), 0);
    printf("%s\n", names1.get()[i]);
  }

  // Allocate another buffer that is one element too short (size2 - 1)
  std::unique_ptr<const char*[]> names2(new const char*[size2 - 1]);

  // Call SSL_get_all_curve_names() with the short buffer
  size_t size3 = SSL_get_all_curve_names(names2.get(), size2 - 1);

  // Check that the size returned is the number of ciphers it
  // would have returned if the buffer had been big enough.
  ASSERT_EQ(size3, size2);

  // Check that the names are not null and have a non-zero length
  for (size_t i = 0; i < (size2 - 1); i++) {
    ASSERT_NE(names2.get()[i], nullptr);
    ASSERT_GT(strlen(names2.get()[i]), 0);
    // printf("%s\n", names2.get()[i]);
  }
}
