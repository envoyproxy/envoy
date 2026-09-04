#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>

#include "source/common/common/logger.h"
#include "source/common/config/metadata.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/abi_context_accessors.h"
#include "source/extensions/router/route_providers/dynamic_modules/route_provider.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace RouteProviders {
namespace DynamicModules {

using Envoy::Extensions::DynamicModules::ContextAccessor;
using Envoy::Extensions::DynamicModules::HeadersMapOptConstRef;

namespace {

RouteProviderContext*
routeProviderContext(envoy_dynamic_module_type_route_provider_context_envoy_ptr ptr) {
  return static_cast<RouteProviderContext*>(ptr);
}

// Durations cross the ABI boundary as unsigned, so clamp them to the ceiling that
// TimerUtils::durationToTimeval already imposes.
std::chrono::milliseconds toMilliseconds(uint64_t value_ms) {
  using Rep = std::chrono::milliseconds::rep;
  constexpr uint64_t max_ms = static_cast<uint64_t>(
      std::chrono::milliseconds(std::chrono::seconds(std::numeric_limits<int32_t>::max())).count());
  return std::chrono::milliseconds(static_cast<Rep>(std::min(value_ms, max_ms)));
}

HeaderMutationType
toHeaderMutationType(envoy_dynamic_module_type_route_provider_header_mutation m) {
  switch (m) {
  case envoy_dynamic_module_type_route_provider_header_mutation_Overwrite:
    return HeaderMutationType::Overwrite;
  case envoy_dynamic_module_type_route_provider_header_mutation_Remove:
    return HeaderMutationType::Remove;
  default:
    return HeaderMutationType::Append;
  }
}

// Returns a mutable handle to the route metadata value the module is setting, creating the
// namespace and key when they are absent.
Protobuf::Value& mutableRouteMetadataValue(RouteProviderContext* context,
                                           envoy_dynamic_module_type_module_buffer ns,
                                           envoy_dynamic_module_type_module_buffer key) {
  return Envoy::Config::Metadata::mutableMetadataValue(context->selection.route_metadata,
                                                       std::string(ns.ptr, ns.length),
                                                       std::string(key.ptr, key.length));
}

} // namespace

extern "C" {

size_t envoy_dynamic_module_callback_route_provider_get_request_headers_size(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr) {
  return routeProviderContext(context_envoy_ptr)->headers.size();
}

bool envoy_dynamic_module_callback_route_provider_get_request_headers(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header* result_headers) {
  auto* context = routeProviderContext(context_envoy_ptr);
  return ContextAccessor::getHeaders(HeadersMapOptConstRef(context->headers), result_headers);
}

bool envoy_dynamic_module_callback_route_provider_get_request_header_value(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_envoy_buffer* result,
    size_t index, size_t* total_count_out) {
  auto* context = routeProviderContext(context_envoy_ptr);
  return ContextAccessor::getHeaderValue(HeadersMapOptConstRef(context->headers), key, result,
                                         index, total_count_out);
}

bool envoy_dynamic_module_callback_route_provider_get_attribute_string(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id,
    envoy_dynamic_module_type_envoy_buffer* result) {
  auto* context = routeProviderContext(context_envoy_ptr);
  return ContextAccessor::getAttributeString(context->stream_info, attribute_id, result);
}

bool envoy_dynamic_module_callback_route_provider_get_attribute_int(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, uint64_t* result) {
  auto* context = routeProviderContext(context_envoy_ptr);
  return ContextAccessor::getAttributeInt(context->stream_info, attribute_id, result);
}

bool envoy_dynamic_module_callback_route_provider_get_attribute_bool(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_attribute_id attribute_id, bool* result) {
  auto* context = routeProviderContext(context_envoy_ptr);
  return ContextAccessor::getAttributeBool(context->stream_info, attribute_id, result);
}

uint64_t envoy_dynamic_module_callback_route_provider_get_random_value(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr) {
  return routeProviderContext(context_envoy_ptr)->random_value;
}

bool envoy_dynamic_module_callback_route_provider_select_route(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr, size_t index) {
  auto* context = routeProviderContext(context_envoy_ptr);
  if (index >= context->config.routeTemplates().size()) {
    ENVOY_LOG_EVERY_POW_2_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), warn,
        "dynamic module selected the out of range route index {}, so no route is selected", index);
    return false;
  }
  context->selection.selected_index = index;
  return true;
}

