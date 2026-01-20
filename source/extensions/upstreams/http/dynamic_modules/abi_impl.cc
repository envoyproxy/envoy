#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/upstreams/http/dynamic_modules/http_tcp_bridge.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace DynamicModules {
namespace {

DynamicModuleHttpTcpBridge*
getBridge(envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr ptr) {
  return static_cast<DynamicModuleHttpTcpBridge*>(ptr);
}

} // namespace
} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy

using namespace Envoy::Extensions::Upstreams::Http::DynamicModules;

extern "C" {

size_t envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers_count(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr) {
  if (bridge_envoy_ptr == nullptr) {
    return 0;
  }
  return getBridge(bridge_envoy_ptr)->getRequestHeadersCount();
}

bool envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr, size_t index,
    envoy_dynamic_module_type_envoy_buffer* key, envoy_dynamic_module_type_envoy_buffer* value) {
  if (bridge_envoy_ptr == nullptr) {
    return false;
  }
  return getBridge(bridge_envoy_ptr)->getRequestHeader(index, key, value);
}

bool envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header_value(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* value) {
  if (bridge_envoy_ptr == nullptr) {
    return false;
  }
  return getBridge(bridge_envoy_ptr)->getRequestHeaderValue(key, value);
}

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_upstream_buffer(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    uintptr_t* result_buffer_ptr, size_t* result_buffer_length) {
  if (bridge_envoy_ptr == nullptr) {
    *result_buffer_ptr = 0;
    *result_buffer_length = 0;
    return;
  }
  getBridge(bridge_envoy_ptr)->getUpstreamBuffer(result_buffer_ptr, result_buffer_length);
}

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_upstream_buffer(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data) {
  if (bridge_envoy_ptr == nullptr) {
    return;
  }
  getBridge(bridge_envoy_ptr)->setUpstreamBuffer(data);
}

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_append_upstream_buffer(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer data) {
  if (bridge_envoy_ptr == nullptr) {
    return;
  }
  getBridge(bridge_envoy_ptr)->appendUpstreamBuffer(data);
}

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_downstream_buffer(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    uintptr_t* result_buffer_ptr, size_t* result_buffer_length) {
  if (bridge_envoy_ptr == nullptr) {
    *result_buffer_ptr = 0;
    *result_buffer_length = 0;
    return;
  }
  getBridge(bridge_envoy_ptr)->getDownstreamBuffer(result_buffer_ptr, result_buffer_length);
}

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_drain_downstream_buffer(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr, size_t length) {
  if (bridge_envoy_ptr == nullptr) {
    return;
  }
  getBridge(bridge_envoy_ptr)->drainDownstreamBuffer(length);
}

bool envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_response_header(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  if (bridge_envoy_ptr == nullptr) {
    return false;
  }
  return getBridge(bridge_envoy_ptr)->setResponseHeader(key, value);
}

bool envoy_dynamic_module_callback_upstream_http_tcp_bridge_add_response_header(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value) {
  if (bridge_envoy_ptr == nullptr) {
    return false;
  }
  return getBridge(bridge_envoy_ptr)->addResponseHeader(key, value);
}

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    bool end_of_stream) {
  if (bridge_envoy_ptr == nullptr) {
    return;
  }
  getBridge(bridge_envoy_ptr)->sendResponse(end_of_stream);
}

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_route_name(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  if (bridge_envoy_ptr == nullptr) {
    result->ptr = nullptr;
    result->length = 0;
    return;
  }
  getBridge(bridge_envoy_ptr)->getRouteName(result);
}

void envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_cluster_name(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr bridge_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  if (bridge_envoy_ptr == nullptr) {
    result->ptr = nullptr;
    result->length = 0;
    return;
  }
  getBridge(bridge_envoy_ptr)->getClusterName(result);
}

} // extern "C"
