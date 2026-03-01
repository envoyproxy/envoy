// NOLINT(namespace-envoy)

#include "source/common/http/header_utility.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/upstreams/http/dynamic_modules/upstream_request.h"

namespace {

Envoy::Extensions::Upstreams::Http::DynamicModules::HttpTcpBridge*
getBridge(envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr) {
  return static_cast<Envoy::Extensions::Upstreams::Http::DynamicModules::HttpTcpBridge*>(
      bridge_envoy_ptr);
}

} // namespace

extern "C" {

// ----------------------- Request Header Operations ---------------------------

bool envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result,
    size_t index, size_t* total_count_out) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  const auto* headers = bridge->requestHeaders();
  if (headers == nullptr) {
    *result = {.ptr = nullptr, .length = 0};
    if (total_count_out != nullptr) {
      *total_count_out = 0;
    }
    return false;
  }

  absl::string_view key_view(key.ptr, key.length);
  const auto values = headers->get(Envoy::Http::LowerCaseString(key_view));
  if (total_count_out != nullptr) {
    *total_count_out = values.size();
  }

  if (index >= values.size()) {
    *result = {.ptr = nullptr, .length = 0};
    return false;
  }

  const auto value = values[index]->value().getStringView();
  *result = {.ptr = const_cast<char*>(value.data()), .length = value.size()};
  return true;
}

size_t envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers_size(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  const auto* headers = bridge->requestHeaders();
  return headers != nullptr ? headers->size() : 0;
}

bool envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  const auto* headers = bridge->requestHeaders();
  if (headers == nullptr) {
    return false;
  }
  size_t i = 0;
  headers->iterate([&i, &result_headers](
                       const Envoy::Http::HeaderEntry& header) -> Envoy::Http::HeaderMap::Iterate {
    auto& key = header.key();
    result_headers[i].key_ptr = const_cast<char*>(key.getStringView().data());
    result_headers[i].key_length = key.size();
    auto& value = header.value();
    result_headers[i].value_ptr = const_cast<char*>(value.getStringView().data());
    result_headers[i].value_length = value.size();
    i++;
    return Envoy::Http::HeaderMap::Iterate::Continue;
  });
  return true;
}

// ----------------------- Request Buffer Operations ---------------------------

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    uintptr_t* result_buffer_ptr, size_t* result_buffer_length) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  auto& buffer = bridge->requestBuffer();
  if (buffer.length() == 0) {
    *result_buffer_ptr = 0;
    *result_buffer_length = 0;
    return;
  }
  auto slices = buffer.getRawSlices();
  if (slices.empty()) {
    *result_buffer_ptr = 0;
    *result_buffer_length = 0;
    return;
  }
  // Linearize the buffer so we can return a single contiguous pointer.
  buffer.linearize(buffer.length());
  auto linearized = buffer.getRawSlices();
  *result_buffer_ptr = reinterpret_cast<uintptr_t>(linearized[0].mem_);
  *result_buffer_length = linearized[0].len_;
}

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_request_buffer(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  auto& buffer = bridge->requestBuffer();
  buffer.drain(buffer.length());
  if (data.length > 0) {
    buffer.add(data.ptr, data.length);
  }
}

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_drain_request_buffer(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr, size_t length) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  auto& buffer = bridge->requestBuffer();
  buffer.drain(std::min(static_cast<uint64_t>(length), buffer.length()));
}

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_append_request_buffer(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  if (data.length > 0) {
    bridge->requestBuffer().add(data.ptr, data.length);
  }
}

// ----------------------- Response Buffer Operations --------------------------

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_response_buffer(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    uintptr_t* result_buffer_ptr, size_t* result_buffer_length) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  auto* buffer = bridge->responseBuffer();
  if (buffer == nullptr || buffer->length() == 0) {
    *result_buffer_ptr = 0;
    *result_buffer_length = 0;
    return;
  }
  buffer->linearize(buffer->length());
  auto slices = buffer->getRawSlices();
  if (slices.empty()) {
    *result_buffer_ptr = 0;
    *result_buffer_length = 0;
    return;
  }
  *result_buffer_ptr = reinterpret_cast<uintptr_t>(slices[0].mem_);
  *result_buffer_length = slices[0].len_;
}

// ----------------------- Response Header Operations --------------------------

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_response_header(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  auto& headers = bridge->responseHeaders();
  if (headers == nullptr) {
    return;
  }
  absl::string_view key_view(key.ptr, key.length);
  absl::string_view value_view(value.ptr, value.length);
  headers->setCopy(Envoy::Http::LowerCaseString(key_view), value_view);
}

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_add_response_header(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  auto& headers = bridge->responseHeaders();
  if (headers == nullptr) {
    return;
  }
  absl::string_view key_view(key.ptr, key.length);
  absl::string_view value_view(value.ptr, value.length);
  headers->addCopy(Envoy::Http::LowerCaseString(key_view), value_view);
}

// ----------------------- Response Body Operations ----------------------------

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_response_body(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  auto& body = bridge->responseBody();
  body.drain(body.length());
  if (data.length > 0) {
    body.add(data.ptr, data.length);
  }
}

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_append_response_body(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  if (data.length > 0) {
    bridge->responseBody().add(data.ptr, data.length);
  }
}

// ----------------------- Response Trailer Operations -------------------------

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_response_trailer(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  auto& trailers = bridge->responseTrailers();
  if (trailers == nullptr) {
    trailers = Envoy::Http::createHeaderMap<Envoy::Http::ResponseTrailerMapImpl>({});
  }
  absl::string_view key_view(key.ptr, key.length);
  absl::string_view value_view(value.ptr, value.length);
  trailers->setCopy(Envoy::Http::LowerCaseString(key_view), value_view);
}

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_add_response_trailer(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  auto& trailers = bridge->responseTrailers();
  if (trailers == nullptr) {
    trailers = Envoy::Http::createHeaderMap<Envoy::Http::ResponseTrailerMapImpl>({});
  }
  absl::string_view key_view(key.ptr, key.length);
  absl::string_view value_view(value.ptr, value.length);
  trailers->addCopy(Envoy::Http::LowerCaseString(key_view), value_view);
}

} // extern "C"