void envoy_dynamic_module_callback_route_provider_set_cluster_name(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer cluster_name) {
  auto* context = routeProviderContext(context_envoy_ptr);
  context->selection.cluster_name.assign(cluster_name.ptr, cluster_name.length);
}

void envoy_dynamic_module_callback_route_provider_set_timeout(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    uint64_t timeout_ms) {
  routeProviderContext(context_envoy_ptr)->selection.timeout = toMilliseconds(timeout_ms);
}

void envoy_dynamic_module_callback_route_provider_set_idle_timeout(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    uint64_t idle_timeout_ms) {
  routeProviderContext(context_envoy_ptr)->selection.idle_timeout = toMilliseconds(idle_timeout_ms);
}

void envoy_dynamic_module_callback_route_provider_set_max_stream_duration(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    uint64_t max_stream_duration_ms) {
  routeProviderContext(context_envoy_ptr)->selection.max_stream_duration =
      toMilliseconds(max_stream_duration_ms);
}

void envoy_dynamic_module_callback_route_provider_set_priority(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_resource_priority priority) {
  auto* context = routeProviderContext(context_envoy_ptr);
  context->selection.priority = priority == envoy_dynamic_module_type_resource_priority_High
                                    ? Upstream::ResourcePriority::High
                                    : Upstream::ResourcePriority::Default;
}

void envoy_dynamic_module_callback_route_provider_set_request_body_buffer_limit(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    uint64_t limit_bytes) {
  routeProviderContext(context_envoy_ptr)->selection.request_body_buffer_limit = limit_bytes;
}

bool envoy_dynamic_module_callback_route_provider_set_route_action_override(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name) {
  auto* context = routeProviderContext(context_envoy_ptr);
  const absl::string_view name_view(name.ptr, name.length);
  const RouteActionOverride* entry = context->config.routeActionOverride(name_view);
  if (entry == nullptr) {
    ENVOY_LOG_EVERY_POW_2_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), warn,
        "dynamic module selected the unknown route action override '{}', so the route action "
        "properties of the route template stay in effect",
        name_view);
    return false;
  }
  context->selection.route_action_override = entry;
  return true;
}

bool envoy_dynamic_module_callback_route_provider_set_per_route_config_override(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name) {
  auto* context = routeProviderContext(context_envoy_ptr);
  const absl::string_view name_view(name.ptr, name.length);
  const PerRouteConfigOverride* entry = context->config.perRouteConfigOverride(name_view);
  if (entry == nullptr) {
    ENVOY_LOG_EVERY_POW_2_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), warn,
        "dynamic module selected the unknown per-route configuration override '{}', so the "
        "per-route configuration of the route template stays in effect",
        name_view);
    return false;
  }
  context->selection.per_route_config_override = entry;
  return true;
}

void envoy_dynamic_module_callback_route_provider_set_route_metadata_number(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    double value) {
  mutableRouteMetadataValue(routeProviderContext(context_envoy_ptr), ns, key)
      .set_number_value(value);
}

void envoy_dynamic_module_callback_route_provider_set_route_metadata_string(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_module_buffer value) {
  mutableRouteMetadataValue(routeProviderContext(context_envoy_ptr), ns, key)
      .set_string_value(std::string(value.ptr, value.length));
}

void envoy_dynamic_module_callback_route_provider_set_route_metadata_bool(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns, envoy_dynamic_module_type_module_buffer key,
    bool value) {
  mutableRouteMetadataValue(routeProviderContext(context_envoy_ptr), ns, key).set_bool_value(value);
}

