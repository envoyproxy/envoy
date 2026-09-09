#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/abi_context_accessors.h"
#include "source/extensions/router/route_extension/dynamic_modules/route_extension.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace DynamicModules {

using Envoy::Extensions::DynamicModules::ContextAccessor;
using Envoy::Extensions::DynamicModules::HeadersMapOptConstRef;

namespace {
RouteExtensionContext*
routeExtensionContext(envoy_dynamic_module_type_route_extension_context_envoy_ptr ptr) {
  return static_cast<RouteExtensionContext*>(ptr);
}
} // namespace

extern "C" {

size_t envoy_dynamic_module_callback_route_extension_get_request_headers_size(
    envoy_dynamic_module_type_route_extension_context_envoy_ptr context_envoy_ptr) {
  return routeExtensionContext(context_envoy_ptr)->headers.size();
}

bool envoy_dynamic_module_callback_route_extension_get_request_headers(
    envoy_dynamic_module_type_route_extension_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  auto* context = routeExtensionContext(context_envoy_ptr);
  return ContextAccessor::getHeaders(HeadersMapOptConstRef(context->headers), result_headers);
}

bool envoy_dynamic_module_callback_route_extension_get_request_header_value(
    envoy_dynamic_module_type_route_extension_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result,
    size_t index, size_t* total_count_out) {
  auto* context = routeExtensionContext(context_envoy_ptr);
  return ContextAccessor::getHeaderValue(HeadersMapOptConstRef(context->headers), key, result,
                                         index, total_count_out);
}

uint64_t envoy_dynamic_module_callback_route_extension_get_random_value(
    envoy_dynamic_module_type_route_extension_context_envoy_ptr context_envoy_ptr) {
  return routeExtensionContext(context_envoy_ptr)->random_value;
}

void envoy_dynamic_module_callback_route_extension_set_cluster_name(
    envoy_dynamic_module_type_route_extension_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer cluster_name) {
  auto* context = routeExtensionContext(context_envoy_ptr);
  context->overrides.cluster_name.assign(cluster_name.ptr, cluster_name.length);
}

bool envoy_dynamic_module_callback_route_extension_set_route_action_override(
    envoy_dynamic_module_type_route_extension_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name) {
  auto* context = routeExtensionContext(context_envoy_ptr);
  const absl::string_view name_view(name.ptr, name.length);
  const RouteActionOverride* entry = context->config.routeActionOverride(name_view);
  if (entry == nullptr) {
    // The module and the route action overrides are configured separately, so a name that does not
    // resolve is usually a mismatch between the two rather than a deliberate probe. Rate limit the
    // warning because a mismatch repeats on every request.
    ENVOY_LOG_EVERY_POW_2_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), warn,
        "dynamic module selected the unknown route action override '{}', so the route action "
        "properties of the matched route stay in effect",
        name_view);
    return false;
  }
  context->overrides.route_action_override = entry;
  return true;
}

} // extern "C"

} // namespace DynamicModules
} // namespace Router
} // namespace Extensions
} // namespace Envoy
