#pragma once

#include "envoy/http/async_client.h"
#include "envoy/network/filter.h"
#include "envoy/network/listener_filter_buffer.h"

#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/filters/listener/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace ListenerFilters {

class DynamicModuleListenerFilter;
using DynamicModuleListenerFilterSharedPtr = std::shared_ptr<DynamicModuleListenerFilter>;
using DynamicModuleListenerFilterWeakPtr = std::weak_ptr<DynamicModuleListenerFilter>;

/**
 * A listener filter that uses a dynamic module. Corresponds to a single accepted connection.
 */
class DynamicModuleListenerFilter
    : public Network::ListenerFilter,
      public std::enable_shared_from_this<DynamicModuleListenerFilter>,
      public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  explicit DynamicModuleListenerFilter(DynamicModuleListenerFilterConfigSharedPtr config);
  ~DynamicModuleListenerFilter() override;

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

  /**
   * Sends an HTTP callout to the specified cluster with the given message.
   */
  envoy_dynamic_module_type_http_callout_init_result
  sendHttpCallout(uint64_t* callout_id_out, absl::string_view cluster_name,
                  Http::RequestMessagePtr&& message, uint64_t timeout_milliseconds);

  /**
   * This is called when an event is scheduled via DynamicModuleListenerFilterScheduler.
   */
  void onScheduled(uint64_t event_id);

  /**
   * Get the dispatcher for the worker thread this filter is running on.
   * Returns nullptr if callbacks are not set.
   */
  Event::Dispatcher* dispatcher() {
    return callbacks_ != nullptr ? &callbacks_->dispatcher() : nullptr;
  }

  /**
   * Returns the worker index assigned to this filter.
   */
  uint32_t workerIndex() const { return worker_index_; }

private:
  /**
   * Initializes the in-module filter.
   */
  void initializeInModuleFilter();

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

  uint32_t worker_index_;

  /**
   * This implementation of the AsyncClient::Callbacks is used to handle the response from the HTTP
   * callout from the parent listener filter.
   */
  class HttpCalloutCallback : public Http::AsyncClient::Callbacks {
  public:
    HttpCalloutCallback(std::shared_ptr<DynamicModuleListenerFilter> filter, uint64_t id)
        : filter_(std::move(filter)), callout_id_(id) {}
    ~HttpCalloutCallback() override = default;

    void onSuccess(const Http::AsyncClient::Request& request,
                   Http::ResponseMessagePtr&& response) override;
    void onFailure(const Http::AsyncClient::Request& request,
                   Http::AsyncClient::FailureReason reason) override;
    void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                      const Http::ResponseHeaderMap*) override {};
    // This is the request object that is used to send the HTTP callout. It is used to cancel the
    // callout if the filter is destroyed before the callout is completed.
    Http::AsyncClient::Request* request_ = nullptr;

  private:
    const std::weak_ptr<DynamicModuleListenerFilter> filter_;
    const uint64_t callout_id_{};
  };

  uint64_t getNextCalloutId() { return next_callout_id_++; }

  uint64_t next_callout_id_ = 1; // 0 is reserved as an invalid id.

  absl::flat_hash_map<uint64_t, std::unique_ptr<DynamicModuleListenerFilter::HttpCalloutCallback>>
      http_callouts_;
};

/**
 * This class is used to schedule a listener filter event hook from a different thread
 * than the one it was assigned to. This is created via
 * envoy_dynamic_module_callback_listener_filter_scheduler_new and deleted via
 * envoy_dynamic_module_callback_listener_filter_scheduler_delete.
 */
class DynamicModuleListenerFilterScheduler {
public:
  DynamicModuleListenerFilterScheduler(DynamicModuleListenerFilterWeakPtr filter,
                                       Event::Dispatcher& dispatcher)
      : filter_(std::move(filter)), dispatcher_(dispatcher) {}

  void commit(uint64_t event_id) {
    dispatcher_.post([filter = filter_, event_id]() {
      if (DynamicModuleListenerFilterSharedPtr filter_shared = filter.lock()) {
        filter_shared->onScheduled(event_id);
      }
    });
  }

private:
  // The filter that this scheduler is associated with. Using a weak pointer to avoid unnecessarily
  // extending the lifetime of the filter.
  DynamicModuleListenerFilterWeakPtr filter_;
  // The dispatcher is used to post the event to the worker thread that filter_ is assigned to.
  Event::Dispatcher& dispatcher_;
};

} // namespace ListenerFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
