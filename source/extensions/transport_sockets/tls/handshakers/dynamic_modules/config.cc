#include "source/extensions/transport_sockets/tls/handshakers/dynamic_modules/config.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/transport_sockets/tls/handshakers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/transport_sockets/tls/handshakers/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace DynamicModules {

namespace {

envoy_dynamic_module_type_tls_handshaker_capabilities
toAbiCapabilities(const Ssl::HandshakerCapabilities& capabilities) {
  envoy_dynamic_module_type_tls_handshaker_capabilities out;
  out.provides_certificates = capabilities.provides_certificates;
  out.verifies_peer_certificates = capabilities.verifies_peer_certificates;
  out.handles_session_resumption = capabilities.handles_session_resumption;
  out.provides_ciphers_and_curves = capabilities.provides_ciphers_and_curves;
  out.handles_alpn_selection = capabilities.handles_alpn_selection;
  out.is_fips_compliant = capabilities.is_fips_compliant;
  out.provides_sigalgs = capabilities.provides_sigalgs;
  return out;
}

Ssl::HandshakerCapabilities
fromAbiCapabilities(const envoy_dynamic_module_type_tls_handshaker_capabilities& capabilities) {
  Ssl::HandshakerCapabilities out;
  out.provides_certificates = capabilities.provides_certificates;
  out.verifies_peer_certificates = capabilities.verifies_peer_certificates;
  out.handles_session_resumption = capabilities.handles_session_resumption;
  out.provides_ciphers_and_curves = capabilities.provides_ciphers_and_curves;
  out.handles_alpn_selection = capabilities.handles_alpn_selection;
  out.is_fips_compliant = capabilities.is_fips_compliant;
  out.provides_sigalgs = capabilities.provides_sigalgs;
  return out;
}

// Loads the dynamic module and builds the shared handshaker config from the typed config.
absl::StatusOr<DynamicModuleHandshakerConfigSharedPtr>
loadHandshakerConfig(const Protobuf::Message& message,
                     ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto* typed_config = dynamic_cast<const Protobuf::Any*>(&message);
  if (typed_config == nullptr) {
    return absl::InvalidArgumentError("Unexpected dynamic module TLS handshaker config type");
  }

  envoy::extensions::transport_sockets::tls::handshakers::dynamic_modules::v3::
      DynamicModuleTlsHandshaker proto_config;
  RETURN_IF_NOT_OK(
      Config::Utility::translateOpaqueConfig(*typed_config, validation_visitor, proto_config));

  // Handshakers do not support remote module sources, so no init manager or async callback is
  // passed. Only the synchronous local-file and by-name paths can succeed here.
  auto load_result = Envoy::Extensions::DynamicModules::newDynamicModuleByConfig(
      proto_config.dynamic_module_config(), proto_config.handshaker_name());
  RETURN_IF_NOT_OK_REF(load_result.status());

  std::string handshaker_config_str;
  if (proto_config.has_handshaker_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.handshaker_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    handshaker_config_str = std::move(config_or_error.value());
  }

  return newDynamicModuleHandshakerConfig(proto_config.handshaker_name(), handshaker_config_str,
                                          std::move(load_result->loaded));
}

} // namespace

DynamicModuleHandshakerConfig::DynamicModuleHandshakerConfig(
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module)
    : dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleHandshakerConfig::~DynamicModuleHandshakerConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
    in_module_config_ = nullptr;
  }
}

