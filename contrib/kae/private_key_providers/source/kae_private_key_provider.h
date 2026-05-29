#pragma once

#include <openssl/ossl_typ.h>

#include <memory>

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/private_key/private_key_config.h"

#include "source/common/common/logger.h"

#include "contrib/envoy/extensions/private_key_providers/kae/v3alpha/kae.pb.h"
#include "contrib/kae/private_key_providers/source/libuadk.h"
#include "kae.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Kae {

class KaePrivateKeyConnection {
public:
  KaePrivateKeyConnection(Ssl::PrivateKeyConnectionCallbacks& cb, Event::Dispatcher& dispatcher,
                          KaeHandle& handle, bssl::UniquePtr<EVP_PKEY> pkey);
  void registerCallback(KaeContext* ctx);
  void unregisterCallback();
  KaeHandle& getHandle() { return handle_; }
  EVP_PKEY* getPrivateKey() { return pkey_.get(); }

private:
  Ssl::PrivateKeyConnectionCallbacks& cb_;
  Event::Dispatcher& dispatcher_;
  Event::FileEventPtr ssl_async_event_;
  KaeHandle& handle_;
  bssl::UniquePtr<EVP_PKEY> pkey_;
};

class KaePrivateKeyMethodProvider : public virtual Ssl::PrivateKeyMethodProvider,
                                    public Logger::Loggable<Logger::Id::connection> {
public:
  KaePrivateKeyMethodProvider(
      const envoy::extensions::private_key_providers::kae::v3alpha::KaePrivateKeyMethodConfig&
          config,
      Server::Configuration::TransportSocketFactoryContext& private_key_provider_context,
      LibUadkCryptoSharedPtr libuadk);

  void registerPrivateKeyMethod(SSL* ssl, Ssl::PrivateKeyConnectionCallbacks& cb,
                                Event::Dispatcher& dispatcher) override;
  void unregisterPrivateKeyMethod(SSL* ssl) override;
  bool checkFips() override;
  bool isAvailable() override;
  Ssl::BoringSslPrivateKeyMethodSharedPtr getBoringSslPrivateKeyMethod() override;

private:
  Ssl::BoringSslPrivateKeyMethodSharedPtr method_;
  std::shared_ptr<KaeManager> manager_;
  std::shared_ptr<KaeSection> section_;
  Api::Api& api_;
  bssl::UniquePtr<EVP_PKEY> pkey_;
  LibUadkCryptoSharedPtr libuadk_;
  bool initialized_{};
};

} // namespace Kae
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
