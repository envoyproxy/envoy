#include "envoy/extensions/transport_sockets/tls/handshakers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/ssl/handshaker.h"

#include "source/common/tls/ssl_handshaker.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/transport_sockets/tls/handshakers/dynamic_modules/config.h"

#include "test/common/tls/ssl_certs_test.h"
#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/network/connection.h"
#include "test/test_common/logging.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace DynamicModules {
namespace {

using ::Envoy::StatusHelpers::HasStatusMessage;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

// A callback shaped like pem_password_cb.
int pemPasswordCallback(char* buf, int buf_size, int, void* u) {
  if (u == nullptr) {
    return 0;
  }
  std::string passphrase = *reinterpret_cast<std::string*>(u);
  RELEASE_ASSERT(buf_size >= static_cast<int>(passphrase.size()), "Passphrase was larger buffer.");
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
  MOCK_METHOD(void, onAsynchronousCertValidationComplete, (), (override));
  MOCK_METHOD(void, onAsynchronousCertificateSelectionComplete, (), (override));
};

class DynamicModuleHandshakerTest : public SslCertsTest {
protected:
  DynamicModuleHandshakerTest()
      : client_ctx_(SSL_CTX_new(TLS_method())), server_ctx_(SSL_CTX_new(TLS_method())) {
    Envoy::Extensions::DynamicModules::DynamicModulesTestEnvironment::setModulesSearchPath();
  }

  void SetUp() override {
    auto key = makeKey();
    auto cert = makeCert();
    auto chain = std::vector<CRYPTO_BUFFER*>{cert.get()};

    server_ssl_ = bssl::UniquePtr<SSL>(SSL_new(server_ctx_.get()));
    SSL_set_accept_state(server_ssl_.get());
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

  bssl::UniquePtr<EVP_PKEY> makeKey() {
    std::string file = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        "{{ test_rundir }}/test/common/tls/test_data/unittest_key.pem"));
    std::string passphrase = "";
    bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(file.data(), file.size()));
    bssl::UniquePtr<EVP_PKEY> key(EVP_PKEY_new());
    RSA* rsa = PEM_read_bio_RSAPrivateKey(bio.get(), nullptr, &pemPasswordCallback, &passphrase);
    RELEASE_ASSERT(rsa != nullptr, "PEM_read_bio_RSAPrivateKey failed.");
    RELEASE_ASSERT(1 == EVP_PKEY_assign_RSA(key.get(), rsa), "EVP_PKEY_assign_RSA failed.");
    return key;
  }

  bssl::UniquePtr<CRYPTO_BUFFER> makeCert() {
    std::string file = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        "{{ test_rundir }}/test/common/tls/test_data/unittest_cert.pem"));
    bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(file.data(), file.size()));
    uint8_t* data = nullptr;
    long len = 0;
    RELEASE_ASSERT(
        PEM_bytes_read_bio(&data, &len, nullptr, PEM_STRING_X509, bio.get(), nullptr, nullptr),
        "PEM_bytes_read_bio failed");
    bssl::UniquePtr<uint8_t> tmp(data);
    return bssl::UniquePtr<CRYPTO_BUFFER>(CRYPTO_BUFFER_new(data, len, nullptr));
  }

  // Loads a module by its C test program name and creates the shared handshaker config.
  absl::StatusOr<DynamicModuleHandshakerConfigSharedPtr>
  createConfig(const std::string& module_name, const std::string& handshaker_name = "test",
               const std::string& handshaker_config = "") {
    auto module =
        Envoy::Extensions::DynamicModules::newDynamicModuleByName(module_name, false, false);
    if (!module.ok()) {
      return module.status();
    }
    return newDynamicModuleHandshakerConfig(handshaker_name, handshaker_config,
                                            std::move(module.value()));
  }

  // Drives the client and server handshake until the server handshaker returns Close.
  Network::PostIoAction driveServerHandshake(DynamicModuleHandshaker& handshaker) {
    auto action = Network::PostIoAction::KeepOpen;
    for (int i = 0; i < 20 && action != Network::PostIoAction::Close; i++) {
      SSL_do_handshake(client_ssl_.get());
      action = handshaker.doHandshake();
    }
    return action;
  }

  const size_t kBufferLength{1024};
  BIO* client_bio_;
  BIO* server_bio_;
  bssl::UniquePtr<SSL_CTX> client_ctx_;
  bssl::UniquePtr<SSL_CTX> server_ctx_;
  bssl::UniquePtr<SSL> client_ssl_;
  bssl::UniquePtr<SSL> server_ssl_;
};

