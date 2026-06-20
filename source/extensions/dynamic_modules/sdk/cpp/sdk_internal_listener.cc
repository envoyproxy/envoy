#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "source/extensions/dynamic_modules/abi/abi.h"

#include "sdk_internal_common.h"
#include "sdk_listener.h"

namespace Envoy {
namespace DynamicModules {

namespace {

template <class T> T* unwrapPointer(const void* ptr) {
  return const_cast<T*>(static_cast<const T*>(ptr));
}

template <class T> void* wrapPointer(const T* ptr) {
  return reinterpret_cast<void*>(const_cast<T*>(ptr));
}

std::optional<std::string_view> bufferViewToOptionalStringView(const BufferView& value,
                                                               bool found) {
  if (!found) {
    return {};
  }
  return std::string_view(value.data() == nullptr ? "" : value.data(), value.size());
}

std::vector<std::string_view> bufferViewsToStringViews(const std::vector<BufferView>& views) {
  std::vector<std::string_view> result;
  result.reserve(views.size());
  for (const auto& view : views) {
    result.emplace_back(view.toStringView());
  }
  return result;
}

using ListenerSchedulerImpl =
    SchedulerImplBase<envoy_dynamic_module_callback_listener_filter_scheduler_new,
                      envoy_dynamic_module_callback_listener_filter_scheduler_commit,
                      envoy_dynamic_module_callback_listener_filter_scheduler_delete>;
using ListenerConfigSchedulerImpl =
    SchedulerImplBase<envoy_dynamic_module_callback_listener_filter_config_scheduler_new,
                      envoy_dynamic_module_callback_listener_filter_config_scheduler_commit,
                      envoy_dynamic_module_callback_listener_filter_config_scheduler_delete>;

class ListenerFilterHandleImpl : public ListenerFilterHandle {
public:
  explicit ListenerFilterHandleImpl(
      envoy_dynamic_module_type_listener_filter_envoy_ptr host_plugin_ptr)
      : host_plugin_ptr_(host_plugin_ptr) {}

  std::optional<BufferView> getBufferChunk() override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_listener_filter_get_buffer_chunk(
        host_plugin_ptr_, reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    if (!found) {
      return {};
    }
    return value;
  }

  bool drainBuffer(size_t length) override {
    return envoy_dynamic_module_callback_listener_filter_drain_buffer(host_plugin_ptr_, length);
  }

  void setDetectedTransportProtocol(std::string_view protocol) override {
    envoy_dynamic_module_callback_listener_filter_set_detected_transport_protocol(
        host_plugin_ptr_,
        envoy_dynamic_module_type_module_buffer{protocol.data(), protocol.size()});
  }

  void setRequestedServerName(std::string_view name) override {
    envoy_dynamic_module_callback_listener_filter_set_requested_server_name(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{name.data(), name.size()});
  }

  void setRequestedApplicationProtocols(std::span<const BufferView> protocols) override {
    envoy_dynamic_module_callback_listener_filter_set_requested_application_protocols(
        host_plugin_ptr_,
        reinterpret_cast<envoy_dynamic_module_type_module_buffer*>(
            const_cast<BufferView*>(protocols.data())),
        protocols.size());
  }

  void setJa3Hash(std::string_view hash) override {
    envoy_dynamic_module_callback_listener_filter_set_ja3_hash(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{hash.data(), hash.size()});
  }

  void setJa4Hash(std::string_view hash) override {
    envoy_dynamic_module_callback_listener_filter_set_ja4_hash(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{hash.data(), hash.size()});
  }

  std::optional<std::string_view> getRequestedServerName() override {
    return getStringValue(envoy_dynamic_module_callback_listener_filter_get_requested_server_name);
  }

  std::optional<std::string_view> getDetectedTransportProtocol() override {
    return getStringValue(
        envoy_dynamic_module_callback_listener_filter_get_detected_transport_protocol);
  }

  std::vector<std::string_view> getRequestedApplicationProtocols() override {
    return getStringVector(
        envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols_size,
        envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols);
  }

