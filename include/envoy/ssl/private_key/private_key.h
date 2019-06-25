#pragma once

#include <functional>
#include <string>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"
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

typedef std::shared_ptr<SSL_PRIVATE_KEY_METHOD> BoringSslPrivateKeyMethodSharedPtr;

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
   * Get the private key methods from the provider.
   * @return the private key methods associated with this provider and
   * configuration.
   */
  virtual BoringSslPrivateKeyMethodSharedPtr getBoringSslPrivateKeyMethod() PURE;
};

typedef std::shared_ptr<PrivateKeyMethodProvider> PrivateKeyMethodProviderSharedPtr;

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
   * @param message a protobuf message object containing a
   * PrivateKeyProvider message.
   * @param private_key_method_provider_context context that provides components for creating and
   * initializing connections for keyless TLS etc.
   * @return PrivateKeyMethodProvider the private key operations provider, or nullptr if
   * no provider can be used with the context configuration.
   */
  virtual PrivateKeyMethodProviderSharedPtr
  createPrivateKeyMethodProvider(const envoy::api::v2::auth::PrivateKeyProvider& message,
                                 Envoy::Server::Configuration::TransportSocketFactoryContext&
                                     private_key_method_provider_context) PURE;
};

} // namespace Ssl
} // namespace Envoy
