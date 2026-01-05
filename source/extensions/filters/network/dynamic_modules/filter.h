#pragma once

#include "envoy/network/connection.h"
#include "envoy/network/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/filters/network/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace NetworkFilters {

/**
 * A network filter that uses a dynamic module. Corresponds to a single TCP connection.
 */
class DynamicModuleNetworkFilter : public Network::Filter,
                                   public Network::ConnectionCallbacks,
                                   public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  DynamicModuleNetworkFilter(DynamicModuleNetworkFilterConfigSharedPtr config);
  ~DynamicModuleNetworkFilter() override;

  /**
   * Initializes the in-module filter.
   */
  void initializeInModuleFilter();

  // ---------- Network::ReadFilter ----------
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // ---------- Network::WriteFilter ----------
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override;

  // ---------- Network::ConnectionCallbacks ----------
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;

  // Accessors for ABI callbacks.
  Network::ReadFilterCallbacks* readCallbacks() { return read_callbacks_; }
  Network::WriteFilterCallbacks* writeCallbacks() { return write_callbacks_; }
  Buffer::Instance* currentReadBuffer() { return current_read_buffer_; }
  Buffer::Instance* currentWriteBuffer() { return current_write_buffer_; }

  // Test-only setters for buffer pointers.
  void setCurrentReadBufferForTest(Buffer::Instance* buffer) { current_read_buffer_ = buffer; }
  void setCurrentWriteBufferForTest(Buffer::Instance* buffer) { current_write_buffer_ = buffer; }

  /**
   * Continue reading after returning StopIteration.
   */
  void continueReading();

  /**
   * Close the connection.
   */
  void close(Network::ConnectionCloseType close_type);

  /**
   * Write data to the connection.
   */
  void write(Buffer::Instance& data, bool end_stream);

  /**
   * Check if the filter has been destroyed.
   */
  bool isDestroyed() const { return destroyed_; }

  /**
   * Get the filter configuration.
   */
  const DynamicModuleNetworkFilterConfig& getFilterConfig() const { return *config_; }

  /**
   * Get the connection.
   */
  Network::Connection& connection() { return read_callbacks_->connection(); }

private:
  /**
   * Helper to get the `this` pointer as a void pointer.
   */
  void* thisAsVoidPtr() { return static_cast<void*>(this); }

  /**
   * Called when filter is destroyed. Forwards the call to the module via on_network_filter_destroy_
   * and resets in_module_filter_ to null. Subsequent calls are a no-op.
   */
  void destroy();

  const DynamicModuleNetworkFilterConfigSharedPtr config_;
  envoy_dynamic_module_type_network_filter_module_ptr in_module_filter_ = nullptr;

  Network::ReadFilterCallbacks* read_callbacks_ = nullptr;
  Network::WriteFilterCallbacks* write_callbacks_ = nullptr;

  // Current buffers, only valid during callbacks.
  Buffer::Instance* current_read_buffer_ = nullptr;
  Buffer::Instance* current_write_buffer_ = nullptr;

  bool destroyed_ = false;
};

using DynamicModuleNetworkFilterSharedPtr = std::shared_ptr<DynamicModuleNetworkFilter>;

} // namespace NetworkFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