  std::optional<std::string_view> getJa3Hash() override {
    return getStringValue(envoy_dynamic_module_callback_listener_filter_get_ja3_hash);
  }

  std::optional<std::string_view> getJa4Hash() override {
    return getStringValue(envoy_dynamic_module_callback_listener_filter_get_ja4_hash);
  }

  bool isSsl() override {
    return envoy_dynamic_module_callback_listener_filter_is_ssl(host_plugin_ptr_);
  }

  std::vector<std::string_view> getSslUriSans() override {
    return getStringVector(envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans_size,
                           envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans);
  }

  std::vector<std::string_view> getSslDnsSans() override {
    return getStringVector(envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans_size,
                           envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans);
  }

  std::optional<std::string_view> getSslSubject() override {
    return getStringValue(envoy_dynamic_module_callback_listener_filter_get_ssl_subject);
  }

  std::optional<std::pair<std::string_view, uint32_t>> getRemoteAddress() override {
    return getAddress(envoy_dynamic_module_callback_listener_filter_get_remote_address);
  }

  std::optional<std::pair<std::string_view, uint32_t>> getDirectRemoteAddress() override {
    return getAddress(envoy_dynamic_module_callback_listener_filter_get_direct_remote_address);
  }

  std::optional<std::pair<std::string_view, uint32_t>> getLocalAddress() override {
    return getAddress(envoy_dynamic_module_callback_listener_filter_get_local_address);
  }

  std::optional<std::pair<std::string_view, uint32_t>> getDirectLocalAddress() override {
    return getAddress(envoy_dynamic_module_callback_listener_filter_get_direct_local_address);
  }

  std::optional<std::pair<std::string_view, uint32_t>> getOriginalDst() override {
    return getAddress(envoy_dynamic_module_callback_listener_filter_get_original_dst);
  }

  AddressType getAddressType() override {
    return static_cast<AddressType>(
        envoy_dynamic_module_callback_listener_filter_get_address_type(host_plugin_ptr_));
  }

  bool isLocalAddressRestored() override {
    return envoy_dynamic_module_callback_listener_filter_is_local_address_restored(
        host_plugin_ptr_);
  }

  bool setRemoteAddress(std::string_view address, uint32_t port, bool is_ipv6) override {
    return envoy_dynamic_module_callback_listener_filter_set_remote_address(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{address.data(), address.size()},
        port, is_ipv6);
  }

  bool restoreLocalAddress(std::string_view address, uint32_t port, bool is_ipv6) override {
    return envoy_dynamic_module_callback_listener_filter_restore_local_address(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{address.data(), address.size()},
        port, is_ipv6);
  }

  void continueFilterChain(bool success) override {
    envoy_dynamic_module_callback_listener_filter_continue_filter_chain(host_plugin_ptr_, success);
  }

  void useOriginalDst(bool use_original_dst) override {
    envoy_dynamic_module_callback_listener_filter_use_original_dst(host_plugin_ptr_,
                                                                   use_original_dst);
  }

  void closeSocket(std::string_view details) override {
    envoy_dynamic_module_callback_listener_filter_close_socket(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{details.data(), details.size()});
  }

  int64_t writeToSocket(std::string_view data) override {
    return envoy_dynamic_module_callback_listener_filter_write_to_socket(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{data.data(), data.size()});
  }

  int64_t getSocketFd() override {
    return envoy_dynamic_module_callback_listener_filter_get_socket_fd(host_plugin_ptr_);
  }

  bool setSocketOptionInt(int64_t level, int64_t name, int64_t value) override {
    return envoy_dynamic_module_callback_listener_filter_set_socket_option_int(host_plugin_ptr_,
                                                                               level, name, value);
  }

