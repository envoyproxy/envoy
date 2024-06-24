#pragma once

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/private_key/private_key_config.h"

#include "source/common/common/logger.h"

#include "contrib/envoy/extensions/private_key_providers/qat/v3alpha/qat.pb.h"
#include "contrib/qat/private_key_providers/source/libqat.h"
#include "contrib/qat/private_key_providers/source/qat.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Qat {

class QatPrivateKeyConnection {
public:
  QatPrivateKeyConnection(Ssl::PrivateKeyConnectionCallbacks& cb, Event::Dispatcher& dispatcher,
                          QatHandle& handle, bssl::UniquePtr<EVP_PKEY> pkey);

  void registerCallback(QatContext* ctx);
  void unregisterCallback();
  QatHandle& getHandle() { return handle_; };
  EVP_PKEY* getPrivateKey() { return pkey_.get(); };

private:
  Ssl::PrivateKeyConnectionCallbacks& cb_;
  Event::Dispatcher& dispatcher_;
  Event::FileEventPtr ssl_async_event_{};
  QatHandle& handle_;
  bssl::UniquePtr<EVP_PKEY> pkey_;
};

class QatPrivateKeyMethodProvider : public virtual Ssl::PrivateKeyMethodProvider,
                                    public Logger::Loggable<Logger::Id::connection> {
public:
  QatPrivateKeyMethodProvider(
      const envoy::extensions::private_key_providers::qat::v3alpha::QatPrivateKeyMethodConfig&
          config,
      Server::Configuration::TransportSocketFactoryContext& private_key_provider_context,
      LibQatCryptoSharedPtr libqat);
  // Ssl::PrivateKeyMethodProvider
  void registerPrivateKeyMethod(SSL* ssl, Ssl::PrivateKeyConnectionCallbacks& cb,
                                Event::Dispatcher& dispatcher) override;
  void unregisterPrivateKeyMethod(SSL* ssl) override;
  bool checkFips() override;
  bool isAvailable() override;
  Ssl::BoringSslPrivateKeyMethodSharedPtr getBoringSslPrivateKeyMethod() override;

private:
  Ssl::BoringSslPrivateKeyMethodSharedPtr method_{};
  std::shared_ptr<QatManager> manager_;
  std::shared_ptr<QatSection> section_;
  Api::Api& api_;
  bssl::UniquePtr<EVP_PKEY> pkey_;
  LibQatCryptoSharedPtr libqat_{};
  bool initialized_{};
};

} // namespace Qat
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
