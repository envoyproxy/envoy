#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"
#include "envoy/ssl/connection.h"

#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/transport_sockets/dynamic_modules/config.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace TransportSockets {

/**
 * Implementation of Network::TransportSocket that uses a dynamic module.
 *
 * This class acts as a wrapper that delegates all transport socket operations to an implementation
 * loaded from a shared library through the dynamic modules ABI. It supports both upstream (client)
 * and downstream (server) transport sockets.
 *
 * The dynamic module can implement custom transport layer protocols, including TLS implementations
 * using alternative libraries (e.g., Rustls instead of BoringSSL), custom encryption schemes, or
 * protocol transformations.
 *
 * Lifecycle:
 * 1. Construction: Creates the wrapper with a shared configuration
 * 2. setTransportSocketCallbacks: Initializes the in-module socket instance
 * 3. onConnected: Notifies the module that the underlying connection is established
 * 4. doRead/doWrite: Delegates I/O operations to the module
 * 5. Destruction: Cleans up the in-module socket instance
 *
 * Thread Safety: This class is not thread-safe and should only be accessed from the connection's
 * dispatcher thread.
 */
class DynamicModuleTransportSocket : public Network::TransportSocket,
                                     protected Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  DynamicModuleTransportSocket(DynamicModuleTransportSocketConfigSharedPtr config,
                               bool is_upstream);
  ~DynamicModuleTransportSocket() override;

  // Network::TransportSocket.
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  absl::string_view failureReason() const override;
  [[nodiscard]] bool canFlushClose() override;
  void closeSocket(Network::ConnectionEvent event) override;
  void onConnected() override;
  [[nodiscard]] Network::IoResult doRead(Buffer::Instance& buffer) override;
  [[nodiscard]] Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  Ssl::ConnectionInfoConstSharedPtr ssl() const override;
  [[nodiscard]] bool startSecureTransport() override;
  void configureInitialCongestionWindow(uint64_t bandwidth_bits_per_sec,
                                        std::chrono::microseconds rtt) override;

  // Accessors for callbacks - used by the ABI implementation.
  Network::TransportSocketCallbacks* callbacks() { return callbacks_; }

private:
  /**
   * Initialize the in-module transport socket instance.
   */
  void initializeInModuleSocket();

  /**
   * Clean up the in-module transport socket instance.
   */
  void destroyInModuleSocket();

  DynamicModuleTransportSocketConfigSharedPtr config_;
  Network::TransportSocketCallbacks* callbacks_{nullptr};

  // The pointer to the in-module transport socket instance.
  void* in_module_socket_{nullptr};

  // Whether this is an upstream or downstream socket.
  [[maybe_unused]] bool is_upstream_;

  // Cached protocol string from the module.
  mutable std::string protocol_;
  mutable bool protocol_cached_{false};

  // Cached failure reason from the module.
  mutable std::string failure_reason_;
  mutable bool failure_reason_cached_{false};

  // SSL connection info implementation.
  class DynamicModuleSslConnectionInfo;
  mutable Ssl::ConnectionInfoConstSharedPtr ssl_info_;
};

using DynamicModuleTransportSocketPtr = std::unique_ptr<DynamicModuleTransportSocket>;

} // namespace TransportSockets
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
