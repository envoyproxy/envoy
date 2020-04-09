#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/private_key/private_key_config.h"

#include "common/config/utility.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {

struct TestPrivateKeyConnectionTestOptions {
  // Return private key method value directly without asynchronous operation.
  bool sync_mode_{};

  // The "decrypt" private key method is expected to he called.
  bool decrypt_expected_{};

  // The "sign" private key method is expected to he called.
  bool sign_expected_{};

  // Add a cryptographic error (invalid signature, incorrect decryption).
  bool crypto_error_{};

  // Return an error from the private key method.
  bool method_error_{};

  // Return an error from the private key method completion function.
  bool async_method_error_{};
};

// An example private key method provider for testing the decrypt() and sign()
// functionality.
class TestPrivateKeyConnection {
public:
  TestPrivateKeyConnection(Ssl::PrivateKeyConnectionCallbacks& cb, Event::Dispatcher& dispatcher,
                           bssl::UniquePtr<EVP_PKEY> pkey,
                           TestPrivateKeyConnectionTestOptions& test_options);
  EVP_PKEY* getPrivateKey() { return pkey_.get(); }
  void delayed_op();
  // Store the output data temporarily.
  std::vector<uint8_t> output_;
  // The complete callback can return other value than "retry" only after
  // onPrivateKeyMethodComplete() function has been called. This is controlled by "finished"
  // variable.
  bool finished_{};
  TestPrivateKeyConnectionTestOptions& test_options_;

private:
  Ssl::PrivateKeyConnectionCallbacks& cb_;
  Event::Dispatcher& dispatcher_;
  bssl::UniquePtr<EVP_PKEY> pkey_;
  // A zero-length timer controls the callback.
  Event::TimerPtr timer_;
};

class TestPrivateKeyMethodProvider : public virtual Ssl::PrivateKeyMethodProvider {
public:
  TestPrivateKeyMethodProvider(
      const ProtobufWkt::Any& typed_config,
      Server::Configuration::TransportSocketFactoryContext& factory_context);
  // Ssl::PrivateKeyMethodProvider
  void registerPrivateKeyMethod(SSL* ssl, Ssl::PrivateKeyConnectionCallbacks& cb,
                                Event::Dispatcher& dispatcher) override;
  void unregisterPrivateKeyMethod(SSL* ssl) override;
  bool checkFips() override;
  Ssl::BoringSslPrivateKeyMethodSharedPtr getBoringSslPrivateKeyMethod() override;

  static int rsaConnectionIndex();
  static int ecdsaConnectionIndex();

private:
  Ssl::BoringSslPrivateKeyMethodSharedPtr method_{};
  bssl::UniquePtr<EVP_PKEY> pkey_;
  TestPrivateKeyConnectionTestOptions test_options_;
  std::string mode_;
};

class TestPrivateKeyMethodFactory : public Ssl::PrivateKeyMethodProviderInstanceFactory {
public:
  // Ssl::PrivateKeyMethodProviderInstanceFactory
  Ssl::PrivateKeyMethodProviderSharedPtr createPrivateKeyMethodProviderInstance(
      const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& config,
      Server::Configuration::TransportSocketFactoryContext& factory_context) override {
    return std::make_shared<TestPrivateKeyMethodProvider>(config.typed_config(), factory_context);
  }

  std::string name() const override { return "test"; };
};

} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
