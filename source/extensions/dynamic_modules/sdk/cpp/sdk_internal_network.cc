#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "source/extensions/dynamic_modules/abi/abi.h"

#include "sdk.h"
#include "sdk_internal_common.h"
#include "sdk_network.h"

namespace Envoy {
namespace DynamicModules {
namespace {

template <class T> T* unwrapPointer(const void* ptr) {
  return const_cast<T*>(reinterpret_cast<const T*>(ptr));
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

using NetworkSchedulerImpl =
    SchedulerImplBase<envoy_dynamic_module_callback_network_filter_scheduler_new,
                      envoy_dynamic_module_callback_network_filter_scheduler_commit,
                      envoy_dynamic_module_callback_network_filter_scheduler_delete>;
using NetworkConfigSchedulerImpl =
    SchedulerImplBase<envoy_dynamic_module_callback_network_filter_config_scheduler_new,
                      envoy_dynamic_module_callback_network_filter_config_scheduler_commit,
                      envoy_dynamic_module_callback_network_filter_config_scheduler_delete>;

template <bool IsReadBuffer> class NetworkBufferImpl : public NetworkBuffer {
public:
  explicit NetworkBufferImpl(envoy_dynamic_module_type_network_filter_envoy_ptr host_plugin_ptr)
      : host_plugin_ptr_(host_plugin_ptr) {}

  std::vector<BufferView> getChunks() const override {
    const size_t chunks_size = getChunksSize();
    if (chunks_size == 0) {
      return {};
    }

    std::vector<BufferView> result_chunks(chunks_size);
    const bool ok = getChunksImpl(
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(result_chunks.data()));
    if (!ok) {
      return {};
    }
    return result_chunks;
  }

  size_t getSize() const override {
    if constexpr (IsReadBuffer) {
      return envoy_dynamic_module_callback_network_filter_get_read_buffer_size(host_plugin_ptr_);
    } else {
      return envoy_dynamic_module_callback_network_filter_get_write_buffer_size(host_plugin_ptr_);
    }
  }

  bool drain(size_t size) override {
    if constexpr (IsReadBuffer) {
      return envoy_dynamic_module_callback_network_filter_drain_read_buffer(host_plugin_ptr_, size);
    } else {
      return envoy_dynamic_module_callback_network_filter_drain_write_buffer(host_plugin_ptr_,
                                                                             size);
    }
  }

  bool prepend(std::string_view data) override {
    const envoy_dynamic_module_type_module_buffer buffer{data.data(), data.size()};
    if constexpr (IsReadBuffer) {
      return envoy_dynamic_module_callback_network_filter_prepend_read_buffer(host_plugin_ptr_,
                                                                              buffer);
    } else {
      return envoy_dynamic_module_callback_network_filter_prepend_write_buffer(host_plugin_ptr_,
                                                                               buffer);
    }
  }

  bool append(std::string_view data) override {
    const envoy_dynamic_module_type_module_buffer buffer{data.data(), data.size()};
    if constexpr (IsReadBuffer) {
      return envoy_dynamic_module_callback_network_filter_append_read_buffer(host_plugin_ptr_,
                                                                             buffer);
    } else {
      return envoy_dynamic_module_callback_network_filter_append_write_buffer(host_plugin_ptr_,
                                                                              buffer);
    }
  }

private:
  size_t getChunksSize() const {
    if constexpr (IsReadBuffer) {
      return envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size(
          host_plugin_ptr_);
    } else {
      return envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size(
          host_plugin_ptr_);
    }
  }

  bool getChunksImpl(envoy_dynamic_module_type_envoy_buffer* result_chunks) const {
    if constexpr (IsReadBuffer) {
      return envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks(host_plugin_ptr_,
                                                                                 result_chunks);
    } else {
      return envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks(host_plugin_ptr_,
                                                                                  result_chunks);
    }
  }

  envoy_dynamic_module_type_network_filter_envoy_ptr host_plugin_ptr_;
};

using ReadBufferImpl = NetworkBufferImpl<true>;
using WriteBufferImpl = NetworkBufferImpl<false>;

class NetworkFilterHandleImpl : public NetworkFilterHandle {
public:
  explicit NetworkFilterHandleImpl(
      envoy_dynamic_module_type_network_filter_envoy_ptr host_plugin_ptr)
      : host_plugin_ptr_(host_plugin_ptr), read_buffer_(host_plugin_ptr),
        write_buffer_(host_plugin_ptr) {}

  NetworkBuffer& readBuffer() override { return read_buffer_; }
  NetworkBuffer& writeBuffer() override { return write_buffer_; }

  void write(std::string_view data, bool end_stream) override {
    envoy_dynamic_module_callback_network_filter_write(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{data.data(), data.size()},
        end_stream);
  }

  void injectReadData(std::string_view data, bool end_stream) override {
    envoy_dynamic_module_callback_network_filter_inject_read_data(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{data.data(), data.size()},
        end_stream);
  }

  void injectWriteData(std::string_view data, bool end_stream) override {
    envoy_dynamic_module_callback_network_filter_inject_write_data(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{data.data(), data.size()},
        end_stream);
  }

  void continueReading() override {
    envoy_dynamic_module_callback_network_filter_continue_reading(host_plugin_ptr_);
  }

  void close(NetworkConnectionCloseType close_type) override {
    envoy_dynamic_module_callback_network_filter_close(
        host_plugin_ptr_,
        static_cast<envoy_dynamic_module_type_network_connection_close_type>(close_type));
  }

  uint64_t getConnectionId() override {
    return envoy_dynamic_module_callback_network_filter_get_connection_id(host_plugin_ptr_);
  }

  std::optional<std::pair<std::string_view, uint32_t>> getRemoteAddress() override {
    return getAddress(envoy_dynamic_module_callback_network_filter_get_remote_address);
  }

  std::optional<std::pair<std::string_view, uint32_t>> getLocalAddress() override {
    return getAddress(envoy_dynamic_module_callback_network_filter_get_local_address);
  }

  bool isSsl() override {
    return envoy_dynamic_module_callback_network_filter_is_ssl(host_plugin_ptr_);
  }

  void disableClose(bool disabled) override {
    envoy_dynamic_module_callback_network_filter_disable_close(host_plugin_ptr_, disabled);
  }

  void closeWithDetails(NetworkConnectionCloseType close_type, std::string_view details) override {
    envoy_dynamic_module_callback_network_filter_close_with_details(
        host_plugin_ptr_,
        static_cast<envoy_dynamic_module_type_network_connection_close_type>(close_type),
        envoy_dynamic_module_type_module_buffer{details.data(), details.size()});
  }

  std::optional<std::string_view> getRequestedServerName() override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_network_filter_get_requested_server_name(
        host_plugin_ptr_, reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  std::optional<std::pair<std::string_view, uint32_t>> getDirectRemoteAddress() override {
    return getAddress(envoy_dynamic_module_callback_network_filter_get_direct_remote_address);
  }

  std::vector<std::string_view> getSslUriSans() override {
    return getSans(envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans_size,
                   envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans);
  }

  std::vector<std::string_view> getSslDnsSans() override {
    return getSans(envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans_size,
                   envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans);
  }

  std::optional<std::string_view> getSslSubject() override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_network_filter_get_ssl_subject(
        host_plugin_ptr_, reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  bool setFilterState(std::string_view key, std::string_view value) override {
    return envoy_dynamic_module_callback_network_set_filter_state_bytes(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  std::optional<std::string_view> getFilterState(std::string_view key) override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_network_get_filter_state_bytes(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  bool setFilterStateTyped(std::string_view key, std::string_view value) override {
    return envoy_dynamic_module_callback_network_set_filter_state_typed(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  std::optional<std::string_view> getFilterStateTyped(std::string_view key) override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_network_get_filter_state_typed(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  std::optional<std::string_view> getMetadataString(std::string_view ns,
                                                    std::string_view key) override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_network_get_dynamic_metadata_string(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  std::optional<double> getMetadataNumber(std::string_view ns, std::string_view key) override {
    double value = 0.0;
    const bool found = envoy_dynamic_module_callback_network_get_dynamic_metadata_number(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, &value);
    if (!found) {
      return {};
    }
    return value;
  }

  std::optional<bool> getMetadataBool(std::string_view ns, std::string_view key) override {
    bool value = false;
    const bool found = envoy_dynamic_module_callback_network_get_dynamic_metadata_bool(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, &value);
    if (!found) {
      return {};
    }
    return value;
  }

  void setMetadata(std::string_view ns, std::string_view key, std::string_view value) override {
    envoy_dynamic_module_callback_network_set_dynamic_metadata_string(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  void setMetadata(std::string_view ns, std::string_view key, double value) override {
    envoy_dynamic_module_callback_network_set_dynamic_metadata_number(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, value);
  }

  void setMetadata(std::string_view ns, std::string_view key, bool value) override {
    envoy_dynamic_module_callback_network_set_dynamic_metadata_bool(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, value);
  }

  void setSocketOptionInt(int64_t level, int64_t name, SocketOptionState state,
                          int64_t value) override {
    envoy_dynamic_module_callback_network_set_socket_option_int(
        host_plugin_ptr_, level, name,
        static_cast<envoy_dynamic_module_type_socket_option_state>(state), value);
  }

  void setSocketOptionBytes(int64_t level, int64_t name, SocketOptionState state,
                            std::string_view value) override {
    envoy_dynamic_module_callback_network_set_socket_option_bytes(
        host_plugin_ptr_, level, name,
        static_cast<envoy_dynamic_module_type_socket_option_state>(state),
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  std::optional<int64_t> getSocketOptionInt(int64_t level, int64_t name,
                                            SocketOptionState state) override {
    int64_t value = 0;
    const bool found = envoy_dynamic_module_callback_network_get_socket_option_int(
        host_plugin_ptr_, level, name,
        static_cast<envoy_dynamic_module_type_socket_option_state>(state), &value);
    if (!found) {
      return {};
    }
    return value;
  }

  std::optional<std::string_view> getSocketOptionBytes(int64_t level, int64_t name,
                                                       SocketOptionState state) override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_network_get_socket_option_bytes(
        host_plugin_ptr_, level, name,
        static_cast<envoy_dynamic_module_type_socket_option_state>(state),
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  std::vector<SocketOption> getSocketOptions() override {
    const size_t option_count =
        envoy_dynamic_module_callback_network_get_socket_options_size(host_plugin_ptr_);
    if (option_count == 0) {
      return {};
    }
    std::vector<envoy_dynamic_module_type_socket_option> raw_options(option_count);
    envoy_dynamic_module_callback_network_get_socket_options(host_plugin_ptr_, raw_options.data());

    std::vector<SocketOption> options;
    options.reserve(option_count);
    for (const auto& raw_option : raw_options) {
      options.push_back(SocketOption{
          raw_option.level,
          raw_option.name,
          static_cast<SocketOptionState>(raw_option.state),
          static_cast<SocketOptionValueType>(raw_option.value_type),
          raw_option.int_value,
          BufferView{raw_option.byte_value.ptr, raw_option.byte_value.length},
      });
    }
    return options;
  }

  std::pair<HttpCalloutInitResult, uint64_t> httpCallout(std::string_view cluster,
                                                         std::span<const HeaderView> headers,
                                                         std::string_view body, uint64_t timeout_ms,
                                                         HttpCalloutCallback& cb) override {
    uint64_t callout_id_out = 0;
    const auto result = envoy_dynamic_module_callback_network_filter_http_callout(
        host_plugin_ptr_, &callout_id_out,
        envoy_dynamic_module_type_module_buffer{cluster.data(), cluster.size()},
        const_cast<envoy_dynamic_module_type_module_http_header*>(
            reinterpret_cast<const envoy_dynamic_module_type_module_http_header*>(headers.data())),
        headers.size(), envoy_dynamic_module_type_module_buffer{body.data(), body.size()},
        timeout_ms);

    if (result == envoy_dynamic_module_type_http_callout_init_result_Success) {
      callout_callbacks_[callout_id_out] = &cb;
    }
    return {static_cast<HttpCalloutInitResult>(result), callout_id_out};
  }

  MetricsResult recordHistogramValue(MetricID id, uint64_t value) override {
    return static_cast<MetricsResult>(
        envoy_dynamic_module_callback_network_filter_record_histogram_value(host_plugin_ptr_, id,
                                                                            value));
  }

  MetricsResult setGaugeValue(MetricID id, uint64_t value) override {
    return static_cast<MetricsResult>(
        envoy_dynamic_module_callback_network_filter_set_gauge(host_plugin_ptr_, id, value));
  }

  MetricsResult incrementGaugeValue(MetricID id, uint64_t value) override {
    return static_cast<MetricsResult>(
        envoy_dynamic_module_callback_network_filter_increment_gauge(host_plugin_ptr_, id, value));
  }

  MetricsResult decrementGaugeValue(MetricID id, uint64_t value) override {
    return static_cast<MetricsResult>(
        envoy_dynamic_module_callback_network_filter_decrement_gauge(host_plugin_ptr_, id, value));
  }

  MetricsResult incrementCounterValue(MetricID id, uint64_t value) override {
    return static_cast<MetricsResult>(
        envoy_dynamic_module_callback_network_filter_increment_counter(host_plugin_ptr_, id,
                                                                       value));
  }

  std::optional<ClusterHostCounts> getClusterHostCounts(std::string_view cluster,
                                                        uint32_t priority) override {
    size_t total = 0;
    size_t healthy = 0;
    size_t degraded = 0;
    const bool found = envoy_dynamic_module_callback_network_filter_get_cluster_host_count(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{cluster.data(), cluster.size()},
        priority, &total, &healthy, &degraded);
    if (!found) {
      return {};
    }
    return ClusterHostCounts{static_cast<uint64_t>(total), static_cast<uint64_t>(healthy),
                             static_cast<uint64_t>(degraded)};
  }

  std::optional<std::pair<std::string_view, uint32_t>> getUpstreamHostAddress() override {
    return getAddress(envoy_dynamic_module_callback_network_filter_get_upstream_host_address);
  }

  std::optional<std::string_view> getUpstreamHostHostname() override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_network_filter_get_upstream_host_hostname(
        host_plugin_ptr_, reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  std::optional<std::string_view> getUpstreamHostCluster() override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_network_filter_get_upstream_host_cluster(
        host_plugin_ptr_, reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  bool hasUpstreamHost() override {
    return envoy_dynamic_module_callback_network_filter_has_upstream_host(host_plugin_ptr_);
  }

  bool startUpstreamSecureTransport() override {
    return envoy_dynamic_module_callback_network_filter_start_upstream_secure_transport(
        host_plugin_ptr_);
  }

  NetworkConnectionState getConnectionState() override {
    return static_cast<NetworkConnectionState>(
        envoy_dynamic_module_callback_network_filter_get_connection_state(host_plugin_ptr_));
  }

  NetworkReadDisableStatus readDisable(bool disable) override {
    return static_cast<NetworkReadDisableStatus>(
        envoy_dynamic_module_callback_network_filter_read_disable(host_plugin_ptr_, disable));
  }

  bool readEnabled() override {
    return envoy_dynamic_module_callback_network_filter_read_enabled(host_plugin_ptr_);
  }

  bool isHalfCloseEnabled() override {
    return envoy_dynamic_module_callback_network_filter_is_half_close_enabled(host_plugin_ptr_);
  }

  void enableHalfClose(bool enabled) override {
    envoy_dynamic_module_callback_network_filter_enable_half_close(host_plugin_ptr_, enabled);
  }

  uint32_t getBufferLimit() override {
    return envoy_dynamic_module_callback_network_filter_get_buffer_limit(host_plugin_ptr_);
  }

  void setBufferLimits(uint32_t limit) override {
    envoy_dynamic_module_callback_network_filter_set_buffer_limits(host_plugin_ptr_, limit);
  }

  bool aboveHighWatermark() override {
    return envoy_dynamic_module_callback_network_filter_above_high_watermark(host_plugin_ptr_);
  }

  std::shared_ptr<Scheduler> getScheduler() override {
    if (!scheduler_) {
      scheduler_ = std::make_shared<NetworkSchedulerImpl>(host_plugin_ptr_);
    }
    return scheduler_;
  }

  uint32_t getWorkerIndex() override {
    return envoy_dynamic_module_callback_network_filter_get_worker_index(host_plugin_ptr_);
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

  std::unique_ptr<NetworkFilter> plugin_;
  std::map<uint64_t, HttpCalloutCallback*> callout_callbacks_;
  std::shared_ptr<NetworkSchedulerImpl> scheduler_;

private:
  using AddressGetter = bool (*)(envoy_dynamic_module_type_network_filter_envoy_ptr,
                                 envoy_dynamic_module_type_envoy_buffer*, uint32_t*);

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

  using SansSizeGetter = size_t (*)(envoy_dynamic_module_type_network_filter_envoy_ptr);
  using SansGetter = bool (*)(envoy_dynamic_module_type_network_filter_envoy_ptr,
                              envoy_dynamic_module_type_envoy_buffer*);

  std::vector<std::string_view> getSans(SansSizeGetter size_getter, SansGetter getter) {
    const size_t count = size_getter(host_plugin_ptr_);
    if (count == 0) {
      return {};
    }
    std::vector<envoy_dynamic_module_type_envoy_buffer> buffers(count);
    const bool found = getter(host_plugin_ptr_, buffers.data());
    if (!found) {
      return {};
    }
    std::vector<std::string_view> result;
    result.reserve(count);
    for (const auto& buffer : buffers) {
      result.emplace_back(buffer.ptr == nullptr ? "" : buffer.ptr, buffer.length);
    }
    return result;
  }

  envoy_dynamic_module_type_network_filter_envoy_ptr host_plugin_ptr_;
  ReadBufferImpl read_buffer_;
  WriteBufferImpl write_buffer_;
};

class NetworkFilterConfigHandleImpl : public NetworkFilterConfigHandle {
public:
  explicit NetworkFilterConfigHandleImpl(
      envoy_dynamic_module_type_network_filter_config_envoy_ptr host_config_ptr)
      : host_config_ptr_(host_config_ptr) {}

  std::pair<MetricID, MetricsResult> defineHistogram(std::string_view name) override {
    size_t metric_id = 0;
    const auto result = static_cast<MetricsResult>(
        envoy_dynamic_module_callback_network_filter_config_define_histogram(
            host_config_ptr_, envoy_dynamic_module_type_module_buffer{name.data(), name.size()},
            &metric_id));
    return {metric_id, result};
  }

  std::pair<MetricID, MetricsResult> defineGauge(std::string_view name) override {
    size_t metric_id = 0;
    const auto result =
        static_cast<MetricsResult>(envoy_dynamic_module_callback_network_filter_config_define_gauge(
            host_config_ptr_, envoy_dynamic_module_type_module_buffer{name.data(), name.size()},
            &metric_id));
    return {metric_id, result};
  }

  std::pair<MetricID, MetricsResult> defineCounter(std::string_view name) override {
    size_t metric_id = 0;
    const auto result = static_cast<MetricsResult>(
        envoy_dynamic_module_callback_network_filter_config_define_counter(
            host_config_ptr_, envoy_dynamic_module_type_module_buffer{name.data(), name.size()},
            &metric_id));
    return {metric_id, result};
  }

  std::shared_ptr<Scheduler> getScheduler() override {
    if (!scheduler_) {
      scheduler_ = std::make_shared<NetworkConfigSchedulerImpl>(host_config_ptr_);
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

  std::shared_ptr<NetworkConfigSchedulerImpl> scheduler_;

private:
  envoy_dynamic_module_type_network_filter_config_envoy_ptr host_config_ptr_;
};

struct NetworkFilterFactoryWrapper {
  std::unique_ptr<NetworkFilterConfigHandleImpl> config_handle_;
  std::unique_ptr<NetworkFilterFactory> factory_;
};

} // namespace

extern "C" {

envoy_dynamic_module_type_network_filter_config_module_ptr
envoy_dynamic_module_on_network_filter_config_new(
    envoy_dynamic_module_type_network_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  auto config_handle = std::make_unique<NetworkFilterConfigHandleImpl>(filter_config_envoy_ptr);
  const std::string_view name_view(name.ptr, name.length);
  const std::string_view config_view(config.ptr, config.length);

  auto config_factory = NetworkFilterConfigFactoryRegistry::getRegistry().find(name_view);
  if (config_factory == NetworkFilterConfigFactoryRegistry::getRegistry().end()) {
    DYM_LOG((*config_handle), LogLevel::Warn,
            "Network plugin config factory not found for name: {}", name_view);
    return nullptr;
  }

  auto plugin_factory = config_factory->second->create(*config_handle, config_view);
  if (!plugin_factory) {
    DYM_LOG((*config_handle), LogLevel::Warn,
            "Failed to create network plugin factory for name: {}", name_view);
    return nullptr;
  }

  auto factory = std::make_unique<NetworkFilterFactoryWrapper>();
  factory->config_handle_ = std::move(config_handle);
  factory->factory_ = std::move(plugin_factory);
  return wrapPointer(factory.release());
}

void envoy_dynamic_module_on_network_filter_config_destroy(
    envoy_dynamic_module_type_network_filter_config_module_ptr filter_config_ptr) {
  auto* factory_wrapper = unwrapPointer<NetworkFilterFactoryWrapper>(filter_config_ptr);
  if (factory_wrapper == nullptr) {
    return;
  }
  if (factory_wrapper->factory_) {
    factory_wrapper->factory_->onDestroy();
  }
  delete factory_wrapper;
}

envoy_dynamic_module_type_network_filter_module_ptr envoy_dynamic_module_on_network_filter_new(
    envoy_dynamic_module_type_network_filter_config_module_ptr filter_config_ptr,
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr) {
  auto* factory_wrapper = unwrapPointer<NetworkFilterFactoryWrapper>(filter_config_ptr);
  if (factory_wrapper == nullptr) {
    return nullptr;
  }

  auto plugin_handle = std::make_unique<NetworkFilterHandleImpl>(filter_envoy_ptr);
  auto plugin = factory_wrapper->factory_->create(*plugin_handle);
  if (plugin == nullptr) {
    DYM_LOG((*plugin_handle), LogLevel::Warn, "Failed to create network plugin instance");
    return nullptr;
  }
  plugin_handle->plugin_ = std::move(plugin);
  return wrapPointer(plugin_handle.release());
}

envoy_dynamic_module_type_on_network_filter_data_status
envoy_dynamic_module_on_network_filter_new_connection(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr) {
  static_cast<void>(filter_envoy_ptr);
  auto* plugin_handle = unwrapPointer<NetworkFilterHandleImpl>(filter_module_ptr);
  if (plugin_handle == nullptr) {
    return envoy_dynamic_module_type_on_network_filter_data_status_Continue;
  }
  return static_cast<envoy_dynamic_module_type_on_network_filter_data_status>(
      plugin_handle->plugin_->onNewConnection());
}

envoy_dynamic_module_type_on_network_filter_data_status envoy_dynamic_module_on_network_filter_read(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr, size_t data_length,
    bool end_stream) {
  static_cast<void>(filter_envoy_ptr);
  static_cast<void>(data_length);
  auto* plugin_handle = unwrapPointer<NetworkFilterHandleImpl>(filter_module_ptr);
  if (plugin_handle == nullptr) {
    return envoy_dynamic_module_type_on_network_filter_data_status_Continue;
  }
  return static_cast<envoy_dynamic_module_type_on_network_filter_data_status>(
      plugin_handle->plugin_->onRead(plugin_handle->readBuffer(), end_stream));
}

envoy_dynamic_module_type_on_network_filter_data_status
envoy_dynamic_module_on_network_filter_write(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr, size_t data_length,
    bool end_stream) {
  static_cast<void>(filter_envoy_ptr);
  static_cast<void>(data_length);
  auto* plugin_handle = unwrapPointer<NetworkFilterHandleImpl>(filter_module_ptr);
  if (plugin_handle == nullptr) {
    return envoy_dynamic_module_type_on_network_filter_data_status_Continue;
  }
  return static_cast<envoy_dynamic_module_type_on_network_filter_data_status>(
      plugin_handle->plugin_->onWrite(plugin_handle->writeBuffer(), end_stream));
}

void envoy_dynamic_module_on_network_filter_event(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr,
    envoy_dynamic_module_type_network_connection_event event) {
  static_cast<void>(filter_envoy_ptr);
  auto* plugin_handle = unwrapPointer<NetworkFilterHandleImpl>(filter_module_ptr);
  if (plugin_handle == nullptr) {
    return;
  }
  plugin_handle->plugin_->onEvent(static_cast<NetworkConnectionEvent>(event));
}

void envoy_dynamic_module_on_network_filter_destroy(
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr) {
  auto* plugin_handle = unwrapPointer<NetworkFilterHandleImpl>(filter_module_ptr);
  if (plugin_handle == nullptr) {
    return;
  }
  plugin_handle->plugin_->onDestroy();
  delete plugin_handle;
}

void envoy_dynamic_module_on_network_filter_http_callout_done(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr, uint64_t callout_id,
    envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_chunks, size_t body_chunks_size) {
  static_cast<void>(filter_envoy_ptr);
  auto* plugin_handle = unwrapPointer<NetworkFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle) {
    return;
  }

  auto it = plugin_handle->callout_callbacks_.find(callout_id);
  if (it != plugin_handle->callout_callbacks_.end()) {
    auto* typed_headers = reinterpret_cast<HeaderView*>(headers);
    auto* typed_body_chunks = reinterpret_cast<BufferView*>(body_chunks);
    auto* callback = it->second;
    plugin_handle->callout_callbacks_.erase(it);
    callback->onHttpCalloutDone(static_cast<HttpCalloutResult>(result),
                                {typed_headers, headers_size},
                                {typed_body_chunks, body_chunks_size});
  }
}

void envoy_dynamic_module_on_network_filter_scheduled(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr, uint64_t event_id) {
  static_cast<void>(filter_envoy_ptr);
  auto* plugin_handle = unwrapPointer<NetworkFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle || !plugin_handle->scheduler_) {
    return;
  }
  plugin_handle->scheduler_->onScheduled(event_id);
}

void envoy_dynamic_module_on_network_filter_config_scheduled(
    envoy_dynamic_module_type_network_filter_config_module_ptr filter_config_ptr,
    uint64_t event_id) {
  auto* factory_wrapper = unwrapPointer<NetworkFilterFactoryWrapper>(filter_config_ptr);
  if (factory_wrapper == nullptr || factory_wrapper->config_handle_ == nullptr ||
      factory_wrapper->config_handle_->scheduler_ == nullptr) {
    return;
  }
  factory_wrapper->config_handle_->scheduler_->onScheduled(event_id);
}

void envoy_dynamic_module_on_network_filter_above_write_buffer_high_watermark(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr) {
  static_cast<void>(filter_envoy_ptr);
  auto* plugin_handle = unwrapPointer<NetworkFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle) {
    return;
  }
  plugin_handle->plugin_->onAboveWriteBufferHighWatermark();
}

void envoy_dynamic_module_on_network_filter_below_write_buffer_low_watermark(
    envoy_dynamic_module_type_network_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_network_filter_module_ptr filter_module_ptr) {
  static_cast<void>(filter_envoy_ptr);
  auto* plugin_handle = unwrapPointer<NetworkFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle) {
    return;
  }
  plugin_handle->plugin_->onBelowWriteBufferLowWatermark();
}

} // extern "C"

} // namespace DynamicModules
} // namespace Envoy
