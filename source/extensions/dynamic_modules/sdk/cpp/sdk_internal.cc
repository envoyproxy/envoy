#include <cstddef>
#include <cstdint>

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/abi/abi_version.h"

#include "absl/synchronization/mutex.h"
#include "sdk.h"

namespace Envoy {
namespace DynamicModules {

// BodyBuffer implementation
template <envoy_dynamic_module_type_http_body_type Type> class BodyBufferImpl : public BodyBuffer {
public:
  BodyBufferImpl(void* host_plugin_ptr) : host_plugin_ptr_(host_plugin_ptr) {}

  std::vector<BufferView> getChunks() override {
    size_t chunks_size =
        envoy_dynamic_module_callback_http_get_body_chunks_size(host_plugin_ptr_, Type);
    if (chunks_size == 0) {
      return {};
    }

    std::vector<BufferView> result_chunks(chunks_size);
    envoy_dynamic_module_callback_http_get_body_chunks(
        host_plugin_ptr_, Type,
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(result_chunks.data()));
    return result_chunks;
  }

  size_t getSize() override {
    return envoy_dynamic_module_callback_http_get_body_size(host_plugin_ptr_, Type);
  }

  void append(absl::string_view data) override {
    if (data.empty()) {
      return;
    }
    envoy_dynamic_module_callback_http_append_body(
        host_plugin_ptr_, Type, envoy_dynamic_module_type_module_buffer{data.data(), data.size()});
  }

  void drain(size_t size) override {
    envoy_dynamic_module_callback_http_drain_body(host_plugin_ptr_, Type, size);
  }

private:
  void* host_plugin_ptr_;
};

using ReceivedRequestBody =
    BodyBufferImpl<envoy_dynamic_module_type_http_body_type_ReceivedRequestBody>;
using BufferedRequestBody =
    BodyBufferImpl<envoy_dynamic_module_type_http_body_type_BufferedRequestBody>;
using ReceivedResponseBody =
    BodyBufferImpl<envoy_dynamic_module_type_http_body_type_ReceivedResponseBody>;
using BufferedResponseBody =
    BodyBufferImpl<envoy_dynamic_module_type_http_body_type_BufferedResponseBody>;

// HeaderMap implementation
template <envoy_dynamic_module_type_http_header_type Type> class HeaderMapImpl : public HeaderMap {
public:
  HeaderMapImpl(void* host_plugin_ptr) : host_plugin_ptr_(host_plugin_ptr) {}

  std::vector<absl::string_view> get(absl::string_view key) const override {
    size_t value_count = 0;
    auto first_value = getSingleHeader(key, 0, &value_count);
    if (value_count == 0) {
      return {};
    }

    std::vector<absl::string_view> values;
    values.reserve(value_count);
    values.push_back(first_value);

    for (size_t i = 1; i < value_count; i++) {
      values.push_back(getSingleHeader(key, i, nullptr));
    }
    return values;
  }

  absl::string_view getOne(absl::string_view key) const override {
    return getSingleHeader(key, 0, nullptr);
  }

  std::vector<HeaderView> getAll() const override {
    size_t header_count =
        envoy_dynamic_module_callback_http_get_headers_size(host_plugin_ptr_, Type);
    if (header_count == 0) {
      return {};
    }

    std::vector<HeaderView> result_headers(header_count);
    envoy_dynamic_module_callback_http_get_headers(
        host_plugin_ptr_, Type,
        reinterpret_cast<envoy_dynamic_module_type_envoy_http_header*>(result_headers.data()));
    return result_headers;
  }

  void set(absl::string_view key, absl::string_view value) override {
    envoy_dynamic_module_callback_http_set_header(
        host_plugin_ptr_, Type, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  void add(absl::string_view key, absl::string_view value) override {
    envoy_dynamic_module_callback_http_add_header(
        host_plugin_ptr_, Type, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  void remove(absl::string_view key) override {
    envoy_dynamic_module_callback_http_set_header(
        host_plugin_ptr_, Type, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{nullptr, 0});
  }

  size_t size() const override {
    return envoy_dynamic_module_callback_http_get_headers_size(host_plugin_ptr_, Type);
  }

private:
  void* host_plugin_ptr_;

  absl::string_view getSingleHeader(absl::string_view key, size_t index,
                                    size_t* value_count) const {
    BufferView value{nullptr, 0};

    bool ret = envoy_dynamic_module_callback_http_get_header(
        host_plugin_ptr_, Type, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value), index, value_count);
    if (!ret || value.data() == nullptr || value.size() == 0) {
      return {};
    }
    return value.toStringView();
  }
};

using RequestHeaders = HeaderMapImpl<envoy_dynamic_module_type_http_header_type_RequestHeader>;
using RequestTrailers = HeaderMapImpl<envoy_dynamic_module_type_http_header_type_RequestTrailer>;
using ResponseHeaders = HeaderMapImpl<envoy_dynamic_module_type_http_header_type_ResponseHeader>;
using ResponseTrailers = HeaderMapImpl<envoy_dynamic_module_type_http_header_type_ResponseTrailer>;

// Scheduler implementation
class SchedulerImpl : public Scheduler {
public:
  SchedulerImpl(void* host_plugin_ptr)
      : scheduler_ptr_(envoy_dynamic_module_callback_http_filter_scheduler_new(host_plugin_ptr)) {}

  void schedule(std::function<void()> func) override {
    // Lock to protect access to tasks_ and next_task_id_ manually
    mutex_.lock();
    const uint64_t task_id = next_task_id_++;
    tasks_[task_id] = std::move(func);
    mutex_.unlock();

    envoy_dynamic_module_callback_http_filter_scheduler_commit(scheduler_ptr_, task_id);
  }

  void onScheduled(uint64_t task_id) {
    std::function<void()> func;

    // Lock to protect access to tasks_ manually
    mutex_.lock();
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
      func = std::move(it->second);
      tasks_.erase(it);
    }
    mutex_.unlock();

    if (func) {
      func();
    }
  }

  ~SchedulerImpl() override {
    envoy_dynamic_module_callback_http_filter_scheduler_delete(scheduler_ptr_);
  }

private:
  void* scheduler_ptr_{};

  absl::Mutex mutex_;
  uint64_t next_task_id_ ABSL_GUARDED_BY(mutex_){1}; // 0 is reserved.
  absl::flat_hash_map<uint64_t, std::function<void()>> tasks_ ABSL_GUARDED_BY(mutex_);
};

// HttpFilterHandle implementation
class HttpFilterHandleImpl : public HttpFilterHandle {
public:
  HttpFilterHandleImpl(void* host_plugin_ptr)
      : host_plugin_ptr_(host_plugin_ptr), request_headers_(host_plugin_ptr),
        response_headers_(host_plugin_ptr), request_trailers_(host_plugin_ptr),
        response_trailers_(host_plugin_ptr), received_request_body_(host_plugin_ptr),
        received_response_body_(host_plugin_ptr), buffered_request_body_(host_plugin_ptr),
        buffered_response_body_(host_plugin_ptr) {}

  absl::optional<absl::string_view> getMetadataString(absl::string_view ns,
                                                      absl::string_view key) override {
    BufferView value{nullptr, 0};

    const bool ret = envoy_dynamic_module_callback_http_get_metadata_string(
        host_plugin_ptr_, envoy_dynamic_module_type_metadata_source_Dynamic,
        envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));

    if (!ret || value.data() == nullptr || value.size() == 0) {
      return {};
    }
    return value.toStringView();
  }

  absl::optional<double> getMetadataNumber(absl::string_view ns, absl::string_view key) override {
    double value = 0.0;
    const bool ret = envoy_dynamic_module_callback_http_get_metadata_number(
        host_plugin_ptr_, envoy_dynamic_module_type_metadata_source_Dynamic,
        envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, &value);

    if (!ret) {
      return {};
    }
    return value;
  }

  void setMetadata(absl::string_view ns, absl::string_view key, absl::string_view value) override {
    envoy_dynamic_module_callback_http_set_dynamic_metadata_string(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  void setMetadata(absl::string_view ns, absl::string_view key, double value) override {
    envoy_dynamic_module_callback_http_set_dynamic_metadata_number(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, value);
  }

  absl::optional<absl::string_view> getFilterState(absl::string_view key) override {
    BufferView value{nullptr, 0};

    const bool ret = envoy_dynamic_module_callback_http_get_filter_state_bytes(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));

    if (!ret || value.data() == nullptr || value.size() == 0) {
      return {};
    }
    return value.toStringView();
  }

  void setFilterState(absl::string_view key, absl::string_view value) override {
    envoy_dynamic_module_callback_http_set_filter_state_bytes(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  absl::optional<absl::string_view> getAttributeString(AttributeID id) override {
    BufferView value{nullptr, 0};

    const bool ret = envoy_dynamic_module_callback_http_filter_get_attribute_string(
        host_plugin_ptr_, static_cast<envoy_dynamic_module_type_attribute_id>(id),
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));

    if (!ret || value.data() == nullptr || value.size() == 0) {
      return {};
    }
    return value.toStringView();
  }

  absl::optional<uint64_t> getAttributeNumber(AttributeID id) override {
    uint64_t value = 0;
    const bool ret = envoy_dynamic_module_callback_http_filter_get_attribute_int(
        host_plugin_ptr_, static_cast<envoy_dynamic_module_type_attribute_id>(id), &value);

    if (!ret) {
      return {};
    }
    return value;
  }

  void sendLocalResponse(uint32_t status, absl::Span<const HeaderView> headers,
                         absl::string_view body, absl::string_view detail) override {
    envoy_dynamic_module_callback_http_send_response(
        host_plugin_ptr_, status,
        const_cast<envoy_dynamic_module_type_module_http_header*>(
            reinterpret_cast<const envoy_dynamic_module_type_module_http_header*>(headers.data())),
        headers.size(), envoy_dynamic_module_type_module_buffer{body.data(), body.size()},
        envoy_dynamic_module_type_module_buffer{detail.data(), detail.size()});
  }

  void sendResponseHeaders(absl::Span<const HeaderView> headers, bool end_stream) override {
    envoy_dynamic_module_callback_http_send_response_headers(
        host_plugin_ptr_,
        const_cast<envoy_dynamic_module_type_module_http_header*>(
            reinterpret_cast<const envoy_dynamic_module_type_module_http_header*>(headers.data())),
        headers.size(), end_stream);
  }

  void sendResponseData(absl::string_view body, bool end_stream) override {
    envoy_dynamic_module_callback_http_send_response_data(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{body.data(), body.size()},
        end_stream);
  }

  void sendResponseTrailers(absl::Span<const HeaderView> trailers) override {
    envoy_dynamic_module_callback_http_send_response_trailers(
        host_plugin_ptr_,
        const_cast<envoy_dynamic_module_type_module_http_header*>(
            reinterpret_cast<const envoy_dynamic_module_type_module_http_header*>(trailers.data())),
        trailers.size());
  }

  void addCustomFlag(absl::string_view flag) override {
    envoy_dynamic_module_callback_http_add_custom_flag(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{flag.data(), flag.size()});
  }

  void continueRequest() override {
    envoy_dynamic_module_callback_http_filter_continue_decoding(host_plugin_ptr_);
  }

  void continueResponse() override {
    envoy_dynamic_module_callback_http_filter_continue_encoding(host_plugin_ptr_);
  }

  void clearRouteCache() override {
    envoy_dynamic_module_callback_http_clear_route_cache(host_plugin_ptr_);
  }

  HeaderMap& requestHeaders() override { return request_headers_; }

  BodyBuffer& bufferedRequestBody() override { return buffered_request_body_; }

  HeaderMap& requestTrailers() override { return request_trailers_; }

  HeaderMap& responseHeaders() override { return response_headers_; }

  BodyBuffer& bufferedResponseBody() override { return buffered_response_body_; }

  HeaderMap& responseTrailers() override { return response_trailers_; }

  const RouteSpecificConfig* getMostSpecificConfig() override {
    const void* config_ptr =
        envoy_dynamic_module_callback_get_most_specific_route_config(host_plugin_ptr_);
    if (config_ptr == nullptr) {
      return nullptr;
    }
    return static_cast<const RouteSpecificConfig*>(config_ptr);
  }

  std::shared_ptr<Scheduler> getScheduler() override {
    if (!scheduler_) {
      scheduler_ = std::make_shared<SchedulerImpl>(host_plugin_ptr_);
    }
    return scheduler_;
  }

  std::pair<HttpCalloutInitResult, uint64_t>
  httpCallout(absl::string_view cluster, absl::Span<const HeaderView> headers,
              absl::string_view body, uint64_t timeout_ms, HttpCalloutCallback& cb) override {
    uint64_t callout_id_out = 0;
    auto result = envoy_dynamic_module_callback_http_filter_http_callout(
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

  std::pair<HttpCalloutInitResult, uint64_t>
  startHttpStream(absl::string_view cluster, absl::Span<const HeaderView> headers,
                  absl::string_view body, bool end_of_stream, uint64_t timeout_ms,
                  HttpStreamCallback& cb) override {
    uint64_t stream_id_out = 0;
    auto result = envoy_dynamic_module_callback_http_filter_start_http_stream(
        host_plugin_ptr_, &stream_id_out,
        envoy_dynamic_module_type_module_buffer{cluster.data(), cluster.size()},
        const_cast<envoy_dynamic_module_type_module_http_header*>(
            reinterpret_cast<const envoy_dynamic_module_type_module_http_header*>(headers.data())),
        headers.size(), envoy_dynamic_module_type_module_buffer{body.data(), body.size()},
        end_of_stream, timeout_ms);

    if (result == envoy_dynamic_module_type_http_callout_init_result_Success) {
      stream_callbacks_[stream_id_out] = &cb;
    }

    return {static_cast<HttpCalloutInitResult>(result), stream_id_out};
  }

  bool sendHttpStreamData(uint64_t stream_id, absl::string_view body, bool end_of_stream) override {
    return envoy_dynamic_module_callback_http_stream_send_data(
        host_plugin_ptr_, stream_id,
        envoy_dynamic_module_type_module_buffer{body.data(), body.size()}, end_of_stream);
  }

  bool sendHttpStreamTrailers(uint64_t stream_id, absl::Span<const HeaderView> trailers) override {
    return envoy_dynamic_module_callback_http_stream_send_trailers(
        host_plugin_ptr_, stream_id,
        const_cast<envoy_dynamic_module_type_module_http_header*>(
            reinterpret_cast<const envoy_dynamic_module_type_module_http_header*>(trailers.data())),
        trailers.size());
  }

  void resetHttpStream(uint64_t stream_id) override {
    envoy_dynamic_module_callback_http_filter_reset_http_stream(host_plugin_ptr_, stream_id);
  }

  void setDownstreamWatermarkCallbacks(DownstreamWatermarkCallbacks& callbacks) override {
    downstream_watermark_callbacks_ = &callbacks;
  }

  void clearDownstreamWatermarkCallbacks() override { downstream_watermark_callbacks_ = nullptr; }

  MetricsResult recordHistogramValue(MetricID id, uint64_t value,
                                     absl::Span<const BufferView> tags_values) override {
    return static_cast<MetricsResult>(
        envoy_dynamic_module_callback_http_filter_record_histogram_value(
            host_plugin_ptr_, id,
            const_cast<envoy_dynamic_module_type_module_buffer*>(
                reinterpret_cast<const envoy_dynamic_module_type_module_buffer*>(
                    tags_values.data())),
            tags_values.size(), value));
  }

  MetricsResult setGaugeValue(MetricID id, uint64_t value,
                              absl::Span<const BufferView> tags_values) override {
    return static_cast<MetricsResult>(envoy_dynamic_module_callback_http_filter_set_gauge(
        host_plugin_ptr_, id,
        const_cast<envoy_dynamic_module_type_module_buffer*>(
            reinterpret_cast<const envoy_dynamic_module_type_module_buffer*>(tags_values.data())),
        tags_values.size(), value));
  }

  MetricsResult incrementGaugeValue(MetricID id, uint64_t value,
                                    absl::Span<const BufferView> tags_values) override {
    return static_cast<MetricsResult>(envoy_dynamic_module_callback_http_filter_increment_gauge(
        host_plugin_ptr_, id,
        const_cast<envoy_dynamic_module_type_module_buffer*>(
            reinterpret_cast<const envoy_dynamic_module_type_module_buffer*>(tags_values.data())),
        tags_values.size(), value));
  }

  MetricsResult decrementGaugeValue(MetricID id, uint64_t value,
                                    absl::Span<const BufferView> tags_values) override {
    return static_cast<MetricsResult>(envoy_dynamic_module_callback_http_filter_decrement_gauge(
        host_plugin_ptr_, id,
        const_cast<envoy_dynamic_module_type_module_buffer*>(
            reinterpret_cast<const envoy_dynamic_module_type_module_buffer*>(tags_values.data())),
        tags_values.size(), value));
  }

  MetricsResult incrementCounterValue(MetricID id, uint64_t value,
                                      absl::Span<const BufferView> tags_values) override {
    return static_cast<MetricsResult>(envoy_dynamic_module_callback_http_filter_increment_counter(
        host_plugin_ptr_, id,
        const_cast<envoy_dynamic_module_type_module_buffer*>(
            reinterpret_cast<const envoy_dynamic_module_type_module_buffer*>(tags_values.data())),
        tags_values.size(), value));
  }
  bool logEnabled(LogLevel level) override {
    return envoy_dynamic_module_callback_log_enabled(
        static_cast<envoy_dynamic_module_type_log_level>(level));
  }
  void log(LogLevel level, absl::string_view message) override {
    return envoy_dynamic_module_callback_log(
        static_cast<envoy_dynamic_module_type_log_level>(level),
        envoy_dynamic_module_type_module_buffer{message.data(), message.size()});
  }

  void* host_plugin_ptr_;
  RequestHeaders request_headers_;
  ResponseHeaders response_headers_;
  RequestTrailers request_trailers_;
  ResponseTrailers response_trailers_;

  ReceivedRequestBody received_request_body_;
  ReceivedResponseBody received_response_body_;
  BufferedRequestBody buffered_request_body_;
  BufferedResponseBody buffered_response_body_;

  std::shared_ptr<SchedulerImpl> scheduler_;

  absl::flat_hash_map<uint64_t, HttpCalloutCallback*> callout_callbacks_;
  absl::flat_hash_map<uint64_t, HttpStreamCallback*> stream_callbacks_;

  DownstreamWatermarkCallbacks* downstream_watermark_callbacks_ = nullptr;

  std::unique_ptr<HttpFilter> plugin_;
  bool stream_complete_ = false;
  bool local_reply_sent_ = false;
};

// HttpFilterConfigHandle implementation
class HttpFilterConfigHandleImpl : public HttpFilterConfigHandle {
public:
  HttpFilterConfigHandleImpl(void* host_config_ptr) : host_config_ptr_(host_config_ptr) {}

  std::pair<MetricID, MetricsResult>
  defineHistogram(absl::string_view name, absl::Span<const BufferView> tags_keys) override {
    size_t metric_id = 0;
    auto result = static_cast<MetricsResult>(
        envoy_dynamic_module_callback_http_filter_config_define_histogram(
            host_config_ptr_, envoy_dynamic_module_type_module_buffer{name.data(), name.size()},
            const_cast<envoy_dynamic_module_type_module_buffer*>(
                reinterpret_cast<const envoy_dynamic_module_type_module_buffer*>(tags_keys.data())),
            tags_keys.size(), &metric_id));
    return {metric_id, result};
  }

  std::pair<MetricID, MetricsResult> defineGauge(absl::string_view name,
                                                 absl::Span<const BufferView> tags_keys) override {
    size_t metric_id = 0;
    auto result =
        static_cast<MetricsResult>(envoy_dynamic_module_callback_http_filter_config_define_gauge(
            host_config_ptr_, envoy_dynamic_module_type_module_buffer{name.data(), name.size()},
            const_cast<envoy_dynamic_module_type_module_buffer*>(
                reinterpret_cast<const envoy_dynamic_module_type_module_buffer*>(tags_keys.data())),
            tags_keys.size(), &metric_id));
    return {metric_id, result};
  }

  std::pair<MetricID, MetricsResult>
  defineCounter(absl::string_view name, absl::Span<const BufferView> tags_keys) override {
    size_t metric_id = 0;
    auto result =
        static_cast<MetricsResult>(envoy_dynamic_module_callback_http_filter_config_define_counter(
            host_config_ptr_, envoy_dynamic_module_type_module_buffer{name.data(), name.size()},
            const_cast<envoy_dynamic_module_type_module_buffer*>(
                reinterpret_cast<const envoy_dynamic_module_type_module_buffer*>(tags_keys.data())),
            tags_keys.size(), &metric_id));
    return {metric_id, result};
  }
  bool logEnabled(LogLevel level) override {
    return envoy_dynamic_module_callback_log_enabled(
        static_cast<envoy_dynamic_module_type_log_level>(level));
  }
  void log(LogLevel level, absl::string_view message) override {
    return envoy_dynamic_module_callback_log(
        static_cast<envoy_dynamic_module_type_log_level>(level),
        envoy_dynamic_module_type_module_buffer{message.data(), message.size()});
  }

private:
  void* host_config_ptr_;
};

class DummyLoggerHandle {
public:
  bool logEnabled(LogLevel level) {
    return envoy_dynamic_module_callback_log_enabled(
        static_cast<envoy_dynamic_module_type_log_level>(level));
  }
  void log(LogLevel level, absl::string_view message) {
    return envoy_dynamic_module_callback_log(
        static_cast<envoy_dynamic_module_type_log_level>(level),
        envoy_dynamic_module_type_module_buffer{message.data(), message.size()});
  }
};

DummyLoggerHandle& dummyLoggerHandle() {
  static auto handle = new DummyLoggerHandle();
  return *handle;
}

template <class T> T* unwrapPointer(const void* ptr) {
  return const_cast<T*>(reinterpret_cast<const T*>(ptr));
}

template <class T> void* wrapPointer(const T* ptr) {
  return reinterpret_cast<void*>(const_cast<T*>(ptr));
}

struct HttpFilterFactoryWrapper {
  std::unique_ptr<HttpFilterConfigHandle> config_handle_;
  std::unique_ptr<HttpFilterFactory> factory_;
};

// Extern C exports
extern "C" {

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return Envoy::Extensions::DynamicModules::kAbiVersion;
}

envoy_dynamic_module_type_http_filter_config_module_ptr
envoy_dynamic_module_on_http_filter_config_new(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  auto config_handle = std::make_unique<HttpFilterConfigHandleImpl>(filter_config_envoy_ptr);
  absl::string_view name_view(name.ptr, name.length);
  absl::string_view config_view(config.ptr, config.length);

  auto config_factory = HttpFilterConfigFactoryRegistry::getRegistry().find(name_view);
  if (config_factory == HttpFilterConfigFactoryRegistry::getRegistry().end()) {
    DYM_LOG((*config_handle), LogLevel::Warn, "Plugin config factory not found for name: {}",
            name_view);
    return nullptr;
  }

  auto plugin_factory = config_factory->second->create(*config_handle, config_view);
  if (!plugin_factory) {
    DYM_LOG((*config_handle), LogLevel::Warn, "Failed to create plugin factory for name: {}",
            name_view);
    return nullptr;
  }

  auto factory = std::make_unique<HttpFilterFactoryWrapper>();
  factory->config_handle_ = std::move(config_handle);
  factory->factory_ = std::move(plugin_factory);

  return wrapPointer(factory.release());
}

void envoy_dynamic_module_on_http_filter_config_destroy(
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr) {
  auto* factory_wrapper = unwrapPointer<HttpFilterFactoryWrapper>(filter_config_ptr);
  delete factory_wrapper;
}

envoy_dynamic_module_type_http_filter_per_route_config_module_ptr
envoy_dynamic_module_on_http_filter_per_route_config_new(
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  absl::string_view name_view(name.ptr, name.length);
  absl::string_view config_view(config.ptr, config.length);

  auto config_factory = HttpFilterConfigFactoryRegistry::getRegistry().find(name_view);
  if (config_factory == HttpFilterConfigFactoryRegistry::getRegistry().end()) {
    DYM_LOG(dummyLoggerHandle(), LogLevel::Warn,
            "Plugin per-route config factory not found for name: {}", name_view);
    return nullptr;
  }

  auto parsed_config = config_factory->second->createPerRoute(config_view);
  if (!parsed_config) {
    DYM_LOG(dummyLoggerHandle(), LogLevel::Warn,
            "Failed to create plugin per-route config for name: {}", name_view);
    return nullptr;
  }

  return wrapPointer(parsed_config.release());
}

void envoy_dynamic_module_on_http_filter_per_route_config_destroy(
    envoy_dynamic_module_type_http_filter_per_route_config_module_ptr filter_config_ptr) {
  auto* config = unwrapPointer<RouteSpecificConfig>(filter_config_ptr);
  delete config;
}

envoy_dynamic_module_type_http_filter_module_ptr envoy_dynamic_module_on_http_filter_new(
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr,
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr) {
  auto* factory_wrapper = unwrapPointer<HttpFilterFactoryWrapper>(filter_config_ptr);
  if (!factory_wrapper) {
    return nullptr;
  }

  auto plugin_handle = std::make_unique<HttpFilterHandleImpl>(filter_envoy_ptr);
  auto plugin = factory_wrapper->factory_->create(*plugin_handle);
  if (plugin == nullptr) {
    DYM_LOG((*plugin_handle), LogLevel::Warn, "Failed to create plugin instance");
    return nullptr;
  }
  plugin_handle->plugin_ = std::move(plugin);

  return wrapPointer(plugin_handle.release());
}

void envoy_dynamic_module_on_http_filter_destroy(
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr) {
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);
  delete plugin_handle;
}

envoy_dynamic_module_type_on_http_filter_request_headers_status
envoy_dynamic_module_on_http_filter_request_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, bool end_of_stream) {
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);

  if (plugin_handle == nullptr) {
    return envoy_dynamic_module_type_on_http_filter_request_headers_status_Continue;
  }
  auto status =
      plugin_handle->plugin_->onRequestHeaders(plugin_handle->request_headers_, end_of_stream);

  return static_cast<envoy_dynamic_module_type_on_http_filter_request_headers_status>(status);
}

envoy_dynamic_module_type_on_http_filter_request_body_status
envoy_dynamic_module_on_http_filter_request_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, bool end_of_stream) {
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);
  if (plugin_handle == nullptr) {
    return envoy_dynamic_module_type_on_http_filter_request_body_status_Continue;
  }

  auto status =
      plugin_handle->plugin_->onRequestBody(plugin_handle->received_request_body_, end_of_stream);
  return static_cast<envoy_dynamic_module_type_on_http_filter_request_body_status>(status);
}

envoy_dynamic_module_type_on_http_filter_request_trailers_status
envoy_dynamic_module_on_http_filter_request_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr) {
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);
  if (plugin_handle == nullptr) {
    return envoy_dynamic_module_type_on_http_filter_request_trailers_status_Continue;
  }

