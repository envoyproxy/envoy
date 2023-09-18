#pragma once

#include <functional>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/ssl/private_key/private_key_callbacks.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Server {
namespace Configuration {
// Prevent a dependency loop with the forward declaration.
class TransportSocketFactoryContext;
} // namespace Configuration
} // namespace Server

namespace Ssl {

#ifdef OPENSSL_IS_BORINGSSL
using BoringSslPrivateKeyMethodSharedPtr = std::shared_ptr<SSL_PRIVATE_KEY_METHOD>;
#endif

class PrivateKeyMethodProvider {
public:
  virtual ~PrivateKeyMethodProvider() = default;

  /**
   * Register an SSL connection to private key operations by the provider.
   * @param ssl a SSL connection object.
   * @param cb a callbacks object, whose "complete" method will be invoked
   * when the asynchronous processing is complete.
   * @param dispatcher supplies the owning thread's dispatcher.
   */
  virtual void registerPrivateKeyMethod(SSL* ssl, PrivateKeyConnectionCallbacks& cb,
                                        Event::Dispatcher& dispatcher) PURE;

  /**
   * Unregister an SSL connection from private key operations by the provider.
   * @param ssl a SSL connection object.
   * @throw EnvoyException if registration fails.
   */
  virtual void unregisterPrivateKeyMethod(SSL* ssl) PURE;

  /**
   * Check whether the private key method satisfies FIPS requirements.
   * @return true if FIPS key requirements are satisfied, false if not.
   */
  virtual bool checkFips() PURE;

  /**
   * Check whether the private key method is available.
   * @return true if the private key method is available, false if not.
   */
  virtual bool isAvailable() PURE;

#ifdef OPENSSL_IS_BORINGSSL
  /**
   * Get the private key methods from the provider.
   * @return the private key methods associated with this provider and
   * configuration.
   */
  virtual BoringSslPrivateKeyMethodSharedPtr getBoringSslPrivateKeyMethod() PURE;
#endif
};

using PrivateKeyMethodProviderSharedPtr = std::shared_ptr<PrivateKeyMethodProvider>;

/**
 * A manager for finding correct user-provided functions for handling BoringSSL private key
 * operations.
 */
class PrivateKeyMethodManager {
public:
  virtual ~PrivateKeyMethodManager() = default;

  /**
   * Finds and returns a private key operations provider for BoringSSL.
   *
   * @param config a protobuf message object containing a PrivateKeyProvider message.
   * @param factory_context context that provides components for creating and
   * initializing connections using asynchronous private key operations.
   * @return PrivateKeyMethodProvider the private key operations provider, or nullptr if
   * no provider can be used with the context configuration.
   */
  virtual PrivateKeyMethodProviderSharedPtr createPrivateKeyMethodProvider(
      const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& config,
      Envoy::Server::Configuration::TransportSocketFactoryContext& factory_context) PURE;
};

} // namespace Ssl
} // namespace Envoy