absl::StatusOr<DynamicModuleHandshakerConfigSharedPtr> newDynamicModuleHandshakerConfig(
    const std::string& handshaker_name, const std::string& handshaker_config,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module) {

  auto on_config_new = dynamic_module->getFunctionPointer<OnTlsHandshakerConfigNewType>(
      "envoy_dynamic_module_on_tls_handshaker_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnTlsHandshakerConfigDestroyType>(
      "envoy_dynamic_module_on_tls_handshaker_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  // Optional hooks. An unresolved symbol means the module does not implement the hook.
  auto on_configure_ssl_context =
      dynamic_module->getFunctionPointer<OnTlsHandshakerConfigureSslContextType>(
          "envoy_dynamic_module_on_tls_handshaker_configure_ssl_context");
  auto on_new = dynamic_module->getFunctionPointer<OnTlsHandshakerNewType>(
      "envoy_dynamic_module_on_tls_handshaker_new");
  auto on_destroy = dynamic_module->getFunctionPointer<OnTlsHandshakerDestroyType>(
      "envoy_dynamic_module_on_tls_handshaker_destroy");
  auto on_handshake = dynamic_module->getFunctionPointer<OnTlsHandshakerHandshakeType>(
      "envoy_dynamic_module_on_tls_handshaker_handshake");

  // The per-connection new and destroy hooks are a pair, so require both or neither.
  if (on_new.ok() != on_destroy.ok()) {
    return absl::InvalidArgumentError("dynamic module TLS handshaker must export both "
                                      "envoy_dynamic_module_on_tls_handshaker_new and "
                                      "envoy_dynamic_module_on_tls_handshaker_destroy, or neither");
  }

  auto config = std::make_shared<DynamicModuleHandshakerConfig>(std::move(dynamic_module));
  config->on_config_destroy_ = on_config_destroy.value();
  if (on_configure_ssl_context.ok()) {
    config->on_configure_ssl_context_ = on_configure_ssl_context.value();
  }
  if (on_new.ok()) {
    config->on_new_ = on_new.value();
    config->on_destroy_ = on_destroy.value();
  }
  if (on_handshake.ok()) {
    config->on_handshake_ = on_handshake.value();
  }

  // Envoy fills the capabilities with the defaults, and the module overrides what it needs.
  envoy_dynamic_module_type_tls_handshaker_capabilities capabilities =
      toAbiCapabilities(Ssl::HandshakerCapabilities{});

  envoy_dynamic_module_type_envoy_buffer name_buffer = {handshaker_name.data(),
                                                        handshaker_name.size()};
  envoy_dynamic_module_type_envoy_buffer config_buffer = {handshaker_config.data(),
                                                          handshaker_config.size()};
  config->in_module_config_ = on_config_new.value()(static_cast<void*>(config.get()), name_buffer,
                                                    config_buffer, &capabilities);
  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module TLS handshaker config");
  }

  config->setCapabilities(fromAbiCapabilities(capabilities));
  return config;
}

DynamicModuleHandshaker::DynamicModuleHandshaker(DynamicModuleHandshakerConfigSharedPtr config,
                                                 bssl::UniquePtr<SSL> ssl,
                                                 int ssl_extended_socket_info_index,
                                                 Ssl::HandshakeCallbacks* handshake_callbacks)
    : SslHandshakerImpl(std::move(ssl), ssl_extended_socket_info_index, handshake_callbacks),
      config_(std::move(config)) {
  if (config_ != nullptr && config_->on_new_ != nullptr) {
    in_module_handshaker_ = config_->on_new_(config_->in_module_config_, this->ssl());
    if (in_module_handshaker_ == nullptr) {
      ENVOY_LOG(error, "dynamic module failed to create TLS handshaker; connection will be closed");
    }
  }
}

DynamicModuleHandshaker::~DynamicModuleHandshaker() {
  if (in_module_handshaker_ != nullptr && config_->on_destroy_ != nullptr) {
    config_->on_destroy_(in_module_handshaker_);
    in_module_handshaker_ = nullptr;
  }
}

