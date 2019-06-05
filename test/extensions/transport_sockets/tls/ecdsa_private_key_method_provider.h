#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/private_key/private_key_config.h"

#include "common/common/lock_guard.h"
#include "common/common/thread.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {

struct EcdsaPrivateKeyConnectionTestOptions {
  // Return an error from the private key method completion function.
  bool async_method_error_{};
};

// An example ECDSA private key method provider for testing the decrypt() and sign()
// functionality.
class EcdsaPrivateKeyConnection {
public:
  EcdsaPrivateKeyConnection(SSL* ssl, Ssl::PrivateKeyConnectionCallbacks& cb,
                            Event::Dispatcher& dispatcher, bssl::UniquePtr<EVP_PKEY> pkey,
                            EcdsaPrivateKeyConnectionTestOptions& test_options);
  EC_KEY* getPrivateKey() { return EVP_PKEY_get1_EC_KEY(pkey_.get()); }
  void delayed_op();

  // Store the output data temporarily.
  std::vector<uint8_t> output_;
  // Is the operation finished?
  bool finished_{};
  EcdsaPrivateKeyConnectionTestOptions& test_options_;

private:
  Ssl::PrivateKeyConnectionCallbacks& cb_;
  Event::Dispatcher& dispatcher_;
  bssl::UniquePtr<EVP_PKEY> pkey_;
  Event::TimerPtr timer_;
};

class EcdsaPrivateKeyMethodProvider : public virtual Ssl::PrivateKeyMethodProvider {
public:
  EcdsaPrivateKeyMethodProvider(
      const ProtobufWkt::Struct& config,
      Server::Configuration::TransportSocketFactoryContext& factory_context);
  // Ssl::PrivateKeyMethodProvider
  void registerPrivateKeyMethod(SSL* ssl, Ssl::PrivateKeyConnectionCallbacks& cb,
                                Event::Dispatcher& dispatcher) override;
  void unregisterPrivateKeyMethod(SSL* ssl) override;
  bool checkFips() override;
  Ssl::BoringSslPrivateKeyMethodSharedPtr getBoringSslPrivateKeyMethod() override;

  static int ssl_ecdsa_connection_index;

private:
  Thread::MutexBasicLockable map_lock_{};
  Ssl::BoringSslPrivateKeyMethodSharedPtr method_{};
  bssl::UniquePtr<EVP_PKEY> pkey_;
  EcdsaPrivateKeyConnectionTestOptions test_options_;
  std::map<SSL*, std::unique_ptr<EcdsaPrivateKeyConnection>> connections_;
};

class EcdsaPrivateKeyMethodFactory : public Ssl::PrivateKeyMethodProviderInstanceFactory {
public:
  // Ssl::PrivateKeyMethodProviderInstanceFactory
  Ssl::PrivateKeyMethodProviderSharedPtr
  createPrivateKeyMethodProviderInstance(const envoy::api::v2::auth::PrivateKeyMethod& message,
                                         Server::Configuration::TransportSocketFactoryContext&
                                             private_key_method_provider_context) override {
    return std::make_shared<EcdsaPrivateKeyMethodProvider>(message.config(),
                                                           private_key_method_provider_context);
  }

  std::string name() const override { return std::string("ecdsa_test"); };
};

} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
