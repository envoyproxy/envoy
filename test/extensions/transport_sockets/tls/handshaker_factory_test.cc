#include <openssl/ssl3.h>

#include "envoy/network/transport_socket.h"
#include "envoy/ssl/handshaker.h"

#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/transport_sockets/tls/context_config_impl.h"
#include "source/extensions/transport_sockets/tls/context_manager_impl.h"
#include "source/extensions/transport_sockets/tls/ssl_handshaker.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"
#include "source/server/process_context_impl.h"

#include "test/mocks/network/connection.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"

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

using ::testing::MockFunction;
using ::testing::Ref;
using ::testing::Return;
using ::testing::WithArg;

// Test-only custom process object which accepts an `SslCtxCb` for in-test SSL_CTX
// manipulation.
class CustomProcessObjectForTest : public ProcessObject {
public:
  CustomProcessObjectForTest(Ssl::SslCtxCb cb) : cb_(cb) {}

  Ssl::SslCtxCb getSslCtxCb() { return cb_; }

  static CustomProcessObjectForTest* get(const ProcessContextOptRef& process_context_opt_ref) {
    auto& process_context = process_context_opt_ref.value().get();
    auto& process_object = dynamic_cast<CustomProcessObjectForTest&>(process_context.get());
    return &process_object;
  }

private:
  Ssl::SslCtxCb cb_;
};

// Example SslHandshakerFactoryImpl demonstrating special-case behavior; in this
// case, using a process context to modify the SSL_CTX.
class HandshakerFactoryImplForTest
    : public Extensions::TransportSockets::Tls::HandshakerFactoryImpl {
public:
  using CreateHandshakerHook =
      std::function<void(const Protobuf::Message&, Ssl::HandshakerFactoryContext&,
                         ProtobufMessage::ValidationVisitor&)>;

  static constexpr char kFactoryName[] = "envoy.testonly_handshaker";

  std::string name() const override { return kFactoryName; }

  Ssl::HandshakerFactoryCb
  createHandshakerCb(const Protobuf::Message& message, Ssl::HandshakerFactoryContext& context,
                     ProtobufMessage::ValidationVisitor& validation_visitor) override {
    if (handshaker_cb_) {
      handshaker_cb_(message, context, validation_visitor);
    }

    // The default HandshakerImpl doesn't take a config or use the HandshakerFactoryContext.
    return [](bssl::UniquePtr<SSL> ssl, int ssl_extended_socket_info_index,
              Ssl::HandshakeCallbacks* handshake_callbacks) {
      return std::make_shared<SslHandshakerImpl>(std::move(ssl), ssl_extended_socket_info_index,
                                                 handshake_callbacks);
    };
  }

  Ssl::SslCtxCb sslctxCb(Ssl::HandshakerFactoryContext& handshaker_factory_context) const override {
    // Get process object, cast to custom process object, and return custom
    // callback.
    return CustomProcessObjectForTest::get(handshaker_factory_context.api().processContext())
        ->getSslCtxCb();
  }

  CreateHandshakerHook handshaker_cb_;
};

class HandshakerFactoryTest : public testing::Test {
protected:
  HandshakerFactoryTest()
      : context_manager_(
            std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(time_system_)),
        registered_factory_(handshaker_factory_) {
    scoped_runtime_.mergeValues(
        {{"envoy.reloadable_features.no_extension_lookup_by_name", "false"}});
    // UpstreamTlsContext proto expects to use the newly-registered handshaker.
    envoy::config::core::v3::TypedExtensionConfig* custom_handshaker =
        tls_context_.mutable_common_tls_context()->mutable_custom_handshaker();
    custom_handshaker->set_name(HandshakerFactoryImplForTest::kFactoryName);
  }

  // Helper for downcasting a socket to a test socket so we can examine its
  // SSL_CTX.
  SSL_CTX* extractSslCtx(Network::TransportSocket* socket) {
    SslSocket* ssl_socket = dynamic_cast<SslSocket*>(socket);
    SSL* ssl = ssl_socket->rawSslForTest();
    return SSL_get_SSL_CTX(ssl);
  }

  Event::GlobalTimeSystem time_system_;
  Stats::IsolatedStoreImpl stats_store_;
  std::unique_ptr<Extensions::TransportSockets::Tls::ContextManagerImpl> context_manager_;
  HandshakerFactoryImplForTest handshaker_factory_;
  Registry::InjectFactory<Ssl::HandshakerFactory> registered_factory_;
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context_;
  TestScopedRuntime scoped_runtime_;
};