Network::PostIoAction DynamicModuleHandshaker::doHandshake() {
  // The module failed to load, so close the connection.
  if (config_ == nullptr) {
    return Network::PostIoAction::Close;
  }
  // The module expected a per-connection handshaker but failed to create one, so close.
  if (config_->on_new_ != nullptr && in_module_handshaker_ == nullptr) {
    return Network::PostIoAction::Close;
  }
  if (config_->on_handshake_ == nullptr) {
    return SslHandshakerImpl::doHandshake();
  }
  // The module drives the handshake and reports the outcome, and Envoy applies the matching socket
  // state transition and the success or failure notification.
  switch (config_->on_handshake_(in_module_handshaker_)) {
  case envoy_dynamic_module_type_tls_handshaker_result_Complete:
    setState(Ssl::SocketState::HandshakeComplete);
    handshakeCallbacks()->onSuccess(ssl());
    return handshakeCallbacks()->connection().state() == Network::Connection::State::Open
               ? Network::PostIoAction::KeepOpen
               : Network::PostIoAction::Close;
  case envoy_dynamic_module_type_tls_handshaker_result_Failed:
    handshakeCallbacks()->onFailure();
    return Network::PostIoAction::Close;
  case envoy_dynamic_module_type_tls_handshaker_result_WaitForData:
    setState(Ssl::SocketState::HandshakeWaitingForConnectionData);
    return Network::PostIoAction::KeepOpen;
  case envoy_dynamic_module_type_tls_handshaker_result_Pending:
    setState(Ssl::SocketState::HandshakeBlockedOnAsyncOperation);
    return Network::PostIoAction::KeepOpen;
  case envoy_dynamic_module_type_tls_handshaker_result_RunDefault:
    return SslHandshakerImpl::doHandshake();
  }
  return Network::PostIoAction::Close;
}

ProtobufTypes::MessagePtr DynamicModuleHandshakerFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::tls::handshakers::dynamic_modules::
                              v3::DynamicModuleTlsHandshaker>();
}

Ssl::HandshakerFactoryCb DynamicModuleHandshakerFactory::createHandshakerCb(
    const Protobuf::Message& message, Ssl::HandshakerFactoryContext&,
    ProtobufMessage::ValidationVisitor& validation_visitor) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  // The handshaker factory interface has no error channel, so a load failure is logged and the
  // handshaker closes the connections it would serve rather than being rejected at config time.
  auto config_or_error = loadHandshakerConfig(message, validation_visitor);
  if (!config_or_error.ok()) {
    ENVOY_LOG_MISC(error, "failed to load dynamic module TLS handshaker: {}",
                   config_or_error.status().message());
    pending_config_ = nullptr;
  } else {
    pending_config_ = std::move(config_or_error.value());
  }

  DynamicModuleHandshakerConfigSharedPtr config = pending_config_;
  return [config](bssl::UniquePtr<SSL> ssl, int ssl_extended_socket_info_index,
                  Ssl::HandshakeCallbacks* handshake_callbacks) -> Ssl::HandshakerSharedPtr {
    return std::make_shared<DynamicModuleHandshaker>(
        config, std::move(ssl), ssl_extended_socket_info_index, handshake_callbacks);
  };
}

Ssl::HandshakerCapabilities DynamicModuleHandshakerFactory::capabilities() const {
  // Fall back to the default capabilities when the module failed to load.
  return pending_config_ != nullptr ? pending_config_->capabilities()
                                    : Ssl::HandshakerCapabilities{};
}

Ssl::SslCtxCb DynamicModuleHandshakerFactory::sslctxCb(Ssl::HandshakerFactoryContext&) const {
  DynamicModuleHandshakerConfigSharedPtr config = pending_config_;
  if (config == nullptr || config->on_configure_ssl_context_ == nullptr) {
    return nullptr;
  }
  return [config](SSL_CTX* ssl_ctx) {
    if (!config->on_configure_ssl_context_(config->in_module_config_, ssl_ctx)) {
      ENVOY_LOG_MISC(warn, "dynamic module TLS handshaker failed to configure SSL_CTX");
    }
  };
}

REGISTER_FACTORY(DynamicModuleHandshakerFactory, Ssl::HandshakerFactory);

} // namespace DynamicModules
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
