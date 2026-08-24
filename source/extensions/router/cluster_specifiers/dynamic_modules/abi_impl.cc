#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>

#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/abi_context_accessors.h"
#include "source/extensions/router/cluster_specifiers/dynamic_modules/cluster_specifier.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace DynamicModules {

using Envoy::Extensions::DynamicModules::ContextAccessor;
using Envoy::Extensions::DynamicModules::HeadersMapOptConstRef;

namespace {

ClusterSpecifierContext*
clusterSpecifierContext(envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr ptr) {
  return static_cast<ClusterSpecifierContext*>(ptr);
}

// Durations cross the ABI boundary as unsigned, so clamp them to the ceiling that
// TimerUtils::durationToTimeval already imposes. Clamping to the millisecond representation maximum
// instead would overflow the float scale factor that the stream idle timer applies, which reaches
// the timer as a negative duration and fires it immediately.
std::chrono::milliseconds toMilliseconds(uint64_t value_ms) {
  using Rep = std::chrono::milliseconds::rep;
  constexpr uint64_t max_ms = static_cast<uint64_t>(
      std::chrono::milliseconds(std::chrono::seconds(std::numeric_limits<int32_t>::max())).count());
  return std::chrono::milliseconds(static_cast<Rep>(std::min(value_ms, max_ms)));
}

} // namespace

extern "C" {

size_t envoy_dynamic_module_callback_cluster_specifier_get_request_headers_size(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr) {
  return clusterSpecifierContext(context_envoy_ptr)->headers.size();
}

bool envoy_dynamic_module_callback_cluster_specifier_get_request_headers(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  return ContextAccessor::getHeaders(HeadersMapOptConstRef(context->headers), result_headers);
}

bool envoy_dynamic_module_callback_cluster_specifier_get_request_header_value(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result,
    size_t index, size_t* total_count_out) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  return ContextAccessor::getHeaderValue(HeadersMapOptConstRef(context->headers), key, result,
                                         index, total_count_out);
}

bool envoy_dynamic_module_callback_cluster_specifier_get_attribute_string(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  return ContextAccessor::getAttributeString(context->stream_info, attribute_id, result);
}

bool envoy_dynamic_module_callback_cluster_specifier_get_attribute_int(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, uint64_t* result) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  return ContextAccessor::getAttributeInt(context->stream_info, attribute_id, result);
}

bool envoy_dynamic_module_callback_cluster_specifier_get_attribute_bool(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, bool* result) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  return ContextAccessor::getAttributeBool(context->stream_info, attribute_id, result);
}

bool envoy_dynamic_module_callback_cluster_specifier_get_dynamic_metadata(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer filter_name,
    envoy_dynamic_module_type_module_buffer path, envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  return ContextAccessor::getDynamicMetadata(context->stream_info, filter_name, path, result);
}

bool envoy_dynamic_module_callback_cluster_specifier_get_route_name(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* result) {
  const std::string& route_name = clusterSpecifierContext(context_envoy_ptr)->route_name;
  if (route_name.empty()) {
    return false;
  }
  *result = {route_name.data(), route_name.size()};
  return true;
}

uint64_t envoy_dynamic_module_callback_cluster_specifier_get_random_value(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr) {
  return clusterSpecifierContext(context_envoy_ptr)->random_value;
}

void envoy_dynamic_module_callback_cluster_specifier_set_cluster_name(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer cluster_name) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  context->selection.cluster_name.assign(cluster_name.ptr, cluster_name.length);
}

void envoy_dynamic_module_callback_cluster_specifier_set_timeout(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    uint64_t timeout_ms) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  context->selection.timeout = toMilliseconds(timeout_ms);
}

void envoy_dynamic_module_callback_cluster_specifier_set_idle_timeout(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    uint64_t idle_timeout_ms) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  context->selection.idle_timeout = toMilliseconds(idle_timeout_ms);
}

void envoy_dynamic_module_callback_cluster_specifier_set_request_body_buffer_limit(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    uint64_t limit_bytes) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  context->selection.request_body_buffer_limit = limit_bytes;
}

void envoy_dynamic_module_callback_cluster_specifier_set_priority(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_resource_priority priority) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
  context->selection.priority = priority == envoy_dynamic_module_type_resource_priority_High
                                    ? Upstream::ResourcePriority::High
                                    : Upstream::ResourcePriority::Default;
}

bool envoy_dynamic_module_callback_cluster_specifier_set_route_action_override(
    envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name) {
  auto* context = clusterSpecifierContext(context_envoy_ptr);
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
  context->selection.route_action_override = entry;
  return true;
}

} // extern "C"

} // namespace DynamicModules
} // namespace Router
} // namespace Extensions
} // namespace Envoy
