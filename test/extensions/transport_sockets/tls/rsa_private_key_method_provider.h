#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/private_key/private_key_config.h"

#include "common/config/utility.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {

struct RsaPrivateKeyConnectionTestOptions {
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

// An example RSA private key method provider here for testing the decrypt() and sign()
// functionality.
class RsaPrivateKeyConnection : public virtual Ssl::PrivateKeyConnection {
public:
  RsaPrivateKeyConnection(SSL* ssl, Ssl::PrivateKeyConnectionCallbacks& cb,
                          Event::Dispatcher& dispatcher, bssl::UniquePtr<EVP_PKEY> pkey,
                          RsaPrivateKeyConnectionTestOptions& test_options);
  RSA* getPrivateKey() { return EVP_PKEY_get0_RSA(pkey_.get()); }
  void delayed_op();

  // Store the output data temporarily.
  std::vector<uint8_t> output_;

  // Is the operation finished?
  bool finished_{};
  RsaPrivateKeyConnectionTestOptions& test_options_;

private:
  Ssl::PrivateKeyConnectionCallbacks& cb_;
  Event::Dispatcher& dispatcher_;
  bssl::UniquePtr<EVP_PKEY> pkey_;
  Event::TimerPtr timer_;
};

class RsaPrivateKeyMethodProvider : public virtual Ssl::PrivateKeyMethodProvider {
public:
  RsaPrivateKeyMethodProvider(
      const ProtobufWkt::Struct& config,
      Server::Configuration::TransportSocketFactoryContext& factory_context);
  // Ssl::PrivateKeyMethodProvider
  Ssl::PrivateKeyConnectionPtr getPrivateKeyConnection(SSL* ssl,
                                                       Ssl::PrivateKeyConnectionCallbacks& cb,
                                                       Event::Dispatcher& dispatcher) override;
  bool checkFips() override;
  Ssl::BoringSslPrivateKeyMethodSharedPtr getBoringSslPrivateKeyMethod() override;

  static int ssl_rsa_connection_index;

private:
  Ssl::BoringSslPrivateKeyMethodSharedPtr method_{};
  bssl::UniquePtr<EVP_PKEY> pkey_;
  RsaPrivateKeyConnectionTestOptions test_options_;
};

class RsaPrivateKeyMethodFactory : public Ssl::PrivateKeyMethodProviderInstanceFactory {
public:
  // Ssl::PrivateKeyMethodProviderInstanceFactory
  Ssl::PrivateKeyMethodProviderSharedPtr
  createPrivateKeyMethodProviderInstance(const envoy::api::v2::auth::PrivateKeyMethod& message,
                                         Server::Configuration::TransportSocketFactoryContext&
                                             private_key_method_provider_context) override {
    return std::make_shared<RsaPrivateKeyMethodProvider>(message.config(),
                                                         private_key_method_provider_context);
  }

  std::string name() const override { return std::string("rsa_test"); };
};

} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