  auto status = plugin_handle->plugin_->onRequestTrailers(plugin_handle->request_trailers_);
  return static_cast<envoy_dynamic_module_type_on_http_filter_request_trailers_status>(status);
}

envoy_dynamic_module_type_on_http_filter_response_headers_status
envoy_dynamic_module_on_http_filter_response_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, bool end_of_stream) {
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);
  if (plugin_handle == nullptr || plugin_handle->local_reply_sent_) {
    return envoy_dynamic_module_type_on_http_filter_response_headers_status_Continue;
  }

  auto status =
      plugin_handle->plugin_->onResponseHeaders(plugin_handle->response_headers_, end_of_stream);
  return static_cast<envoy_dynamic_module_type_on_http_filter_response_headers_status>(status);
}

envoy_dynamic_module_type_on_http_filter_response_body_status
envoy_dynamic_module_on_http_filter_response_body(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, bool end_of_stream) {
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);
  if (plugin_handle == nullptr || plugin_handle->local_reply_sent_) {
    return envoy_dynamic_module_type_on_http_filter_response_body_status_Continue;
  }

  auto status =
      plugin_handle->plugin_->onResponseBody(plugin_handle->received_response_body_, end_of_stream);
  return static_cast<envoy_dynamic_module_type_on_http_filter_response_body_status>(status);
}

