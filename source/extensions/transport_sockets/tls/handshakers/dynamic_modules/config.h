#pragma once

#include <memory>
#include <string>

#include "envoy/ssl/handshaker.h"

#include "source/common/common/statusor.h"
#include "source/common/tls/ssl_handshaker.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace DynamicModules {

// Function pointer types for the TLS handshaker ABI hooks.
using OnTlsHandshakerConfigNewType = decltype(&envoy_dynamic_module_on_tls_handshaker_config_new);
using OnTlsHandshakerConfigDestroyType =
    decltype(&envoy_dynamic_module_on_tls_handshaker_config_destroy);
using OnTlsHandshakerConfigureSslContextType =
    decltype(&envoy_dynamic_module_on_tls_handshaker_configure_ssl_context);
using OnTlsHandshakerNewType = decltype(&envoy_dynamic_module_on_tls_handshaker_new);
using OnTlsHandshakerDestroyType = decltype(&envoy_dynamic_module_on_tls_handshaker_destroy);
using OnTlsHandshakerHandshakeType = decltype(&envoy_dynamic_module_on_tls_handshaker_handshake);

/**
 * Configuration holding the resolved dynamic module, ABI function pointers, and the in-module
 * configuration. This is shared between the factory and every handshaker it creates, which keeps
 * the module loaded for as long as any handshaker is alive.
 */
class DynamicModuleHandshakerConfig {
public:
  DynamicModuleHandshakerConfig(Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  ~DynamicModuleHandshakerConfig();

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_tls_handshaker_config_module_ptr in_module_config_ = nullptr;

  // Resolved mandatory function pointer. Guaranteed non-null after successful creation.
  OnTlsHandshakerConfigDestroyType on_config_destroy_ = nullptr;

  // Resolved optional function pointers. Null when the module does not export the hook.
  OnTlsHandshakerConfigureSslContextType on_configure_ssl_context_ = nullptr;
  OnTlsHandshakerNewType on_new_ = nullptr;
  OnTlsHandshakerDestroyType on_destroy_ = nullptr;
  OnTlsHandshakerHandshakeType on_handshake_ = nullptr;

  const Ssl::HandshakerCapabilities& capabilities() const { return capabilities_; }
  void setCapabilities(const Ssl::HandshakerCapabilities& capabilities) {
    capabilities_ = capabilities;
  }

private:
  Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
  Ssl::HandshakerCapabilities capabilities_;
};

using DynamicModuleHandshakerConfigSharedPtr = std::shared_ptr<DynamicModuleHandshakerConfig>;

/**
 * Creates a new DynamicModuleHandshakerConfig. Resolves the ABI symbols, creates the in-module
 * config, and reads the module capabilities. Returns an error if symbol resolution or module
 * initialization fails.
 */
absl::StatusOr<DynamicModuleHandshakerConfigSharedPtr> newDynamicModuleHandshakerConfig(
    const std::string& handshaker_name, const std::string& handshaker_config,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module);

/**
 * Handshaker backed by a dynamic module. Corresponds to a single connection and runs entirely on
 * that connection's worker thread. It subclasses SslHandshakerImpl so Envoy owns the BoringSSL
 * state and the connection info, and overrides doHandshake to delegate to the module when the
 * module drives the handshake. A null config means the module failed to load, in which case the
 * handshaker closes the connection.
 */
class DynamicModuleHandshaker : public SslHandshakerImpl {
public:
  DynamicModuleHandshaker(DynamicModuleHandshakerConfigSharedPtr config, bssl::UniquePtr<SSL> ssl,
                          int ssl_extended_socket_info_index,
                          Ssl::HandshakeCallbacks* handshake_callbacks);
  ~DynamicModuleHandshaker() override;

  // Ssl::Handshaker
  Network::PostIoAction doHandshake() override;

private:
  const DynamicModuleHandshakerConfigSharedPtr config_;
  envoy_dynamic_module_type_tls_handshaker_module_ptr in_module_handshaker_ = nullptr;
};

/**
 * Factory for creating dynamic module handshakers. Registered under the envoy.tls_handshakers
 * category and used for both client and server TLS contexts.
 */
class DynamicModuleHandshakerFactory : public Ssl::HandshakerFactory {
public:
  std::string name() const override { return "envoy.tls.handshakers.dynamic_modules"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  Ssl::HandshakerFactoryCb
  createHandshakerCb(const Protobuf::Message& message, Ssl::HandshakerFactoryContext& context,
                     ProtobufMessage::ValidationVisitor& validation_visitor) override;

  Ssl::HandshakerCapabilities capabilities() const override;

  Ssl::SslCtxCb sslctxCb(Ssl::HandshakerFactoryContext& handshaker_factory_context) const override;

private:
  // Transient config set during `createHandshakerCb` and read by `capabilities()` and
  // `sslctxCb()`. The context config creation calls these three in sequence on the main thread, so
  // a plain member is safe. The returned handshaker callback and the `sslctxCb` lambda hold their
  // own shared_ptr to the config, so it outlives this transient reference.
  mutable DynamicModuleHandshakerConfigSharedPtr pending_config_;
};

DECLARE_FACTORY(DynamicModuleHandshakerFactory);

} // namespace DynamicModules
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
