#pragma once

#include <string>
#include <vector>

#include "envoy/http/async_client.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/filters/network/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace NetworkFilters {

class DynamicModuleNetworkFilter;
using DynamicModuleNetworkFilterSharedPtr = std::shared_ptr<DynamicModuleNetworkFilter>;
using DynamicModuleNetworkFilterWeakPtr = std::weak_ptr<DynamicModuleNetworkFilter>;

/**
 * A network filter that uses a dynamic module. Corresponds to a single TCP connection.
 */
class DynamicModuleNetworkFilter : public Network::Filter,
                                   public Network::ConnectionCallbacks,
                                   public std::enable_shared_from_this<DynamicModuleNetworkFilter>,
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

  /**
   * Sends an HTTP callout to the specified cluster with the given message.
   */
  envoy_dynamic_module_type_http_callout_init_result
  sendHttpCallout(uint64_t* callout_id_out, absl::string_view cluster_name,
                  Http::RequestMessagePtr&& message, uint64_t timeout_milliseconds);

  /**
   * Store an integer socket option for the current connection and Surface it back to modules.
   */
  void storeSocketOptionInt(int64_t level, int64_t name,
                            envoy_dynamic_module_type_socket_option_state state, int64_t value);

  /**
   * Store a bytes socket option for the current connection and Surface it back to modules.
   */
  void storeSocketOptionBytes(int64_t level, int64_t name,
                              envoy_dynamic_module_type_socket_option_state state,
                              absl::string_view value);

  /**
   * Retrieve an integer socket option by level/name/state.
   */
  bool tryGetSocketOptionInt(int64_t level, int64_t name,
                             envoy_dynamic_module_type_socket_option_state state,
                             int64_t& value_out) const;

  /**
   * Retrieve a bytes socket option by level/name/state.
   */
  bool tryGetSocketOptionBytes(int64_t level, int64_t name,
                               envoy_dynamic_module_type_socket_option_state state,
                               absl::string_view& value_out) const;

  /**
   * Number of socket options stored for this connection.
   */
  size_t socketOptionCount() const { return socket_options_.size(); }

  /**
   * Fill provided buffer with stored socket options up to options_size.
   */
  void copySocketOptions(envoy_dynamic_module_type_socket_option* options_out, size_t options_size,
                         size_t& options_written) const;

  /**
   * This is called when an event is scheduled via DynamicModuleNetworkFilterScheduler.
   */
  void onScheduled(uint64_t event_id);

  /**
   * Get the dispatcher for the worker thread this filter is running on.
   * Returns nullptr if callbacks are not set.
   */
  Event::Dispatcher* dispatcher() {
    return read_callbacks_ != nullptr ? &read_callbacks_->connection().dispatcher() : nullptr;
  }

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

  /**
   * This implementation of the AsyncClient::Callbacks is used to handle the response from the HTTP
   * callout from the parent network filter.
   */
  class HttpCalloutCallback : public Http::AsyncClient::Callbacks {
  public:
    HttpCalloutCallback(std::shared_ptr<DynamicModuleNetworkFilter> filter, uint64_t id)
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
    const std::weak_ptr<DynamicModuleNetworkFilter> filter_;
    const uint64_t callout_id_{};
  };

  uint64_t getNextCalloutId() { return next_callout_id_++; }

  uint64_t next_callout_id_ = 1; // 0 is reserved as an invalid id.

  absl::flat_hash_map<uint64_t, std::unique_ptr<DynamicModuleNetworkFilter::HttpCalloutCallback>>
      http_callouts_;

  struct StoredSocketOption {
    int64_t level;
    int64_t name;
    envoy_dynamic_module_type_socket_option_state state;
    bool is_int;
    int64_t int_value;
    std::string byte_value;
  };

  std::vector<StoredSocketOption> socket_options_;
};

/**
 * This class is used to schedule a network filter event hook from a different thread
 * than the one it was assigned to. This is created via
 * envoy_dynamic_module_callback_network_filter_scheduler_new and deleted via
 * envoy_dynamic_module_callback_network_filter_scheduler_delete.
 */
class DynamicModuleNetworkFilterScheduler {
public:
  DynamicModuleNetworkFilterScheduler(DynamicModuleNetworkFilterWeakPtr filter,
                                      Event::Dispatcher& dispatcher)
      : filter_(std::move(filter)), dispatcher_(dispatcher) {}

  void commit(uint64_t event_id) {
    dispatcher_.post([filter = filter_, event_id]() {
      if (DynamicModuleNetworkFilterSharedPtr filter_shared = filter.lock()) {
        filter_shared->onScheduled(event_id);
      }
    });
  }

private:
  // The filter that this scheduler is associated with. Using a weak pointer to avoid unnecessarily
  // extending the lifetime of the filter.
  DynamicModuleNetworkFilterWeakPtr filter_;
  // The dispatcher is used to post the event to the worker thread that filter_ is assigned to.
  Event::Dispatcher& dispatcher_;
};

} // namespace NetworkFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