TEST_F(DynamicModuleHandshakerTest, ConfigLoadsWithDefaultCapabilities) {
  auto config = createConfig("tls_handshaker_no_op");
  ASSERT_TRUE(config.ok());
  const auto& capabilities = (*config)->capabilities();
  EXPECT_FALSE(capabilities.provides_certificates);
  EXPECT_FALSE(capabilities.verifies_peer_certificates);
  EXPECT_TRUE(capabilities.is_fips_compliant);
  // The module exports no optional hooks.
  EXPECT_EQ(nullptr, (*config)->on_configure_ssl_context_);
  EXPECT_EQ(nullptr, (*config)->on_new_);
  EXPECT_EQ(nullptr, (*config)->on_destroy_);
  EXPECT_EQ(nullptr, (*config)->on_handshake_);
}

TEST_F(DynamicModuleHandshakerTest, ConfigLoadsWithOptionalHooks) {
  auto config = createConfig("tls_handshaker_custom");
  ASSERT_TRUE(config.ok());
  EXPECT_FALSE((*config)->capabilities().provides_certificates);
  EXPECT_NE(nullptr, (*config)->on_configure_ssl_context_);
  EXPECT_NE(nullptr, (*config)->on_new_);
  EXPECT_NE(nullptr, (*config)->on_destroy_);
  EXPECT_NE(nullptr, (*config)->on_handshake_);
}

TEST_F(DynamicModuleHandshakerTest, ConfigReportsProvidesCertificatesCapability) {
  auto config = createConfig("tls_handshaker_provides_certificates");
  ASSERT_TRUE(config.ok());
  EXPECT_TRUE((*config)->capabilities().provides_certificates);
}

TEST_F(DynamicModuleHandshakerTest, ConfigNewFailureIsAnError) {
  auto config = createConfig("tls_handshaker_config_new_fail");
  EXPECT_THAT(config.status(),
              HasStatusMessage("Failed to initialize dynamic module TLS handshaker config"));
}

TEST_F(DynamicModuleHandshakerTest, NewWithoutDestroyIsAnError) {
  auto config = createConfig("tls_handshaker_new_without_destroy");
  EXPECT_FALSE(config.ok());
  EXPECT_THAT(std::string(config.status().message()), ::testing::HasSubstr("must export both"));
}

TEST_F(DynamicModuleHandshakerTest, MissingModuleIsAnError) {
  auto config = createConfig("does_not_exist");
  EXPECT_FALSE(config.ok());
}

TEST_F(DynamicModuleHandshakerTest, MissingMandatorySymbolIsAnError) {
  // The no_op module implements the HTTP filter ABI, not the handshaker ABI.
  auto config = createConfig("no_op");
  EXPECT_THAT(std::string(config.status().message()),
              ::testing::HasSubstr("envoy_dynamic_module_on_tls_handshaker_config_new"));
}

TEST_F(DynamicModuleHandshakerTest, MissingConfigDestroySymbolIsAnError) {
  auto config = createConfig("tls_handshaker_no_config_destroy");
  EXPECT_FALSE(config.ok());
  EXPECT_THAT(std::string(config.status().message()),
              ::testing::HasSubstr("envoy_dynamic_module_on_tls_handshaker_config_destroy"));
}

TEST_F(DynamicModuleHandshakerTest, DefaultHandshakeCompletes) {
  auto config = createConfig("tls_handshaker_no_op");
  ASSERT_TRUE(config.ok());

  NiceMock<Network::MockConnection> connection;
  ON_CALL(connection, state).WillByDefault(Return(Network::Connection::State::Closed));
  NiceMock<MockHandshakeCallbacks> callbacks;
  ON_CALL(callbacks, connection()).WillByDefault(ReturnRef(connection));
  EXPECT_CALL(callbacks, onSuccess);
  EXPECT_CALL(callbacks, onFailure).Times(0);

  DynamicModuleHandshaker handshaker(*config, std::move(server_ssl_), 0, &callbacks);
  EXPECT_EQ(Network::PostIoAction::Close, driveServerHandshake(handshaker));
  EXPECT_EQ(Ssl::SocketState::HandshakeComplete, handshaker.state());
}

TEST_F(DynamicModuleHandshakerTest, ModuleDrivenHandshakeCompletes) {
  auto config = createConfig("tls_handshaker_custom");
  ASSERT_TRUE(config.ok());

  NiceMock<Network::MockConnection> connection;
  ON_CALL(connection, state).WillByDefault(Return(Network::Connection::State::Closed));
  NiceMock<MockHandshakeCallbacks> callbacks;
  ON_CALL(callbacks, connection()).WillByDefault(ReturnRef(connection));
  EXPECT_CALL(callbacks, onSuccess);
  EXPECT_CALL(callbacks, onFailure).Times(0);

  DynamicModuleHandshaker handshaker(*config, std::move(server_ssl_), 0, &callbacks);
  EXPECT_EQ(Network::PostIoAction::Close, driveServerHandshake(handshaker));
  EXPECT_EQ(Ssl::SocketState::HandshakeComplete, handshaker.state());
}