envoy_dynamic_module_type_on_http_filter_response_trailers_status
envoy_dynamic_module_on_http_filter_response_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr) {
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);
  if (plugin_handle == nullptr || plugin_handle->local_reply_sent_) {
    return envoy_dynamic_module_type_on_http_filter_response_trailers_status_Continue;
  }

  auto status = plugin_handle->plugin_->onResponseTrailers(plugin_handle->response_trailers_);
  return static_cast<envoy_dynamic_module_type_on_http_filter_response_trailers_status>(status);
}

void envoy_dynamic_module_on_http_filter_stream_complete(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr) {
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);
  if (plugin_handle == nullptr) {
    return;
  }
  plugin_handle->stream_complete_ = true;
  plugin_handle->plugin_->onStreamComplete();
}

void envoy_dynamic_module_on_http_filter_scheduled(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t event_id) {
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle || !plugin_handle->scheduler_ || plugin_handle->stream_complete_) {
    return;
  }
  plugin_handle->scheduler_->onScheduled(event_id);
}

void envoy_dynamic_module_on_http_filter_http_callout_done(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t callout_id,
    envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_chunks, size_t body_chunks_size) {
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle || plugin_handle->stream_complete_) {
    return;
  }

  auto* typed_headers = reinterpret_cast<HeaderView*>(headers);
  auto* typed_body_chunks = reinterpret_cast<BufferView*>(body_chunks);

  auto it = plugin_handle->callout_callbacks_.find(callout_id);
  if (it != plugin_handle->callout_callbacks_.end()) {
    auto callback = it->second;
    plugin_handle->callout_callbacks_.erase(it);
    callback->onHttpCalloutDone(static_cast<HttpCalloutResult>(result),
                                {typed_headers, headers_size},
                                {typed_body_chunks, body_chunks_size});
  }
}

