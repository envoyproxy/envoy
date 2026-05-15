#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>

#include "source/extensions/dynamic_modules/abi/abi.h"

#include "sdk.h"
#include "sdk_internal_common.h"

namespace Envoy {
namespace DynamicModules {

// BodyBuffer implementation
template <envoy_dynamic_module_type_http_body_type Type> class BodyBufferImpl : public BodyBuffer {
public:
  BodyBufferImpl(void* host_plugin_ptr) : host_plugin_ptr_(host_plugin_ptr) {}

  std::vector<BufferView> getChunks() const override {
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

  size_t getSize() const override {
    return envoy_dynamic_module_callback_http_get_body_size(host_plugin_ptr_, Type);
  }

  void append(std::string_view data) override {
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

  std::vector<std::string_view> get(std::string_view key) const override {
    size_t value_count = 0;
    auto first_value = getSingleHeader(key, 0, &value_count);
    if (value_count == 0) {
      return {};
    }

    std::vector<std::string_view> values;
    values.reserve(value_count);
    values.push_back(first_value);

    for (size_t i = 1; i < value_count; i++) {
      values.push_back(getSingleHeader(key, i, nullptr));
    }
    return values;
  }

  std::string_view getOne(std::string_view key) const override {
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

  void set(std::string_view key, std::string_view value) override {
    envoy_dynamic_module_callback_http_set_header(
        host_plugin_ptr_, Type, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  void add(std::string_view key, std::string_view value) override {
    envoy_dynamic_module_callback_http_add_header(
        host_plugin_ptr_, Type, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  void remove(std::string_view key) override {
    envoy_dynamic_module_callback_http_set_header(
        host_plugin_ptr_, Type, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{nullptr, 0});
  }

  size_t size() const override {
    return envoy_dynamic_module_callback_http_get_headers_size(host_plugin_ptr_, Type);
  }

private:
  void* host_plugin_ptr_;

  std::string_view getSingleHeader(std::string_view key, size_t index, size_t* value_count) const {
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
using SchedulerImpl = SchedulerImplBase<envoy_dynamic_module_callback_http_filter_scheduler_new,
                                        envoy_dynamic_module_callback_http_filter_scheduler_commit,
                                        envoy_dynamic_module_callback_http_filter_scheduler_delete>;
using ConfigSchedulerImpl =
    SchedulerImplBase<envoy_dynamic_module_callback_http_filter_config_scheduler_new,
                      envoy_dynamic_module_callback_http_filter_config_scheduler_commit,
                      envoy_dynamic_module_callback_http_filter_config_scheduler_delete>;

std::optional<std::string_view> bufferViewToOptionalStringView(const BufferView& value,
                                                               bool found) {
  if (!found) {
    return {};
  }
  return std::string_view(value.data() == nullptr ? "" : value.data(), value.size());
}

class ChildSpanImpl;

class SpanImpl : public Span {
public:
  SpanImpl(envoy_dynamic_module_type_http_filter_envoy_ptr host_plugin_ptr,
           envoy_dynamic_module_type_span_envoy_ptr span_ptr)
      : host_plugin_ptr_(host_plugin_ptr), span_ptr_(span_ptr) {}

  void setTag(std::string_view key, std::string_view value) override {
    envoy_dynamic_module_callback_http_span_set_tag(
        span_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  void setOperation(std::string_view operation) override {
    envoy_dynamic_module_callback_http_span_set_operation(
        span_ptr_, envoy_dynamic_module_type_module_buffer{operation.data(), operation.size()});
  }

  void log(std::string_view event) override {
    envoy_dynamic_module_callback_http_span_log(
        host_plugin_ptr_, span_ptr_,
        envoy_dynamic_module_type_module_buffer{event.data(), event.size()});
  }

  void setSampled(bool sampled) override {
    envoy_dynamic_module_callback_http_span_set_sampled(span_ptr_, sampled);
  }

  std::optional<std::string_view> getBaggage(std::string_view key) override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_http_span_get_baggage(
        span_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  void setBaggage(std::string_view key, std::string_view value) override {
    envoy_dynamic_module_callback_http_span_set_baggage(
        span_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  std::optional<std::string_view> getTraceID() override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_http_span_get_trace_id(
        span_ptr_, reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  std::optional<std::string_view> getSpanID() override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_http_span_get_span_id(
        span_ptr_, reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  std::unique_ptr<ChildSpan> spawnChild(std::string_view operation) override;

protected:
  const envoy_dynamic_module_type_http_filter_envoy_ptr host_plugin_ptr_;
  envoy_dynamic_module_type_span_envoy_ptr span_ptr_;
};

class ChildSpanImpl : public ChildSpan {
public:
  ChildSpanImpl(envoy_dynamic_module_type_http_filter_envoy_ptr host_plugin_ptr,
                envoy_dynamic_module_type_child_span_module_ptr child_span_ptr)
      : host_plugin_ptr_(host_plugin_ptr), child_span_ptr_(child_span_ptr),
        span_ptr_(reinterpret_cast<envoy_dynamic_module_type_span_envoy_ptr>(child_span_ptr)) {}

  ~ChildSpanImpl() override { finishImpl(); }

  void setTag(std::string_view key, std::string_view value) override {
    envoy_dynamic_module_callback_http_span_set_tag(
        span_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  void setOperation(std::string_view operation) override {
    envoy_dynamic_module_callback_http_span_set_operation(
        span_ptr_, envoy_dynamic_module_type_module_buffer{operation.data(), operation.size()});
  }

  void log(std::string_view event) override {
    envoy_dynamic_module_callback_http_span_log(
        host_plugin_ptr_, span_ptr_,
        envoy_dynamic_module_type_module_buffer{event.data(), event.size()});
  }

  void setSampled(bool sampled) override {
    envoy_dynamic_module_callback_http_span_set_sampled(span_ptr_, sampled);
  }

  std::optional<std::string_view> getBaggage(std::string_view key) override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_http_span_get_baggage(
        span_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  void setBaggage(std::string_view key, std::string_view value) override {
    envoy_dynamic_module_callback_http_span_set_baggage(
        span_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  std::optional<std::string_view> getTraceID() override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_http_span_get_trace_id(
        span_ptr_, reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  std::optional<std::string_view> getSpanID() override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_http_span_get_span_id(
        span_ptr_, reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  std::unique_ptr<ChildSpan> spawnChild(std::string_view operation) override {
    if (child_span_ptr_ == nullptr) {
      return nullptr;
    }
    auto* child = envoy_dynamic_module_callback_http_span_spawn_child(
        host_plugin_ptr_, span_ptr_,
        envoy_dynamic_module_type_module_buffer{operation.data(), operation.size()});
    if (child == nullptr) {
      return nullptr;
    }
    return std::make_unique<ChildSpanImpl>(host_plugin_ptr_, child);
  }

  void finish() override { finishImpl(); }

private:
  void finishImpl() {
    if (child_span_ptr_ == nullptr) {
      return;
    }
    envoy_dynamic_module_callback_http_child_span_finish(child_span_ptr_);
    child_span_ptr_ = nullptr;
    span_ptr_ = nullptr;
  }
  const envoy_dynamic_module_type_http_filter_envoy_ptr host_plugin_ptr_;
  envoy_dynamic_module_type_child_span_module_ptr child_span_ptr_;
  envoy_dynamic_module_type_span_envoy_ptr span_ptr_;
};

std::unique_ptr<ChildSpan> SpanImpl::spawnChild(std::string_view operation) {
  auto* child = envoy_dynamic_module_callback_http_span_spawn_child(
      host_plugin_ptr_, span_ptr_,
      envoy_dynamic_module_type_module_buffer{operation.data(), operation.size()});
  if (child == nullptr) {
    return nullptr;
  }
  return std::make_unique<ChildSpanImpl>(host_plugin_ptr_, child);
}

// HttpFilterHandle implementation
class HttpFilterHandleImpl : public HttpFilterHandle {
public:
  HttpFilterHandleImpl(void* host_plugin_ptr)
      : host_plugin_ptr_(host_plugin_ptr), request_headers_(host_plugin_ptr),
        response_headers_(host_plugin_ptr), request_trailers_(host_plugin_ptr),
        response_trailers_(host_plugin_ptr), received_request_body_(host_plugin_ptr),
        received_response_body_(host_plugin_ptr), buffered_request_body_(host_plugin_ptr),
        buffered_response_body_(host_plugin_ptr) {}

  std::optional<std::string_view> getMetadataString(std::string_view ns,
                                                    std::string_view key) override {
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

  std::optional<double> getMetadataNumber(std::string_view ns, std::string_view key) override {
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

  std::optional<bool> getMetadataBool(std::string_view ns, std::string_view key) override {
    bool value = false;
    const bool ret = envoy_dynamic_module_callback_http_get_metadata_bool(
        host_plugin_ptr_, envoy_dynamic_module_type_metadata_source_Dynamic,
        envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, &value);

    if (!ret) {
      return {};
    }
    return value;
  }

  std::vector<std::string_view> getMetadataKeys(std::string_view ns) override {
    size_t count = envoy_dynamic_module_callback_http_get_metadata_keys_count(
        host_plugin_ptr_, envoy_dynamic_module_type_metadata_source_Dynamic,
        envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()});
    if (count == 0) {
      return {};
    }
    std::vector<envoy_dynamic_module_type_envoy_buffer> buffers(count);
    const bool ret = envoy_dynamic_module_callback_http_get_metadata_keys(
        host_plugin_ptr_, envoy_dynamic_module_type_metadata_source_Dynamic,
        envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()}, buffers.data());
    if (!ret) {
      return {};
    }
    std::vector<std::string_view> keys;
    keys.reserve(count);
    for (size_t i = 0; i < count; i++) {
      keys.emplace_back(buffers[i].ptr, buffers[i].length);
    }
    return keys;
  }

  std::vector<std::string_view> getMetadataNamespaces() override {
    size_t count = envoy_dynamic_module_callback_http_get_metadata_namespaces_count(
        host_plugin_ptr_, envoy_dynamic_module_type_metadata_source_Dynamic);
    if (count == 0) {
      return {};
    }
    std::vector<envoy_dynamic_module_type_envoy_buffer> buffers(count);
    const bool ret = envoy_dynamic_module_callback_http_get_metadata_namespaces(
        host_plugin_ptr_, envoy_dynamic_module_type_metadata_source_Dynamic, buffers.data());
    if (!ret) {
      return {};
    }
    std::vector<std::string_view> namespaces;
    namespaces.reserve(count);
    for (size_t i = 0; i < count; i++) {
      namespaces.emplace_back(buffers[i].ptr, buffers[i].length);
    }
    return namespaces;
  }

  void setMetadata(std::string_view ns, std::string_view key, std::string_view value) override {
    envoy_dynamic_module_callback_http_set_dynamic_metadata_string(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  void setMetadata(std::string_view ns, std::string_view key, double value) override {
    envoy_dynamic_module_callback_http_set_dynamic_metadata_number(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, value);
  }

  void setMetadata(std::string_view ns, std::string_view key, bool value) override {
    envoy_dynamic_module_callback_http_set_dynamic_metadata_bool(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, value);
  }

  bool addMetadataList(std::string_view ns, std::string_view key, double value) override {
    return envoy_dynamic_module_callback_http_add_dynamic_metadata_list_number(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, value);
  }

  bool addMetadataList(std::string_view ns, std::string_view key, std::string_view value) override {
    return envoy_dynamic_module_callback_http_add_dynamic_metadata_list_string(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  bool addMetadataList(std::string_view ns, std::string_view key, bool value) override {
    return envoy_dynamic_module_callback_http_add_dynamic_metadata_list_bool(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, value);
  }

  std::optional<size_t> getMetadataListSize(std::string_view ns, std::string_view key) override {
    size_t result = 0;
    const bool ret = envoy_dynamic_module_callback_http_get_metadata_list_size(
        host_plugin_ptr_, envoy_dynamic_module_type_metadata_source_Dynamic,
        envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, &result);
    if (!ret) {
      return {};
    }
    return result;
  }

  std::optional<double> getMetadataListNumber(std::string_view ns, std::string_view key,
                                              size_t index) override {
    double value = 0.0;
    const bool ret = envoy_dynamic_module_callback_http_get_metadata_list_number(
        host_plugin_ptr_, envoy_dynamic_module_type_metadata_source_Dynamic,
        envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, index, &value);
    if (!ret) {
      return {};
    }
    return value;
  }

  std::optional<std::string_view> getMetadataListString(std::string_view ns, std::string_view key,
                                                        size_t index) override {
    BufferView value{nullptr, 0};
    const bool ret = envoy_dynamic_module_callback_http_get_metadata_list_string(
        host_plugin_ptr_, envoy_dynamic_module_type_metadata_source_Dynamic,
        envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, index,
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    if (!ret || value.data() == nullptr) {
      return {};
    }
    return value.toStringView();
  }

  std::optional<bool> getMetadataListBool(std::string_view ns, std::string_view key,
                                          size_t index) override {
    bool value = false;
    const bool ret = envoy_dynamic_module_callback_http_get_metadata_list_bool(
        host_plugin_ptr_, envoy_dynamic_module_type_metadata_source_Dynamic,
        envoy_dynamic_module_type_module_buffer{ns.data(), ns.size()},
        envoy_dynamic_module_type_module_buffer{key.data(), key.size()}, index, &value);
    if (!ret) {
      return {};
    }
    return value;
  }

  std::optional<std::string_view> getFilterState(std::string_view key) override {
    BufferView value{nullptr, 0};

    const bool ret = envoy_dynamic_module_callback_http_get_filter_state_bytes(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));

    if (!ret || value.data() == nullptr || value.size() == 0) {
      return {};
    }
    return value.toStringView();
  }

  void setFilterState(std::string_view key, std::string_view value) override {
    envoy_dynamic_module_callback_http_set_filter_state_bytes(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  std::optional<std::string_view> getFilterStateTyped(std::string_view key) override {
    BufferView value{nullptr, 0};
    const bool ret = envoy_dynamic_module_callback_http_get_filter_state_typed(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, ret);
  }

  bool setFilterStateTyped(std::string_view key, std::string_view value) override {
    return envoy_dynamic_module_callback_http_set_filter_state_typed(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{key.data(), key.size()},
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  std::optional<std::string_view> getAttributeString(AttributeID id) override {
    BufferView value{nullptr, 0};

    const bool ret = envoy_dynamic_module_callback_http_filter_get_attribute_string(
        host_plugin_ptr_, static_cast<envoy_dynamic_module_type_attribute_id>(id),
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));

    if (!ret || value.data() == nullptr || value.size() == 0) {
      return {};
    }
    return value.toStringView();
  }

  std::optional<uint64_t> getAttributeNumber(AttributeID id) override {
    uint64_t value = 0;
    const bool ret = envoy_dynamic_module_callback_http_filter_get_attribute_int(
        host_plugin_ptr_, static_cast<envoy_dynamic_module_type_attribute_id>(id), &value);

    if (!ret) {
      return {};
    }
    return value;
  }

  std::optional<bool> getAttributeBool(AttributeID id) override {
    bool value = false;
    const bool ret = envoy_dynamic_module_callback_http_filter_get_attribute_bool(
        host_plugin_ptr_, static_cast<envoy_dynamic_module_type_attribute_id>(id), &value);

    if (!ret) {
      return {};
    }
    return value;
  }

  void sendLocalResponse(uint32_t status, std::span<const HeaderView> headers,
                         std::string_view body, std::string_view detail) override {
    local_reply_sent_ = true;
    envoy_dynamic_module_callback_http_send_response(
        host_plugin_ptr_, status,
        const_cast<envoy_dynamic_module_type_module_http_header*>(
            reinterpret_cast<const envoy_dynamic_module_type_module_http_header*>(headers.data())),
        headers.size(), envoy_dynamic_module_type_module_buffer{body.data(), body.size()},
        envoy_dynamic_module_type_module_buffer{detail.data(), detail.size()});
  }

  void sendResponseHeaders(std::span<const HeaderView> headers, bool end_stream) override {
    envoy_dynamic_module_callback_http_send_response_headers(
        host_plugin_ptr_,
        const_cast<envoy_dynamic_module_type_module_http_header*>(
            reinterpret_cast<const envoy_dynamic_module_type_module_http_header*>(headers.data())),
        headers.size(), end_stream);
  }

  void sendResponseData(std::string_view body, bool end_stream) override {
    envoy_dynamic_module_callback_http_send_response_data(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{body.data(), body.size()},
        end_stream);
  }

  void sendResponseTrailers(std::span<const HeaderView> trailers) override {
    envoy_dynamic_module_callback_http_send_response_trailers(
        host_plugin_ptr_,
        const_cast<envoy_dynamic_module_type_module_http_header*>(
            reinterpret_cast<const envoy_dynamic_module_type_module_http_header*>(trailers.data())),
        trailers.size());
  }

  void addCustomFlag(std::string_view flag) override {
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

  void refreshRouteCluster() override {
    envoy_dynamic_module_callback_http_clear_route_cluster_cache(host_plugin_ptr_);
  }

  uint32_t getWorkerIndex() override {
    return envoy_dynamic_module_callback_http_filter_get_worker_index(host_plugin_ptr_);
  }

  bool setSocketOptionInt(int64_t level, int64_t name, SocketOptionState state,
                          SocketDirection direction, int64_t value) override {
    return envoy_dynamic_module_callback_http_set_socket_option_int(
        host_plugin_ptr_, level, name,
        static_cast<envoy_dynamic_module_type_socket_option_state>(state),
        static_cast<envoy_dynamic_module_type_socket_direction>(direction), value);
  }

  bool setSocketOptionBytes(int64_t level, int64_t name, SocketOptionState state,
                            SocketDirection direction, std::string_view value) override {
    return envoy_dynamic_module_callback_http_set_socket_option_bytes(
        host_plugin_ptr_, level, name,
        static_cast<envoy_dynamic_module_type_socket_option_state>(state),
        static_cast<envoy_dynamic_module_type_socket_direction>(direction),
        envoy_dynamic_module_type_module_buffer{value.data(), value.size()});
  }

  std::optional<int64_t> getSocketOptionInt(int64_t level, int64_t name, SocketOptionState state,
                                            SocketDirection direction) override {
    int64_t value = 0;
    const bool found = envoy_dynamic_module_callback_http_get_socket_option_int(
        host_plugin_ptr_, level, name,
        static_cast<envoy_dynamic_module_type_socket_option_state>(state),
        static_cast<envoy_dynamic_module_type_socket_direction>(direction), &value);
    if (!found) {
      return {};
    }
    return value;
  }

  std::optional<std::string_view> getSocketOptionBytes(int64_t level, int64_t name,
                                                       SocketOptionState state,
                                                       SocketDirection direction) override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_http_get_socket_option_bytes(
        host_plugin_ptr_, level, name,
        static_cast<envoy_dynamic_module_type_socket_option_state>(state),
        static_cast<envoy_dynamic_module_type_socket_direction>(direction),
        reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  uint64_t getBufferLimit() override {
    return envoy_dynamic_module_callback_http_get_buffer_limit(host_plugin_ptr_);
  }

  void setBufferLimit(uint64_t limit) override {
    envoy_dynamic_module_callback_http_set_buffer_limit(host_plugin_ptr_, limit);
  }

  std::unique_ptr<Span> getActiveSpan() override {
    auto* span = envoy_dynamic_module_callback_http_get_active_span(host_plugin_ptr_);
    if (span == nullptr) {
      return nullptr;
    }
    return std::make_unique<SpanImpl>(host_plugin_ptr_, span);
  }

  std::optional<std::string_view> getClusterName() override {
    BufferView value{nullptr, 0};
    const bool found = envoy_dynamic_module_callback_http_get_cluster_name(
        host_plugin_ptr_, reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(&value));
    return bufferViewToOptionalStringView(value, found);
  }

  std::optional<ClusterHostCounts> getClusterHostCounts(uint32_t priority) override {
    size_t total = 0;
    size_t healthy = 0;
    size_t degraded = 0;
    const bool found = envoy_dynamic_module_callback_http_get_cluster_host_count(
        host_plugin_ptr_, priority, &total, &healthy, &degraded);
    if (!found) {
      return {};
    }
    return ClusterHostCounts{static_cast<uint64_t>(total), static_cast<uint64_t>(healthy),
                             static_cast<uint64_t>(degraded)};
  }

  bool setUpstreamOverrideHost(std::string_view host, bool strict) override {
    return envoy_dynamic_module_callback_http_set_upstream_override_host(
        host_plugin_ptr_, envoy_dynamic_module_type_module_buffer{host.data(), host.size()},
        strict);
  }

  void resetStream(HttpFilterStreamResetReason reason, std::string_view details) override {
    envoy_dynamic_module_callback_http_filter_reset_stream(
        host_plugin_ptr_,
        static_cast<envoy_dynamic_module_type_http_filter_stream_reset_reason>(reason),
        envoy_dynamic_module_type_module_buffer{details.data(), details.size()});
  }

  void sendGoAwayAndClose(bool graceful) override {
    envoy_dynamic_module_callback_http_filter_send_go_away_and_close(host_plugin_ptr_, graceful);
  }

  bool recreateStream(std::span<const HeaderView> headers) override {
    return envoy_dynamic_module_callback_http_filter_recreate_stream(
        host_plugin_ptr_,
        const_cast<envoy_dynamic_module_type_module_http_header*>(
            reinterpret_cast<const envoy_dynamic_module_type_module_http_header*>(headers.data())),
        headers.size());
  }

  HeaderMap& requestHeaders() override { return request_headers_; }

  BodyBuffer& bufferedRequestBody() override { return buffered_request_body_; }

  BodyBuffer& receivedRequestBody() override { return received_request_body_; }

  HeaderMap& requestTrailers() override { return request_trailers_; }

  HeaderMap& responseHeaders() override { return response_headers_; }

  BodyBuffer& bufferedResponseBody() override { return buffered_response_body_; }

  BodyBuffer& receivedResponseBody() override { return received_response_body_; }

  bool receivedBufferedRequestBody() override {
    return envoy_dynamic_module_callback_http_received_buffered_request_body(host_plugin_ptr_);
  }

  bool receivedBufferedResponseBody() override {
    return envoy_dynamic_module_callback_http_received_buffered_response_body(host_plugin_ptr_);
  }

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

  std::pair<HttpCalloutInitResult, uint64_t> httpCallout(std::string_view cluster,
                                                         std::span<const HeaderView> headers,
                                                         std::string_view body, uint64_t timeout_ms,
                                                         HttpCalloutCallback& cb) override {
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
  startHttpStream(std::string_view cluster, std::span<const HeaderView> headers,
                  std::string_view body, bool end_of_stream, uint64_t timeout_ms,
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

  bool sendHttpStreamData(uint64_t stream_id, std::string_view body, bool end_of_stream) override {
    return envoy_dynamic_module_callback_http_stream_send_data(
        host_plugin_ptr_, stream_id,
        envoy_dynamic_module_type_module_buffer{body.data(), body.size()}, end_of_stream);
  }

  bool sendHttpStreamTrailers(uint64_t stream_id, std::span<const HeaderView> trailers) override {
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
                                     std::span<const BufferView> tags_values) override {
    return static_cast<MetricsResult>(
        envoy_dynamic_module_callback_http_filter_record_histogram_value(
            host_plugin_ptr_, id,
            const_cast<envoy_dynamic_module_type_module_buffer*>(
                reinterpret_cast<const envoy_dynamic_module_type_module_buffer*>(
                    tags_values.data())),
            tags_values.size(), value));
  }

  MetricsResult setGaugeValue(MetricID id, uint64_t value,
                              std::span<const BufferView> tags_values) override {
    return static_cast<MetricsResult>(envoy_dynamic_module_callback_http_filter_set_gauge(
        host_plugin_ptr_, id,
        const_cast<envoy_dynamic_module_type_module_buffer*>(
            reinterpret_cast<const envoy_dynamic_module_type_module_buffer*>(tags_values.data())),
        tags_values.size(), value));
  }

  MetricsResult incrementGaugeValue(MetricID id, uint64_t value,
                                    std::span<const BufferView> tags_values) override {
    return static_cast<MetricsResult>(envoy_dynamic_module_callback_http_filter_increment_gauge(
        host_plugin_ptr_, id,
        const_cast<envoy_dynamic_module_type_module_buffer*>(
            reinterpret_cast<const envoy_dynamic_module_type_module_buffer*>(tags_values.data())),
        tags_values.size(), value));
  }

  MetricsResult decrementGaugeValue(MetricID id, uint64_t value,
                                    std::span<const BufferView> tags_values) override {
    return static_cast<MetricsResult>(envoy_dynamic_module_callback_http_filter_decrement_gauge(
        host_plugin_ptr_, id,
        const_cast<envoy_dynamic_module_type_module_buffer*>(
            reinterpret_cast<const envoy_dynamic_module_type_module_buffer*>(tags_values.data())),
        tags_values.size(), value));
  }

  MetricsResult incrementCounterValue(MetricID id, uint64_t value,
                                      std::span<const BufferView> tags_values) override {
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
  void log(LogLevel level, std::string_view message) override {
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

  std::map<uint64_t, HttpCalloutCallback*> callout_callbacks_;
  std::map<uint64_t, HttpStreamCallback*> stream_callbacks_;

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
  defineHistogram(std::string_view name, std::span<const BufferView> tags_keys) override {
    size_t metric_id = 0;
    auto result = static_cast<MetricsResult>(
        envoy_dynamic_module_callback_http_filter_config_define_histogram(
            host_config_ptr_, envoy_dynamic_module_type_module_buffer{name.data(), name.size()},
            const_cast<envoy_dynamic_module_type_module_buffer*>(
                reinterpret_cast<const envoy_dynamic_module_type_module_buffer*>(tags_keys.data())),
            tags_keys.size(), &metric_id));
    return {metric_id, result};
  }

  std::pair<MetricID, MetricsResult> defineGauge(std::string_view name,
                                                 std::span<const BufferView> tags_keys) override {
    size_t metric_id = 0;
    auto result =
        static_cast<MetricsResult>(envoy_dynamic_module_callback_http_filter_config_define_gauge(
            host_config_ptr_, envoy_dynamic_module_type_module_buffer{name.data(), name.size()},
            const_cast<envoy_dynamic_module_type_module_buffer*>(
                reinterpret_cast<const envoy_dynamic_module_type_module_buffer*>(tags_keys.data())),
            tags_keys.size(), &metric_id));
    return {metric_id, result};
  }

  std::pair<MetricID, MetricsResult> defineCounter(std::string_view name,
                                                   std::span<const BufferView> tags_keys) override {
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
  void log(LogLevel level, std::string_view message) override {
    return envoy_dynamic_module_callback_log(
        static_cast<envoy_dynamic_module_type_log_level>(level),
        envoy_dynamic_module_type_module_buffer{message.data(), message.size()});
  }

  std::pair<HttpCalloutInitResult, uint64_t> httpCallout(std::string_view cluster,
                                                         std::span<const HeaderView> headers,
                                                         std::string_view body, uint64_t timeout_ms,
                                                         HttpCalloutCallback& cb) override {
    uint64_t callout_id_out = 0;
    auto result = envoy_dynamic_module_callback_http_filter_config_http_callout(
        host_config_ptr_, &callout_id_out,
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
  startHttpStream(std::string_view cluster, std::span<const HeaderView> headers,
                  std::string_view body, bool end_of_stream, uint64_t timeout_ms,
                  HttpStreamCallback& cb) override {
    uint64_t stream_id_out = 0;
    auto result = envoy_dynamic_module_callback_http_filter_config_start_http_stream(
        host_config_ptr_, &stream_id_out,
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

  bool sendHttpStreamData(uint64_t stream_id, std::string_view body, bool end_of_stream) override {
    return envoy_dynamic_module_callback_http_filter_config_stream_send_data(
        host_config_ptr_, stream_id,
        envoy_dynamic_module_type_module_buffer{body.data(), body.size()}, end_of_stream);
  }

  bool sendHttpStreamTrailers(uint64_t stream_id, std::span<const HeaderView> trailers) override {
    return envoy_dynamic_module_callback_http_filter_config_stream_send_trailers(
        host_config_ptr_, stream_id,
        const_cast<envoy_dynamic_module_type_module_http_header*>(
            reinterpret_cast<const envoy_dynamic_module_type_module_http_header*>(trailers.data())),
        trailers.size());
  }

  void resetHttpStream(uint64_t stream_id) override {
    envoy_dynamic_module_callback_http_filter_config_reset_http_stream(host_config_ptr_, stream_id);
  }

  std::shared_ptr<Scheduler> getScheduler() override {
    if (!scheduler_) {
      scheduler_ = std::make_shared<ConfigSchedulerImpl>(host_config_ptr_);
    }
    return scheduler_;
  }

  // Use map because we expect the number of concurrent callouts/streams to be
  // very small.
  std::map<uint64_t, HttpCalloutCallback*> callout_callbacks_;
  std::map<uint64_t, HttpStreamCallback*> stream_callbacks_;
  std::shared_ptr<ConfigSchedulerImpl> scheduler_;

private:
  void* host_config_ptr_;
};

class DummyLoggerHandle {
public:
  bool logEnabled(LogLevel level) {
    return envoy_dynamic_module_callback_log_enabled(
        static_cast<envoy_dynamic_module_type_log_level>(level));
  }
  void log(LogLevel level, std::string_view message) {
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
  std::unique_ptr<HttpFilterConfigHandleImpl> config_handle_;
  std::unique_ptr<HttpFilterFactory> factory_;
};

// Extern C exports
extern "C" {

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_http_filter_config_module_ptr
envoy_dynamic_module_on_http_filter_config_new(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  auto config_handle = std::make_unique<HttpFilterConfigHandleImpl>(filter_config_envoy_ptr);
  std::string_view name_view(name.ptr, name.length);
  std::string_view config_view(config.ptr, config.length);

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
  std::string_view name_view(name.ptr, name.length);
  std::string_view config_view(config.ptr, config.length);

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
  // So the plugin_ field will never be null as long as the plugin handle is alive.
  plugin_handle->plugin_ = std::move(plugin);

  return wrapPointer(plugin_handle.release());
}

void envoy_dynamic_module_on_http_filter_destroy(
    envoy_dynamic_module_type_http_filter_module_ptr filter_module_ptr) {
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);
  if (plugin_handle == nullptr) {
    return;
  }
  plugin_handle->plugin_->onDestroy();
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
  auto* plugin_handle = unwrapPointer<HttpFilterHandleImpl>(filter_module_ptr);
  if (plugin_handle == nullptr) {
    return envoy_dynamic_module_type_on_http_filter_local_reply_status_Continue;
  }
  plugin_handle->local_reply_sent_ = true;
  return static_cast<envoy_dynamic_module_type_on_http_filter_local_reply_status>(
      plugin_handle->plugin_->onLocalReply(
          response_code,
          std::string_view(details.ptr == nullptr ? "" : details.ptr, details.length),
          reset_imminent));
}

void envoy_dynamic_module_on_http_filter_config_http_callout_done(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t callout_id,
    envoy_dynamic_module_type_http_callout_result result,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size,
    envoy_dynamic_module_type_envoy_buffer* body_chunks, size_t body_chunks_size) {
  auto* factory_wrapper = unwrapPointer<HttpFilterFactoryWrapper>(filter_config_ptr);
  if (!factory_wrapper) {
    return;
  }
  auto* config_handle =
      static_cast<HttpFilterConfigHandleImpl*>(factory_wrapper->config_handle_.get());
  if (!config_handle) {
    return;
  }

  auto* typed_headers = reinterpret_cast<HeaderView*>(headers);
  auto* typed_body_chunks = reinterpret_cast<BufferView*>(body_chunks);

  auto it = config_handle->callout_callbacks_.find(callout_id);
  if (it != config_handle->callout_callbacks_.end()) {
    auto* cb = it->second;
    config_handle->callout_callbacks_.erase(it);
    cb->onHttpCalloutDone(static_cast<HttpCalloutResult>(result), {typed_headers, headers_size},
                          {typed_body_chunks, body_chunks_size});
  }
}

void envoy_dynamic_module_on_http_filter_config_http_stream_headers(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_envoy_http_header* headers, size_t headers_size, bool end_stream) {
  auto* factory_wrapper = unwrapPointer<HttpFilterFactoryWrapper>(filter_config_ptr);
  if (!factory_wrapper) {
    return;
  }
  auto* config_handle =
      static_cast<HttpFilterConfigHandleImpl*>(factory_wrapper->config_handle_.get());
  if (!config_handle) {
    return;
  }

  auto it = config_handle->stream_callbacks_.find(stream_id);
  if (it != config_handle->stream_callbacks_.end()) {
    auto* typed_headers = reinterpret_cast<HeaderView*>(headers);
    it->second->onHttpStreamHeaders(stream_id, {typed_headers, headers_size}, end_stream);
  }
}

void envoy_dynamic_module_on_http_filter_config_http_stream_data(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t stream_id,
    const envoy_dynamic_module_type_envoy_buffer* chunks, size_t chunks_size, bool end_stream) {
  auto* factory_wrapper = unwrapPointer<HttpFilterFactoryWrapper>(filter_config_ptr);
  if (!factory_wrapper) {
    return;
  }
  auto* config_handle =
      static_cast<HttpFilterConfigHandleImpl*>(factory_wrapper->config_handle_.get());
  if (!config_handle) {
    return;
  }

  auto it = config_handle->stream_callbacks_.find(stream_id);
  if (it != config_handle->stream_callbacks_.end()) {
    auto* typed_chunks =
        reinterpret_cast<BufferView*>(const_cast<envoy_dynamic_module_type_envoy_buffer*>(chunks));
    it->second->onHttpStreamData(stream_id, {typed_chunks, chunks_size}, end_stream);
  }
}

void envoy_dynamic_module_on_http_filter_config_http_stream_trailers(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_envoy_http_header* trailers, size_t trailers_size) {
  auto* factory_wrapper = unwrapPointer<HttpFilterFactoryWrapper>(filter_config_ptr);
  if (!factory_wrapper) {
    return;
  }
  auto* config_handle =
      static_cast<HttpFilterConfigHandleImpl*>(factory_wrapper->config_handle_.get());
  if (!config_handle) {
    return;
  }

  auto it = config_handle->stream_callbacks_.find(stream_id);
  if (it != config_handle->stream_callbacks_.end()) {
    auto* typed_trailers = reinterpret_cast<HeaderView*>(trailers);
    it->second->onHttpStreamTrailers(stream_id, {typed_trailers, trailers_size});
  }
}

void envoy_dynamic_module_on_http_filter_config_http_stream_complete(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t stream_id) {
  auto* factory_wrapper = unwrapPointer<HttpFilterFactoryWrapper>(filter_config_ptr);
  if (!factory_wrapper) {
    return;
  }
  auto* config_handle =
      static_cast<HttpFilterConfigHandleImpl*>(factory_wrapper->config_handle_.get());
  if (!config_handle) {
    return;
  }

  auto it = config_handle->stream_callbacks_.find(stream_id);
  if (it != config_handle->stream_callbacks_.end()) {
    auto* cb = it->second;
    config_handle->stream_callbacks_.erase(it);
    cb->onHttpStreamComplete(stream_id);
  }
}

void envoy_dynamic_module_on_http_filter_config_http_stream_reset(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t stream_id,
    envoy_dynamic_module_type_http_stream_reset_reason reason) {
  auto* factory_wrapper = unwrapPointer<HttpFilterFactoryWrapper>(filter_config_ptr);
  if (!factory_wrapper) {
    return;
  }
  auto* config_handle =
      static_cast<HttpFilterConfigHandleImpl*>(factory_wrapper->config_handle_.get());
  if (!config_handle) {
    return;
  }

  auto it = config_handle->stream_callbacks_.find(stream_id);
  if (it != config_handle->stream_callbacks_.end()) {
    auto* cb = it->second;
    config_handle->stream_callbacks_.erase(it);
    cb->onHttpStreamReset(stream_id, static_cast<HttpStreamResetReason>(reason));
  }
}

void envoy_dynamic_module_on_http_filter_config_scheduled(
    envoy_dynamic_module_type_http_filter_config_envoy_ptr,
    envoy_dynamic_module_type_http_filter_config_module_ptr filter_config_ptr, uint64_t event_id) {
  auto* factory_wrapper = unwrapPointer<HttpFilterFactoryWrapper>(filter_config_ptr);
  if (factory_wrapper == nullptr || factory_wrapper->config_handle_ == nullptr ||
      factory_wrapper->config_handle_->scheduler_ == nullptr) {
    return;
  }
  factory_wrapper->config_handle_->scheduler_->onScheduled(event_id);
}
}

} // namespace DynamicModules
} // namespace Envoy