TEST_F(DynamicModuleHandshakerTest, ModuleDrivenHandshakeWithoutPerConnectionState) {
  auto config = createConfig("tls_handshaker_handshake_only");
  ASSERT_TRUE(config.ok());
  EXPECT_EQ(nullptr, (*config)->on_new_);
  EXPECT_NE(nullptr, (*config)->on_handshake_);

  NiceMock<Network::MockConnection> connection;
  ON_CALL(connection, state).WillByDefault(Return(Network::Connection::State::Closed));
  NiceMock<MockHandshakeCallbacks> callbacks;
  ON_CALL(callbacks, connection()).WillByDefault(ReturnRef(connection));
  EXPECT_CALL(callbacks, onSuccess);
  EXPECT_CALL(callbacks, onFailure).Times(0);

  DynamicModuleHandshaker handshaker(*config, std::move(server_ssl_), 0, &callbacks);
  EXPECT_EQ(Network::PostIoAction::Close, driveServerHandshake(handshaker));
  EXPECT_EQ(Ssl::SocketState::HandshakeComplete, handshaker.state());
}

TEST_F(DynamicModuleHandshakerTest, ModuleDrivenHandshakeReportsOutcomes) {
  auto config = createConfig("tls_handshaker_manual");
  ASSERT_TRUE(config.ok());

  NiceMock<Network::MockConnection> connection;
  ON_CALL(connection, state).WillByDefault(Return(Network::Connection::State::Open));
  NiceMock<MockHandshakeCallbacks> callbacks;
  ON_CALL(callbacks, connection()).WillByDefault(ReturnRef(connection));
  EXPECT_CALL(callbacks, onSuccess);
  EXPECT_CALL(callbacks, onFailure).Times(0);

  DynamicModuleHandshaker handshaker(*config, std::move(server_ssl_), 0, &callbacks);
  // The module reports WaitForData, then Pending, then Complete, and Envoy applies each transition.
  EXPECT_EQ(Network::PostIoAction::KeepOpen, handshaker.doHandshake());
  EXPECT_EQ(Ssl::SocketState::HandshakeWaitingForConnectionData, handshaker.state());
  EXPECT_EQ(Network::PostIoAction::KeepOpen, handshaker.doHandshake());
  EXPECT_EQ(Ssl::SocketState::HandshakeBlockedOnAsyncOperation, handshaker.state());
  EXPECT_EQ(Network::PostIoAction::KeepOpen, handshaker.doHandshake());
  EXPECT_EQ(Ssl::SocketState::HandshakeComplete, handshaker.state());
}

TEST_F(DynamicModuleHandshakerTest, ModuleDrivenHandshakeReportsFailure) {
  auto config = createConfig("tls_handshaker_manual_fail");
  ASSERT_TRUE(config.ok());

  NiceMock<Network::MockConnection> connection;
  NiceMock<MockHandshakeCallbacks> callbacks;
  ON_CALL(callbacks, connection()).WillByDefault(ReturnRef(connection));
  EXPECT_CALL(callbacks, onFailure);
  EXPECT_CALL(callbacks, onSuccess).Times(0);

  DynamicModuleHandshaker handshaker(*config, std::move(server_ssl_), 0, &callbacks);
  EXPECT_EQ(Network::PostIoAction::Close, handshaker.doHandshake());
}

TEST_F(DynamicModuleHandshakerTest, ModuleDrivenHandshakeCompleteClosesWhenConnectionClosed) {
  auto config = createConfig("tls_handshaker_manual");
  ASSERT_TRUE(config.ok());

  NiceMock<Network::MockConnection> connection;
  ON_CALL(connection, state).WillByDefault(Return(Network::Connection::State::Closed));
  NiceMock<MockHandshakeCallbacks> callbacks;
  ON_CALL(callbacks, connection()).WillByDefault(ReturnRef(connection));
  EXPECT_CALL(callbacks, onSuccess);

  DynamicModuleHandshaker handshaker(*config, std::move(server_ssl_), 0, &callbacks);
  handshaker.doHandshake();
  handshaker.doHandshake();
  // The handshake completed but the connection closed during onSuccess, so close.
  EXPECT_EQ(Network::PostIoAction::Close, handshaker.doHandshake());
  EXPECT_EQ(Ssl::SocketState::HandshakeComplete, handshaker.state());
}