void envoy_dynamic_module_on_http_filter_http_stream_headers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size, bool end_stream) {
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle || plugin_handle->stream_complete_) {
    return;
  }

  auto it = plugin_handle->stream_callbacks_.find(stream_id);
  if (it != plugin_handle->stream_callbacks_.end()) {
    auto* typed_headers = reinterpret_cast<HeaderView*>(headers);
    it->second->onHttpStreamHeaders(stream_id, {typed_headers, headers_size}, end_stream);
  }
}

void envoy_dynamic_module_on_http_filter_http_stream_data(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t stream_id,
    const envoy_dynamic_module_type_envoy_buffer* chunks, size_t chunks_size, bool end_stream) {
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle || plugin_handle->stream_complete_) {
    return;
  }

  auto it = plugin_handle->stream_callbacks_.find(stream_id);
  if (it != plugin_handle->stream_callbacks_.end()) {
    auto* typed_chunks =
        reinterpret_cast<BufferView*>(const_cast<envoy_dynamic_module_type_envoy_buffer*>(chunks));
    it->second->onHttpStreamData(stream_id, {typed_chunks, chunks_size}, end_stream);
  }
}

void envoy_dynamic_module_on_http_filter_http_stream_trailers(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_envoy_http_header* trailers, size_t trailers_size) {
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle || plugin_handle->stream_complete_) {
    return;
  }

  auto it = plugin_handle->stream_callbacks_.find(stream_id);
  if (it != plugin_handle->stream_callbacks_.end()) {
    auto* typed_trailers = reinterpret_cast<HeaderView*>(trailers);
    it->second->onHttpStreamTrailers(stream_id, {typed_trailers, trailers_size});
  }
}

