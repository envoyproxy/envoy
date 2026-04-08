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

void fillBufferChunks(const Envoy::Buffer::Instance& buffer,
                      envoy_dynamic_module_type_envoy_buffer* result_buffer_vector) {
  Envoy::Buffer::RawSliceVector raw_slices = buffer.getRawSlices();
  size_t counter = 0;
  for (const auto& slice : raw_slices) {
    result_buffer_vector[counter].length = slice.len_;
    result_buffer_vector[counter].ptr = static_cast<char*>(slice.mem_);
    counter++;
  }
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
    envoy_dynamic_module_type_envoy_buffer* result_buffer, size_t* result_buffer_length) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  auto* buffer = bridge->requestBuffer();
  if (buffer == nullptr || buffer->length() == 0) {
    *result_buffer_length = 0;
    return;
  }
  fillBufferChunks(*buffer, result_buffer);
  *result_buffer_length = buffer->getRawSlices(std::nullopt).size();
}

// ----------------------- Response Buffer Operations --------------------------

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_response_buffer(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result_buffer, size_t* result_buffer_length) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  auto* buffer = bridge->responseBuffer();
  if (buffer == nullptr || buffer->length() == 0) {
    *result_buffer_length = 0;
    return;
  }
  fillBufferChunks(*buffer, result_buffer);
  *result_buffer_length = buffer->getRawSlices(std::nullopt).size();
}

// ----------------------- Send Upstream Data ----------------------------------

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_upstream_data(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data, bool end_stream) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  bridge->sendUpstreamData(absl::string_view(data.ptr, data.length), end_stream);
}

// ----------------------- Send Response Operations ----------------------------

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    uint32_t status_code, envoy_dynamic_module_type_module_http_header* headers_vector,
    size_t headers_vector_size, envoy_dynamic_module_type_module_buffer body) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  bridge->sendResponse(status_code, headers_vector, headers_vector_size,
                       absl::string_view(body.ptr, body.length));
}

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_headers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    uint32_t status_code, envoy_dynamic_module_type_module_http_header* headers_vector,
    size_t headers_vector_size, bool end_stream) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  bridge->sendResponseHeaders(status_code, headers_vector, headers_vector_size, end_stream);
}

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_data(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data, bool end_stream) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  bridge->sendResponseData(absl::string_view(data.ptr, data.length), end_stream);
}

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_trailers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_http_header* trailers_vector, size_t trailers_vector_size) {
  auto* bridge = getBridge(bridge_envoy_ptr);
  bridge->sendResponseTrailers(trailers_vector, trailers_vector_size);
}

} // extern "C"
