#pragma once

#include "envoy/network/filter.h"
#include "envoy/network/listener_filter_buffer.h"

#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/filters/listener/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace ListenerFilters {

/**
 * A listener filter that uses a dynamic module. Corresponds to a single accepted connection.
 */
class DynamicModuleListenerFilter : public Network::ListenerFilter,
                                    public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  explicit DynamicModuleListenerFilter(DynamicModuleListenerFilterConfigSharedPtr config);
  ~DynamicModuleListenerFilter() override;

  /**
   * Initializes the in-module filter.
   */
  void initializeInModuleFilter();

  // ---------- Network::ListenerFilter ----------
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;
  Network::FilterStatus onData(Network::ListenerFilterBuffer& buffer) override;
  void onClose() override;
  size_t maxReadBytes() const override;

  // Accessors for ABI callbacks.
  Network::ListenerFilterCallbacks* callbacks() { return callbacks_; }
  Network::ListenerFilterBuffer* currentBuffer() { return current_buffer_; }
  Network::Address::InstanceConstSharedPtr& cachedOriginalDst() { return cached_original_dst_; }

  // Test-only setters.
  void setCallbacksForTest(Network::ListenerFilterCallbacks* callbacks) { callbacks_ = callbacks; }
  void setCurrentBufferForTest(Network::ListenerFilterBuffer* buffer) { current_buffer_ = buffer; }

  /**
   * Check if the filter has been destroyed.
   */
  bool isDestroyed() const { return destroyed_; }

  /**
   * Get the filter configuration.
   */
  const DynamicModuleListenerFilterConfig& getFilterConfig() const { return *config_; }

private:
  /**
   * Helper to get the `this` pointer as a void pointer.
   */
  void* thisAsVoidPtr() { return static_cast<void*>(this); }

  /**
   * Called when filter is destroyed. Forwards the call to the module via
   * on_listener_filter_destroy_ and resets in_module_filter_ to null. Subsequent calls are a
   * no-op.
   */
  void destroy();

  const DynamicModuleListenerFilterConfigSharedPtr config_;
  envoy_dynamic_module_type_listener_filter_module_ptr in_module_filter_ = nullptr;

  Network::ListenerFilterCallbacks* callbacks_ = nullptr;

  // Current buffer, only valid during onData callback.
  Network::ListenerFilterBuffer* current_buffer_ = nullptr;

  // Cached original destination address to ensure lifetime extends beyond ABI callback.
  Network::Address::InstanceConstSharedPtr cached_original_dst_;

  bool destroyed_ = false;
};

using DynamicModuleListenerFilterSharedPtr = std::shared_ptr<DynamicModuleListenerFilter>;

} // namespace ListenerFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
