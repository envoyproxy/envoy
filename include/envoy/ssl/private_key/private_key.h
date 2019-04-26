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

class PrivateKeyOperations {
public:
  virtual ~PrivateKeyOperations() {}
};

typedef std::unique_ptr<PrivateKeyOperations> PrivateKeyOperationsPtr;

class PrivateKeyOperationsProvider {
public:
  virtual ~PrivateKeyOperationsProvider() {}

  /**
   * Get a private key operations instance from the provider.
   * @param ssl a SSL connection object.
   * @param cb a callbacks object, whose "complete" method will be invoked
   * when the asynchronous processing is complete.
   * @param dispatcher supplies the owning thread's dispatcher.
   * @return the private key operations instance.
   */
  virtual PrivateKeyOperationsPtr getPrivateKeyOperations(SSL* ssl,
                                                          PrivateKeyOperationsCallbacks& cb,
                                                          Event::Dispatcher& dispatcher) PURE;

  /**
   * Get the private key methods from the provider.
   * @return the private key methods associated with this provider and
   * configuration.
   */
  virtual BoringSslPrivateKeyMethodSharedPtr getBoringSslPrivateKeyMethod() PURE;
};

typedef std::shared_ptr<PrivateKeyOperationsProvider> PrivateKeyOperationsProviderSharedPtr;

/**
 * A manager for finding correct user-provided functions for handling BoringSSL private key
 * operations.
 */
class PrivateKeyOperationsManager {
public:
  virtual ~PrivateKeyOperationsManager() {}

  /**
   * Finds and returns a private key operations provider for BoringSSL.
   *
   * @param message a protobuf message object containing a
   * PrivateKeyMethod message.
   * @param private_key_provider_context context that provides components for creating and
   * initializing connections for keyless TLS etc.
   * @return PrivateKeyOperationsProvider the private key operations provider, or nullptr if
   * no provider can be used with the context configuration.
   */
  virtual PrivateKeyOperationsProviderSharedPtr
  createPrivateKeyOperationsProvider(const envoy::api::v2::auth::PrivateKeyMethod& message,
                                     Envoy::Server::Configuration::TransportSocketFactoryContext&
                                         private_key_provider_context) PURE;
};

} // namespace Ssl
} // namespace Envoy