  bool setSocketOptionBytes(int64_t level, int64_t name, std::string_view value) override {
    return envoy_dynamic_module_callback_listener_filter_set_socket_option_bytes(
        host_plugin_ptr_, level, name,
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  std::optional<int64_t> getSocketOptionInt(int64_t level, int64_t name) override {
    int64_t value = 0;
    const bool found = envoy_dynamic_module_callback_listener_filter_get_socket_option_int(
        host_plugin_ptr_, level, name, &value);
    if (!found) {
      return {};
    }
    return value;
  }

  std::optional<std::vector<uint8_t>> getSocketOptionBytes(int64_t level, int64_t name,
                                                           size_t max_size) override {
    std::vector<uint8_t> value(max_size);
    size_t actual_size = 0;
    const bool found = envoy_dynamic_module_callback_listener_filter_get_socket_option_bytes(
        host_plugin_ptr_, level, name, reinterpret_cast<char*>(value.data()), value.size(),
        &actual_size);
    if (!found) {
      return {};
    }
    value.resize(actual_size);
    return value;
  }

  void setDynamicMetadataString(std::string_view ns, std::string_view key,
                                std::string_view value) override {
    envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  std::optional<std::string_view> getDynamicMetadataString(std::string_view ns,
                                                           std::string_view key) override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_string(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  void setDynamicMetadataNumber(std::string_view ns, std::string_view key, double value) override {
    envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_number(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, value);
  }

  std::optional<double> getDynamicMetadataNumber(std::string_view ns,
                                                 std::string_view key) override {
    double value = 0;
    const bool found = envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_number(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, &value);
    if (!found) {
      return {};
    }
    return value;
  }

  bool setFilterState(std::string_view key, std::string_view value) override {
    return envoy_dynamic_module_callback_listener_filter_set_filter_state(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  std::optional<std::string_view> getFilterState(std::string_view key) override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_listener_filter_get_filter_state(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  void setDownstreamTransportFailureReason(std::string_view reason) override {
    envoy_dynamic_module_callback_listener_filter_set_downstream_transport_failure_reason(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{reason.data(), reason.size()});
  }

  uint64_t getConnectionStartTimeMs() override {
    return envoy_dynamic_module_callback_listener_filter_get_connection_start_time_ms(
        host_plugin_ptr_);
  }

  size_t currentMaxReadBytes() override {
    return envoy_dynamic_module_callback_listener_filter_max_read_bytes(host_plugin_ptr_);
  }

  std::pair<HttpCalloutInitResult, uint64_t> httpCallout(std::string_view cluster,
                                                         std::span<const HeaderView> headers,
                                                         std::string_view body, uint64_t timeout_ms,
                                                         HttpCalloutCallback& cb) override {
    uint64_t callout_id = 0;
    const auto result = static_cast<HttpCalloutInitResult>(
        envoy_dynamic_module_callback_listener_filter_http_callout(
            host_plugin_ptr_, &callout_id,
            envoy_dynamic_module_type_module_buffer{cluster.data(), cluster.size()},
            reinterpret_cast<envoy_dynamic_module_type_module_http_header*>(
                const_cast<HeaderView*>(headers.data())),
            headers.size(), envoy_dynamic_module_type_module_buffer{body.data(), body.size()},
            timeout_ms));
    if (result == HttpCalloutInitResult::Success) {
      callout_callbacks_[callout_id] = &cb;
    }
    return {result, callout_id};
  }

  MetricsResult recordHistogramValue(MetricID id, uint64_t value) override {
    return static_cast<MetricsResult>(
        envoy_dynamic_module_callback_listener_filter_record_histogram_value(host_plugin_ptr_, id,
                                                                             value));
  }

  MetricsResult setGaugeValue(MetricID id, uint64_t value) override {
    return static_cast<MetricsResult>(
        envoy_dynamic_module_callback_listener_filter_set_gauge(host_plugin_ptr_, id, value));
  }

  MetricsResult incrementGaugeValue(MetricID id, uint64_t value) override {
    return static_cast<MetricsResult>(
        envoy_dynamic_module_callback_listener_filter_increment_gauge(host_plugin_ptr_, id, value));
  }

  MetricsResult decrementGaugeValue(MetricID id, uint64_t value) override {
    return static_cast<MetricsResult>(
        envoy_dynamic_module_callback_listener_filter_decrement_gauge(host_plugin_ptr_, id, value));
  }

  MetricsResult incrementCounterValue(MetricID id, uint64_t value) override {
    return static_cast<MetricsResult>(
        envoy_dynamic_module_callback_listener_filter_increment_counter(host_plugin_ptr_, id,
                                                                        value));
  }

  std::shared_ptr<Scheduler> getScheduler() override {
    if (!scheduler_) {
      scheduler_ = std::make_shared<ListenerSchedulerImpl>(host_plugin_ptr_);
    }
    return scheduler_;
  }

  uint32_t getWorkerIndex() override {
    return envoy_dynamic_module_callback_listener_filter_get_worker_index(host_plugin_ptr_);
  }

  bool logEnabled(LogLevel level) override {
    return envoy_dynamic_module_callback_log_enabled(
        static_cast<envoy_dynamic_module_type_log_level>(level));
  }

  void log(LogLevel level, std::string_view message) override {
    envoy_dynamic_module_callback_log(
        static_cast<envoy_dynamic_module_type_log_level>(level),
        envoy_dynamic_module_type_module_buffer{message.data(), message.size()});
  }

  std::unique_ptr<ListenerFilter> plugin_;
  std::shared_ptr<ListenerSchedulerImpl> scheduler_;
  std::map<uint64_t, HttpCalloutCallback*> callout_callbacks_;

private:
  using StringGetter = bool (*)(envoy_dynamic_module_type_listener_filter_envoy_ptr,
                                envoy_dynamic_module_type_envoy_buffer*);
  using SizeGetter = size_t (*)(envoy_dynamic_module_type_listener_filter_envoy_ptr);
  using VectorGetter = bool (*)(envoy_dynamic_module_type_listener_filter_envoy_ptr,
                                envoy_dynamic_module_type_envoy_buffer*);
  using AddressGetter = bool (*)(envoy_dynamic_module_type_listener_filter_envoy_ptr,
                                 envoy_dynamic_module_type_envoy_buffer*, uint32_t*);

  std::optional<std::string_view> getStringValue(StringGetter getter) {
    BufferView value{nullptr, 0};
    const bool found =
        getter(host_plugin_ptr_, reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  std::vector<std::string_view> getStringVector(SizeGetter size_getter, VectorGetter getter) {
    const size_t size = size_getter(host_plugin_ptr_);
    if (size == 0) {
      return {};
    }
    std::vector<BufferView> values(size);
    if (!getter(host_plugin_ptr_,
                reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(values.data()))) {
      return {};
    }
    return bufferViewsToStringViews(values);
  }

  std::optional<std::pair<std::string_view, uint32_t>> getAddress(AddressGetter getter) {
    BufferView value{nullptr, 0};
    uint32_t port = 0;
    const bool found = getter(
        host_plugin_ptr_, reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value), &port);
    if (!found) {
      return {};
    }
    return std::pair<std::string_view, uint32_t>{
        std::string_view(value.data() == nullptr ? "" : value.data(), value.size()), port};
  }

  const envoy_dynamic_module_type_listener_filter_envoy_ptr host_plugin_ptr_ = nullptr;
};

class ListenerFilterConfigHandleImpl : public ListenerFilterConfigHandle {
public:
  explicit ListenerFilterConfigHandleImpl(
      envoy_dynamic_module_type_listener_filter_config_envoy_ptr host_config_ptr)
      : host_config_ptr_(host_config_ptr) {}

  std::pair<MetricID, MetricsResult> defineHistogram(std::string_view name) override {
    size_t metric_id = 0;
    const auto result = static_cast<MetricsResult>(
        envoy_dynamic_module_callback_listener_filter_config_define_histogram(
            host_config_ptr_, envoy_dynamic_module_type_module_buffer{name.data(), name.size()},
            &metric_id));
    return {metric_id, result};
  }

  std::pair<MetricID, MetricsResult> defineGauge(std::string_view name) override {
    size_t metric_id = 0;
    const auto result = static_cast<MetricsResult>(
        envoy_dynamic_module_callback_listener_filter_config_define_gauge(
            host_config_ptr_, envoy_dynamic_module_type_module_buffer{name.data(), name.size()},
            &metric_id));
    return {metric_id, result};
  }

  std::pair<MetricID, MetricsResult> defineCounter(std::string_view name) override {
    size_t metric_id = 0;
    const auto result = static_cast<MetricsResult>(
        envoy_dynamic_module_callback_listener_filter_config_define_counter(
            host_config_ptr_, envoy_dynamic_module_type_module_buffer{name.data(), name.size()},
            &metric_id));
    return {metric_id, result};
  }

  std::shared_ptr<Scheduler> getScheduler() override {
    if (!scheduler_) {
      scheduler_ = std::make_shared<ListenerConfigSchedulerImpl>(host_config_ptr_);
    }
    return scheduler_;
  }

  bool logEnabled(LogLevel level) override {
    return envoy_dynamic_module_callback_log_enabled(
        static_cast<envoy_dynamic_module_type_log_level>(level));
  }

  void log(LogLevel level, std::string_view message) override {
    envoy_dynamic_module_callback_log(
        static_cast<envoy_dynamic_module_type_log_level>(level),
        envoy_dynamic_module_type_module_buffer{message.data(), message.size()});
  }

  std::shared_ptr<ListenerConfigSchedulerImpl> scheduler_;

private:
  envoy_dynamic_module_type_listener_filter_config_envoy_ptr host_config_ptr_ = nullptr;
};

struct ListenerFilterFactoryWrapper {
  std::unique_ptr<ListenerFilterConfigHandleImpl> config_handle_;
  std::unique_ptr<ListenerFilterFactory> factory_;
};

} // namespace

extern "C" {

envoy_dynamic_module_type_listener_filter_config_module_ptr
envoy_dynamic_module_on_listener_filter_config_new(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  auto config_handle = std::make_unique<ListenerFilterConfigHandleImpl>(filter_config_envoy_ptr);
  const std::string_view name_view(name.ptr, name.length);
  const std::string_view config_view(config.ptr, config.length);

  auto config_factory = ListenerFilterConfigFactoryRegistry::getRegistry().find(name_view);
  if (config_factory == ListenerFilterConfigFactoryRegistry::getRegistry().end()) {
    DYM_LOG((*config_handle), LogLevel::Warn,
            "Listener plugin config factory not found for name: {}", name_view);
    return nullptr;
  }

  auto plugin_factory = config_factory->second->create(*config_handle, config_view);
  if (!plugin_factory) {
    DYM_LOG((*config_handle), LogLevel::Warn,
            "Failed to create listener plugin factory for name: {}", name_view);
    return nullptr;
  }

  auto factory = std::make_unique<ListenerFilterFactoryWrapper>();
  factory->config_handle_ = std::move(config_handle);
  factory->factory_ = std::move(plugin_factory);
  return wrapPointer(factory.release());
}

void envoy_dynamic_module_on_listener_filter_config_destroy(
    envoy_dynamic_module_type_listener_filter_config_module_ptr filter_config_ptr) {
  auto* factory_wrapper = unwrapPointer<ListenerFilterFactoryWrapper>(filter_config_ptr);
  if (factory_wrapper == nullptr) {
    return;
  }
  if (factory_wrapper->factory_) {
    factory_wrapper->factory_->onDestroy();
  }
  delete factory_wrapper;
}

envoy_dynamic_module_type_listener_filter_module_ptr envoy_dynamic_module_on_listener_filter_new(
    envoy_dynamic_module_type_listener_filter_config_module_ptr filter_config_ptr,
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr) {
  auto* factory_wrapper = unwrapPointer<ListenerFilterFactoryWrapper>(filter_config_ptr);
  if (factory_wrapper == nullptr) {
    return nullptr;
  }

  auto plugin_handle = std::make_unique<ListenerFilterHandleImpl>(filter_envoy_ptr);
  auto plugin = factory_wrapper->factory_->create(*plugin_handle);
  if (!plugin) {
    DYM_LOG((*plugin_handle), LogLevel::Warn, "Failed to create listener plugin instance");
    return nullptr;
  }
  plugin_handle->plugin_ = std::move(plugin);
  return wrapPointer(plugin_handle.release());
}

envoy_dynamic_module_type_on_listener_filter_status
envoy_dynamic_module_on_listener_filter_on_accept(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr) {
  static_cast<void>(filter_envoy_ptr);
  auto* plugin_handle = unwrapPointer<ListenerFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle) {
    return envoy_dynamic_module_type_on_listener_filter_status_Continue;
  }
  return static_cast<envoy_dynamic_module_type_on_listener_filter_status>(
      plugin_handle->plugin_->onAccept());
}

envoy_dynamic_module_type_on_listener_filter_status envoy_dynamic_module_on_listener_filter_on_data(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr, size_t data_length) {
  static_cast<void>(filter_envoy_ptr);
  auto* plugin_handle = unwrapPointer<ListenerFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle) {
    return envoy_dynamic_module_type_on_listener_filter_status_Continue;
  }
  return static_cast<envoy_dynamic_module_type_on_listener_filter_status>(
      plugin_handle->plugin_->onData(data_length));
}

void envoy_dynamic_module_on_listener_filter_on_close(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr) {
  static_cast<void>(filter_envoy_ptr);
  auto* plugin_handle = unwrapPointer<ListenerFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle) {
    return;
  }
  plugin_handle->plugin_->onClose();
}

size_t envoy_dynamic_module_on_listener_filter_get_max_read_bytes(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr) {
  static_cast<void>(filter_envoy_ptr);
  auto* plugin_handle = unwrapPointer<ListenerFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle) {
    return 0;
  }
  return plugin_handle->plugin_->maxReadBytes();
}

void envoy_dynamic_module_on_listener_filter_destroy(
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr) {
  auto* plugin_handle = unwrapPointer<ListenerFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle) {
    return;
  }
  plugin_handle->plugin_->onDestroy();
  delete plugin_handle;
}

void envoy_dynamic_module_on_listener_filter_http_callout_done(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr, uint64_t callout_id,
    envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_chunks, size_t body_chunks_size) {
  static_cast<void>(filter_envoy_ptr);
  auto* plugin_handle = unwrapPointer<ListenerFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle) {
    return;
  }

  auto it = plugin_handle->callout_callbacks_.find(callout_id);
  if (it == plugin_handle->callout_callbacks_.end()) {
    return;
  }

  auto* callback = it->second;
  plugin_handle->callout_callbacks_.erase(it);
  callback->onHttpCalloutDone(static_cast<HttpCalloutResult>(result),
                              {reinterpret_cast<HeaderView*>(headers), headers_size},
                              {reinterpret_cast<BufferView*>(body_chunks), body_chunks_size});
}

void envoy_dynamic_module_on_listener_filter_scheduled(
    envoy_dynamic_module_type_listener_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_module_ptr filter_module_ptr, uint64_t event_id) {
  static_cast<void>(filter_envoy_ptr);
  auto* plugin_handle = unwrapPointer<ListenerFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle || !plugin_handle->scheduler_) {
    return;
  }
  plugin_handle->scheduler_->onScheduled(event_id);
}

void envoy_dynamic_module_on_listener_filter_config_scheduled(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_listener_filter_config_module_ptr filter_config_ptr,
    uint64_t event_id) {
  static_cast<void>(filter_config_envoy_ptr);
  auto* factory_wrapper = unwrapPointer<ListenerFilterFactoryWrapper>(filter_config_ptr);
  if (factory_wrapper == nullptr || factory_wrapper->config_handle_ == nullptr ||
      factory_wrapper->config_handle_->scheduler_ == nullptr) {
    return;
  }
  factory_wrapper->config_handle_->scheduler_->onScheduled(event_id);
}

} // extern "C"

} // namespace DynamicModules
} // namespace Envoy