TEST_F(HandshakerFactoryTest, SetMockFunctionCb) {
  MockFunction<void(SSL_CTX*)> cb;
  EXPECT_CALL(cb, Call);

  CustomProcessObjectForTest custom_process_object_for_test(cb.AsStdFunction());
  auto process_context_impl = std::make_unique<Envoy::ProcessContextImpl>(
      static_cast<Envoy::ProcessObject&>(custom_process_object_for_test));

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
  EXPECT_CALL(mock_factory_ctx.api_, processContext())
      .WillRepeatedly(Return(std::reference_wrapper<Envoy::ProcessContext>(*process_context_impl)));

  Extensions::TransportSockets::Tls::ClientSslSocketFactory socket_factory(
      /*config=*/
      std::make_unique<Extensions::TransportSockets::Tls::ClientContextConfigImpl>(
          tls_context_, "", mock_factory_ctx),
      *context_manager_, stats_store_);

  std::unique_ptr<Network::TransportSocket> socket =
      socket_factory.createTransportSocket(nullptr, nullptr);

  SSL_CTX* ssl_ctx = extractSslCtx(socket.get());

  // Compare to the next test, where our custom `sslctxcb` reaches in and sets
  // this option.
  EXPECT_FALSE(SSL_CTX_get_options(ssl_ctx) & SSL_OP_NO_TLSv1);
}

TEST_F(HandshakerFactoryTest, SetSpecificSslCtxOption) {
  CustomProcessObjectForTest custom_process_object_for_test(
      /*cb=*/[](SSL_CTX* ssl_ctx) { SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_TLSv1); });
  auto process_context_impl = std::make_unique<Envoy::ProcessContextImpl>(
      static_cast<Envoy::ProcessObject&>(custom_process_object_for_test));

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
  EXPECT_CALL(mock_factory_ctx.api_, processContext())
      .WillRepeatedly(Return(std::reference_wrapper<Envoy::ProcessContext>(*process_context_impl)));

  Extensions::TransportSockets::Tls::ClientSslSocketFactory socket_factory(
      /*config=*/
      std::make_unique<Extensions::TransportSockets::Tls::ClientContextConfigImpl>(
          tls_context_, "", mock_factory_ctx),
      *context_manager_, stats_store_);

  std::unique_ptr<Network::TransportSocket> socket =
      socket_factory.createTransportSocket(nullptr, nullptr);

  SSL_CTX* ssl_ctx = extractSslCtx(socket.get());

  // Compare to the previous test, where our mock `sslctxcb` is called, but does
  // not set this option.
  EXPECT_TRUE(SSL_CTX_get_options(ssl_ctx) & SSL_OP_NO_TLSv1);
}

TEST_F(HandshakerFactoryTest, HandshakerContextProvidesObjectsFromParentContext) {
  CustomProcessObjectForTest custom_process_object_for_test(
      /*cb=*/[](SSL_CTX* ssl_ctx) { SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_TLSv1); });
  auto process_context_impl = std::make_unique<Envoy::ProcessContextImpl>(
      static_cast<Envoy::ProcessObject&>(custom_process_object_for_test));

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> mock_factory_ctx;
  EXPECT_CALL(mock_factory_ctx.api_, processContext())
      .WillRepeatedly(Return(std::reference_wrapper<Envoy::ProcessContext>(*process_context_impl)));

  MockFunction<HandshakerFactoryImplForTest::CreateHandshakerHook> mock_factory_cb;
  handshaker_factory_.handshaker_cb_ = mock_factory_cb.AsStdFunction();

  EXPECT_CALL(mock_factory_cb, Call)
      .WillOnce(WithArg<1>([&](Ssl::HandshakerFactoryContext& context) {
        // Check that the objects available via the context are the same ones
        // provided to the parent context.
        EXPECT_THAT(context.api(), Ref(mock_factory_ctx.api_));
        EXPECT_THAT(context.options(), Ref(mock_factory_ctx.options_));
      }));

  Extensions::TransportSockets::Tls::ClientSslSocketFactory socket_factory(
      /*config=*/
      std::make_unique<Extensions::TransportSockets::Tls::ClientContextConfigImpl>(
          tls_context_, "", mock_factory_ctx),
      *context_manager_, stats_store_);

  std::unique_ptr<Network::TransportSocket> socket =
      socket_factory.createTransportSocket(nullptr, nullptr);
}

} // namespace
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