TEST_F(DynamicModuleHandshakerTest, UnknownResultClosesConnection) {
  auto config = createConfig("tls_handshaker_bad_result");
  ASSERT_TRUE(config.ok());

  NiceMock<Network::MockConnection> connection;
  NiceMock<MockHandshakeCallbacks> callbacks;
  ON_CALL(callbacks, connection()).WillByDefault(ReturnRef(connection));
  EXPECT_CALL(callbacks, onSuccess).Times(0);
  EXPECT_CALL(callbacks, onFailure).Times(0);

  DynamicModuleHandshaker handshaker(*config, std::move(server_ssl_), 0, &callbacks);
  EXPECT_EQ(Network::PostIoAction::Close, handshaker.doHandshake());
}

TEST_F(DynamicModuleHandshakerTest, HandshakerCreationFailureClosesConnection) {
  auto config = createConfig("tls_handshaker_new_fail");
  ASSERT_TRUE(config.ok());

  NiceMock<Network::MockConnection> connection;
  NiceMock<MockHandshakeCallbacks> callbacks;
  ON_CALL(callbacks, connection()).WillByDefault(ReturnRef(connection));
  EXPECT_CALL(callbacks, onSuccess).Times(0);
  EXPECT_CALL(callbacks, onFailure).Times(0);

  EXPECT_LOG_CONTAINS("error", "failed to create TLS handshaker", {
    DynamicModuleHandshaker handshaker(*config, std::move(server_ssl_), 0, &callbacks);
    EXPECT_EQ(Network::PostIoAction::Close, handshaker.doHandshake());
  });
}

TEST_F(DynamicModuleHandshakerTest, NullConfigClosesConnection) {
  NiceMock<MockHandshakeCallbacks> callbacks;
  EXPECT_CALL(callbacks, onSuccess).Times(0);
  EXPECT_CALL(callbacks, onFailure).Times(0);
  DynamicModuleHandshaker handshaker(nullptr, std::move(server_ssl_), 0, &callbacks);
  EXPECT_EQ(Network::PostIoAction::Close, handshaker.doHandshake());
}

class DynamicModuleHandshakerFactoryTest : public SslCertsTest {
protected:
  DynamicModuleHandshakerFactoryTest()
      : handshaker_context_(factory_context_.server_context_.api(),
                            factory_context_.server_context_.options(), "",
                            factory_context_.server_context_.singletonManager(),
                            factory_context_.server_context_.lifecycleNotifier()) {
    Envoy::Extensions::DynamicModules::DynamicModulesTestEnvironment::setModulesSearchPath();
  }

  Protobuf::Any buildConfig(const std::string& module_name, bool with_handshaker_config = false) {
    envoy::extensions::transport_sockets::tls::handshakers::dynamic_modules::v3::
        DynamicModuleTlsHandshaker proto_config;
    proto_config.mutable_dynamic_module_config()->set_name(module_name);
    proto_config.set_handshaker_name("test");
    if (with_handshaker_config) {
      // The content is opaque to the module, so any message serves as the config bytes.
      envoy::extensions::transport_sockets::tls::handshakers::dynamic_modules::v3::
          DynamicModuleTlsHandshaker inner;
      inner.set_handshaker_name("inner");
      EXPECT_TRUE(proto_config.mutable_handshaker_config()->PackFrom(inner));
    }
    Protobuf::Any any;
    EXPECT_TRUE(any.PackFrom(proto_config));
    return any;
  }

  DynamicModuleHandshakerFactory factory_;
  HandshakerFactoryContextImpl handshaker_context_;
};

TEST_F(DynamicModuleHandshakerFactoryTest, NameAndEmptyConfigProto) {
  EXPECT_EQ("envoy.tls.handshakers.dynamic_modules", factory_.name());
  EXPECT_NE(nullptr, factory_.createEmptyConfigProto());
}

TEST_F(DynamicModuleHandshakerFactoryTest, CreateHandshakerCbSetsSslCtxCb) {
  Protobuf::Any config = buildConfig("tls_handshaker_custom");
  auto cb = factory_.createHandshakerCb(config, handshaker_context_,
                                        ProtobufMessage::getStrictValidationVisitor());
  EXPECT_NE(nullptr, cb);

  // The custom module exports configure_ssl_context, so the `sslctxCb` is set and runs
  // without error.
  auto sslctx_cb = factory_.sslctxCb(handshaker_context_);
  ASSERT_NE(nullptr, sslctx_cb);
  bssl::UniquePtr<SSL_CTX> ssl_ctx(SSL_CTX_new(TLS_method()));
  sslctx_cb(ssl_ctx.get());
}