void envoy_dynamic_module_on_http_filter_http_stream_complete(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t stream_id) {
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle || plugin_handle->stream_complete_) {
    return;
  }

  auto it = plugin_handle->stream_callbacks_.find(stream_id);
  if (it != plugin_handle->stream_callbacks_.end()) {
    auto* cb = it->second;
    plugin_handle->stream_callbacks_.erase(it);
    cb->onHttpStreamComplete(stream_id);
  }
}

void envoy_dynamic_module_on_http_filter_http_stream_reset(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_http_stream_reset_reason reason) {
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle || plugin_handle->stream_complete_) {
    return;
  }

  auto it = plugin_handle->stream_callbacks_.find(stream_id);
  if (it != plugin_handle->stream_callbacks_.end()) {
    auto* cb = it->second;
    plugin_handle->stream_callbacks_.erase(it);
    cb->onHttpStreamReset(stream_id, static_cast<HttpStreamResetReason>(reason));
  }
}

void envoy_dynamic_module_on_http_filter_downstream_above_write_buffer_high_watermark(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr) {
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle || plugin_handle->stream_complete_) {
    return;
  }

  if (plugin_handle->downstream_watermark_callbacks_) {
    plugin_handle->downstream_watermark_callbacks_->onAboveWriteBufferHighWatermark();
  }
}

void envoy_dynamic_module_on_http_filter_downstream_below_write_buffer_low_watermark(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr) {
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);
  if (!plugin_handle || plugin_handle->stream_complete_) {
    return;
  }

  if (plugin_handle->downstream_watermark_callbacks_) {
    plugin_handle->downstream_watermark_callbacks_->onBelowWriteBufferLowWatermark();
  }
}

envoy_dynamic_module_type_on_http_filter_local_reply_status
envoy_dynamic_module_on_http_filter_local_reply(
    envoy_dynamic_module_type_http_filter_envoy_ptr filter_envoy_ptr,
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr, uint32_t response_code,
    envoy_dynamic_module_type_envoy_buffer details, bool reset_imminent) {
  return envoy_dynamic_module_type_on_http_filter_local_reply_status_Continue;
}
}

} // namespace DynamicModules
} // namespace Envoy
