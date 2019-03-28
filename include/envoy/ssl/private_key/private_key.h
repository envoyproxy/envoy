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
class TransportSocketFactoryContext;
} // namespace Configuration
} // namespace Server

namespace Ssl {

typedef std::shared_ptr<SSL_PRIVATE_KEY_METHOD> PrivateKeyMethodSharedPtr;

class PrivateKeyOperations {
public:
  virtual ~PrivateKeyOperations() {}

  /**
   * Get the private key asynchronous methods from the provider, if the context can be handled by
   * the provider.
   * @param ssl a SSL connection object.
   * @return private key methods, or nullptr if regular TLS processing should happen.
   */
  virtual PrivateKeyMethodSharedPtr getPrivateKeyMethods(SSL* ssl) PURE;
};

typedef std::unique_ptr<PrivateKeyOperations> PrivateKeyOperationsPtr;

class PrivateKeyOperationsProvider {
public:
  virtual ~PrivateKeyOperationsProvider() {}

  /**
   * Get a private key operations instance from the provider.
   * @param cb a callbacks object, whose "complete" method will be invoked when the asynchronous
   * processing is complete.
   * @param dispatcher supplies the owning thread's dispatcher.
   * @return the private key operations.
   */
  virtual PrivateKeyOperationsPtr getPrivateKeyOperations(PrivateKeyOperationsCallbacks& cb,
                                                          Event::Dispatcher& dispatcher) PURE;
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
   * @param config_source a protobuf message object containing a TLS config source.
   * @param config_name a name that uniquely refers to the private key operations provider.
   * @param private_key_provider_context context that provides components for creating and
   * initializing connections for keyless TLS etc.
   * @return TlsPrivateKeyOperationsProvider the private key operations provider, or nullptr if
   * no provider can be used with the context configuration.
   */
  virtual PrivateKeyOperationsProviderSharedPtr createPrivateKeyOperationsProvider(
      const envoy::api::v2::auth::PrivateKeyOperations& message,
      Server::Configuration::TransportSocketFactoryContext& private_key_provider_context) PURE;
};

// typedef std::shared_ptr<PrivateKeyOperationsManager> PrivateKeyOperationsManagerSharedPtr;

} // namespace Ssl
} // namespace Envoy
