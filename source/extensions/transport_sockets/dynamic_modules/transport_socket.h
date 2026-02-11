#pragma once

#include <memory>

#include "envoy/network/transport_socket.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/transport_socket_abi.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

// Forward declaration.
class DynamicModuleTransportSocketFactoryConfig;
using DynamicModuleTransportSocketFactoryConfigSharedPtr =
    std::shared_ptr<DynamicModuleTransportSocketFactoryConfig>;

/**
 * A transport socket that delegates its implementation to a dynamic module.
 * This acts as a bridge between Envoy's Network::TransportSocket interface and
 * the dynamic module's transport socket implementation.
 */
class DynamicModuleTransportSocket : public Network::TransportSocket,
                                     public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  explicit DynamicModuleTransportSocket(
      DynamicModuleTransportSocketFactoryConfigSharedPtr factory_config);
  ~DynamicModuleTransportSocket() override;

  // Network::TransportSocket implementation.
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  absl::string_view failureReason() const override;
  bool canFlushClose() override;
  void closeSocket(Network::ConnectionEvent event) override;
  Network::IoResult doRead(Buffer::Instance& buffer) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  void onConnected() override;
  Ssl::ConnectionInfoConstSharedPtr ssl() const override;
  bool startSecureTransport() override { return false; }
  void configureInitialCongestionWindow(uint64_t, std::chrono::microseconds) override {}

  // Accessors for ABI callbacks.
  Network::TransportSocketCallbacks* callbacks() { return callbacks_; }
  Buffer::Instance* currentReadBuffer() { return current_read_buffer_; }
  Buffer::Instance* currentWriteBuffer() { return current_write_buffer_; }

  // Test-only setters for buffer pointers.
  void setCurrentReadBuffer(Buffer::Instance* buffer) { current_read_buffer_ = buffer; }
  void setCurrentWriteBuffer(Buffer::Instance* buffer) { current_write_buffer_ = buffer; }

private:
  void* thisAsVoidPtr() { return static_cast<void*>(this); }
  void destroy();

  const DynamicModuleTransportSocketFactoryConfigSharedPtr factory_config_;
  envoy_dynamic_module_type_transport_socket_module_ptr in_module_socket_ = nullptr;

  Network::TransportSocketCallbacks* callbacks_ = nullptr;
  Buffer::Instance* current_read_buffer_ = nullptr;
  Buffer::Instance* current_write_buffer_ = nullptr;

  // Cached strings to ensure the string_view returned by failureReason() and the string
  // returned by protocol() remain valid.
  mutable std::string cached_protocol_;
  mutable std::string cached_failure_reason_;

  bool destroyed_ = false;
};

} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
