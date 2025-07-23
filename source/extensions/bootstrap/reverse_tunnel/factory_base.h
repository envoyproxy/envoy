#pragma once

#include "envoy/server/bootstrap_extension_config.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * Common base class for reverse connection bootstrap factory registrations.
 * Removes a substantial amount of boilerplate and follows Envoy's factory patterns.
 * Template parameters:
 *   ConfigProto: Protobuf message type for the bootstrap configuration
 *   ExtensionImpl: The actual bootstrap extension implementation class
 */
template <class ConfigProto, class ExtensionImpl>
class ReverseConnectionBootstrapFactoryBase
    : public Server::Configuration::BootstrapExtensionFactory {
public:
  // Server::Configuration::BootstrapExtensionFactory implementation
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override {
    return createBootstrapExtensionTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(
                                             config, context.messageValidationVisitor()),
                                         context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return name_; }

protected:
  /**
   * Constructor for the factory base.
   * @param name the name of the bootstrap extension factory
   */
  explicit ReverseConnectionBootstrapFactoryBase(const std::string& name) : name_(name) {}

private:
  /**
   * Create the typed bootstrap extension from the validated protobuf configuration.
   * This method is implemented by derived classes to create the specific extension.
   * @param proto_config the validated protobuf configuration
   * @param context the server factory context
   * @return unique pointer to the created bootstrap extension
   */
  virtual Server::BootstrapExtensionPtr
  createBootstrapExtensionTyped(const ConfigProto& proto_config,
                                Server::Configuration::ServerFactoryContext& context) PURE;

  const std::string name_;
};

/**
 * Thread-safe factory utilities for reverse connection extensions.
 * Provides common thread safety patterns and validation helpers.
 */
class ReverseConnectionFactoryUtils {
public:
  /**
   * Validate thread-local slot availability before using it.
   * Follows Envoy's patterns for safe TLS access.
   * @param tls_slot the thread-local slot to validate
   * @param extension_name name of the extension for error messages
   * @return true if the slot is safe to use, false otherwise
   */
  template <typename TlsType>
  static bool
  validateThreadLocalSlot(const std::unique_ptr<ThreadLocal::TypedSlot<TlsType>>& tls_slot,
                          const std::string& /* extension_name */) {
    return tls_slot != nullptr;
  }

  /**
   * Safe access to thread-local registry with proper error handling.
   * @param tls_slot the thread-local slot to access
   * @param extension_name name of the extension for error messages
   * @return pointer to the thread-local object, or nullptr if not available
   */
  template <typename TlsType>
  static TlsType*
  safeGetThreadLocal(const std::unique_ptr<ThreadLocal::TypedSlot<TlsType>>& tls_slot,
                     const std::string& extension_name) {
    if (!validateThreadLocalSlot(tls_slot, extension_name)) {
      return nullptr;
    }

    try {
      if (auto opt = tls_slot->get(); opt.has_value()) {
        return &opt.value().get();
      }
    } catch (const std::exception&) {
      // Exception during TLS access - return nullptr for safety
    }

    return nullptr;
  }

  /**
   * Create thread-local slot with proper error handling and validation.
   * @param thread_local_manager the thread local manager
   * @param extension_name name of the extension for logging
   * @return unique pointer to the created slot, or nullptr on failure
   */
  template <typename TlsType>
  static std::unique_ptr<ThreadLocal::TypedSlot<TlsType>>
  createThreadLocalSlot(ThreadLocal::SlotAllocator& thread_local_manager,
                        const std::string& /* extension_name */) {
    try {
      return ThreadLocal::TypedSlot<TlsType>::makeUnique(thread_local_manager);
    } catch (const std::exception&) {
      // Failed to create slot - return nullptr
      return nullptr;
    }
  }
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