void envoy_dynamic_module_callback_route_provider_set_route_metadata_struct(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns,
    envoy_dynamic_module_type_module_buffer serialized_struct) {
  Protobuf::Struct metadata_value;
  if (!metadata_value.ParseFromArray(serialized_struct.ptr,
                                     static_cast<int>(serialized_struct.length))) {
    ENVOY_LOG_EVERY_POW_2_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), warn,
        "dynamic module set route metadata with a buffer that does not parse as a "
        "google.protobuf.Struct, so the call is ignored");
    return;
  }
  auto* context = routeProviderContext(context_envoy_ptr);
  (*context->selection.route_metadata.mutable_filter_metadata())[std::string(ns.ptr, ns.length)]
      .MergeFrom(metadata_value);
}

void envoy_dynamic_module_callback_route_provider_set_route_typed_metadata(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer ns,
    envoy_dynamic_module_type_module_buffer serialized_any) {
  Protobuf::Any typed_value;
  if (!typed_value.ParseFromArray(serialized_any.ptr, static_cast<int>(serialized_any.length))) {
    ENVOY_LOG_EVERY_POW_2_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), warn,
        "dynamic module set typed route metadata with a buffer that does not parse as a "
        "google.protobuf.Any, so the call is ignored");
    return;
  }
  auto* context = routeProviderContext(context_envoy_ptr);
  // Assign rather than merge so the whole Any is swapped at once.
  (*context->selection.route_metadata
        .mutable_typed_filter_metadata())[std::string(ns.ptr, ns.length)] = typed_value;
}

void envoy_dynamic_module_callback_route_provider_set_request_header(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value,
    envoy_dynamic_module_type_route_provider_header_mutation mutation) {
  auto* context = routeProviderContext(context_envoy_ptr);
  context->selection.request_header_mutations.push_back(
      {Http::LowerCaseString(std::string(key.ptr, key.length)),
       std::string(value.ptr, value.length), toHeaderMutationType(mutation)});
}

void envoy_dynamic_module_callback_route_provider_set_response_header(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key, envoy_dynamic_module_type_module_buffer value,
    envoy_dynamic_module_type_route_provider_header_mutation mutation) {
  auto* context = routeProviderContext(context_envoy_ptr);
  context->selection.response_header_mutations.push_back(
      {Http::LowerCaseString(std::string(key.ptr, key.length)),
       std::string(value.ptr, value.length), toHeaderMutationType(mutation)});
}

void envoy_dynamic_module_callback_route_provider_set_path(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer path) {
  routeProviderContext(context_envoy_ptr)->selection.path = std::string(path.ptr, path.length);
}

bool envoy_dynamic_module_callback_route_provider_set_direct_response(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    uint32_t status_code, envoy_dynamic_module_type_module_buffer body) {
  if (status_code < 200 || status_code >= 600) {
    ENVOY_LOG_EVERY_POW_2_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), warn,
        "dynamic module set the out of range direct response code {}, so the call is ignored",
        status_code);
    return false;
  }
  auto* context = routeProviderContext(context_envoy_ptr);
  context->selection.terminal = TerminalResponse{false, static_cast<Http::Code>(status_code),
                                                 std::string(body.ptr, body.length)};
  return true;
}

bool envoy_dynamic_module_callback_route_provider_set_redirect(
    envoy_dynamic_module_type_route_provider_context_envoy_ptr context_envoy_ptr,
    uint32_t status_code, envoy_dynamic_module_type_module_buffer location) {
  if (status_code < 300 || status_code >= 400) {
    ENVOY_LOG_EVERY_POW_2_TO_LOGGER(
        Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), warn,
        "dynamic module set the redirect code {} that is not a 3xx code, so the call is ignored",
        status_code);
    return false;
  }
  auto* context = routeProviderContext(context_envoy_ptr);
  context->selection.terminal = TerminalResponse{true, static_cast<Http::Code>(status_code),
                                                 std::string(location.ptr, location.length)};
  return true;
}

} // extern "C"

} // namespace DynamicModules
} // namespace RouteProviders
} // namespace Router
} // namespace Extensions
} // namespace Envoy