TEST_F(DynamicModuleHandshakerFactoryTest, CreateHandshakerCbPopulatesCapabilities) {
  Protobuf::Any config = buildConfig("tls_handshaker_provides_certificates");
  auto cb = factory_.createHandshakerCb(config, handshaker_context_,
                                        ProtobufMessage::getStrictValidationVisitor());
  EXPECT_NE(nullptr, cb);
  EXPECT_TRUE(factory_.capabilities().provides_certificates);
}

TEST_F(DynamicModuleHandshakerFactoryTest, NoOpModuleHasNoSslCtxCb) {
  Protobuf::Any config = buildConfig("tls_handshaker_no_op");
  auto cb = factory_.createHandshakerCb(config, handshaker_context_,
                                        ProtobufMessage::getStrictValidationVisitor());
  EXPECT_NE(nullptr, cb);
  EXPECT_FALSE(factory_.capabilities().provides_certificates);
  EXPECT_EQ(nullptr, factory_.sslctxCb(handshaker_context_));
}

TEST_F(DynamicModuleHandshakerFactoryTest, SslCtxCbLogsOnConfigureFailure) {
  Protobuf::Any config = buildConfig("tls_handshaker_configure_fail");
  auto cb = factory_.createHandshakerCb(config, handshaker_context_,
                                        ProtobufMessage::getStrictValidationVisitor());
  EXPECT_NE(nullptr, cb);
  auto sslctx_cb = factory_.sslctxCb(handshaker_context_);
  ASSERT_NE(nullptr, sslctx_cb);
  bssl::UniquePtr<SSL_CTX> ssl_ctx(SSL_CTX_new(TLS_method()));
  EXPECT_LOG_CONTAINS("warn", "failed to configure SSL_CTX", { sslctx_cb(ssl_ctx.get()); });
}

TEST_F(DynamicModuleHandshakerFactoryTest, CreateHandshakerCbAcceptsHandshakerConfig) {
  Protobuf::Any config = buildConfig("tls_handshaker_no_op", /*with_handshaker_config=*/true);
  auto cb = factory_.createHandshakerCb(config, handshaker_context_,
                                        ProtobufMessage::getStrictValidationVisitor());
  EXPECT_NE(nullptr, cb);
}

TEST_F(DynamicModuleHandshakerFactoryTest, CreateHandshakerCbDefersMalformedConfig) {
  Protobuf::Any malformed;
  malformed.set_type_url("type.googleapis.com/bogus.Type");
  malformed.set_value("x");
  Ssl::HandshakerFactoryCb cb;
  EXPECT_LOG_CONTAINS("error", "failed to load dynamic module TLS handshaker", {
    cb = factory_.createHandshakerCb(malformed, handshaker_context_,
                                     ProtobufMessage::getStrictValidationVisitor());
  });
  EXPECT_NE(nullptr, cb);
}

TEST_F(DynamicModuleHandshakerFactoryTest, CreateHandshakerCbDefersUnexpectedConfigType) {
  // The config message is not a google.protobuf.Any, so the load is deferred.
  envoy::extensions::transport_sockets::tls::handshakers::dynamic_modules::v3::
      DynamicModuleTlsHandshaker not_an_any;
  not_an_any.set_handshaker_name("test");
  Ssl::HandshakerFactoryCb cb;
  EXPECT_LOG_CONTAINS("error", "failed to load dynamic module TLS handshaker", {
    cb = factory_.createHandshakerCb(not_an_any, handshaker_context_,
                                     ProtobufMessage::getStrictValidationVisitor());
  });
  EXPECT_NE(nullptr, cb);
}

TEST_F(DynamicModuleHandshakerFactoryTest, CreateHandshakerCbDefersModuleLoadFailure) {
  Protobuf::Any config = buildConfig("does_not_exist");
  Ssl::HandshakerFactoryCb cb;
  EXPECT_LOG_CONTAINS("error", "failed to load dynamic module TLS handshaker", {
    cb = factory_.createHandshakerCb(config, handshaker_context_,
                                     ProtobufMessage::getStrictValidationVisitor());
  });
  // The factory still returns a callback and reports the default capabilities, and the handshaker
  // closes the connection.
  EXPECT_NE(nullptr, cb);
  EXPECT_FALSE(factory_.capabilities().provides_certificates);
  EXPECT_EQ(nullptr, factory_.sslctxCb(handshaker_context_));
}

} // namespace
} // namespace DynamicModules
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
