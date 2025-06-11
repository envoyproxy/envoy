#include "source/extensions/filters/http/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

DynamicModuleHttpFilterConfig::DynamicModuleHttpFilterConfig(
    const absl::string_view filter_name, const absl::string_view filter_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Server::Configuration::ServerFactoryContext& context)
    : cluster_manager_(context.clusterManager()), filter_name_(filter_name),
      filter_config_(filter_config), dynamic_module_(std::move(dynamic_module)) {};

DynamicModuleHttpFilterConfig::~DynamicModuleHttpFilterConfig() {
  // When the initialization of the dynamic module fails, the in_module_config_ is nullptr,
  // and there's nothing to destroy from the module's point of view.
  if (on_http_filter_config_destroy_) {
    (*on_http_filter_config_destroy_)(in_module_config_);
  }
}

DynamicModuleHttpPerRouteFilterConfig::~DynamicModuleHttpPerRouteFilterConfig() {
  (*destroy_)(config_);
}

absl::StatusOr<DynamicModuleHttpPerRouteFilterConfigConstSharedPtr>
newDynamicModuleHttpPerRouteConfig(const absl::string_view per_route_config_name,
                                   const absl::string_view filter_config,
                                   Extensions::DynamicModules::DynamicModulePtr dynamic_module) {
  auto constructor =
      dynamic_module
          ->getFunctionPointer<decltype(&envoy_dynamic_module_on_http_filter_per_route_config_new)>(
              "envoy_dynamic_module_on_http_filter_per_route_config_new");
  RETURN_IF_NOT_OK_REF(constructor.status());

  auto destroy = dynamic_module->getFunctionPointer<OnHttpPerRouteConfigDestoryType>(
      "envoy_dynamic_module_on_http_filter_per_route_config_destroy");
  RETURN_IF_NOT_OK_REF(destroy.status());

  const void* filter_config_envoy_ptr =
      (*constructor.value())(per_route_config_name.data(), per_route_config_name.size(),
                             filter_config.data(), filter_config.size());
  if (filter_config_envoy_ptr == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize per-route dynamic module");
  }

  return std::make_shared<const DynamicModuleHttpPerRouteFilterConfig>(filter_config_envoy_ptr,
                                                                       destroy.value());
}

absl::StatusOr<DynamicModuleHttpFilterConfigSharedPtr>
newDynamicModuleHttpFilterConfig(const absl::string_view filter_name,
                                 const absl::string_view filter_config,
                                 Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                 Server::Configuration::ServerFactoryContext& context) {
  auto constructor =
      dynamic_module->getFunctionPointer<decltype(&envoy_dynamic_module_on_http_filter_config_new)>(
          "envoy_dynamic_module_on_http_filter_config_new");
  RETURN_IF_NOT_OK_REF(constructor.status());

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnHttpConfigDestoryType>(
      "envoy_dynamic_module_on_http_filter_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());

  auto on_new_filter = dynamic_module->getFunctionPointer<OnHttpFilterNewType>(
      "envoy_dynamic_module_on_http_filter_new");
  RETURN_IF_NOT_OK_REF(on_new_filter.status());

  auto on_request_headers = dynamic_module->getFunctionPointer<OnHttpFilterRequestHeadersType>(
      "envoy_dynamic_module_on_http_filter_request_headers");
  RETURN_IF_NOT_OK_REF(on_request_headers.status());

  auto on_request_body = dynamic_module->getFunctionPointer<OnHttpFilterRequestBodyType>(
      "envoy_dynamic_module_on_http_filter_request_body");
  RETURN_IF_NOT_OK_REF(on_request_body.status());

  auto on_request_trailers = dynamic_module->getFunctionPointer<OnHttpFilterRequestTrailersType>(
      "envoy_dynamic_module_on_http_filter_request_trailers");
  RETURN_IF_NOT_OK_REF(on_request_trailers.status());

  auto on_response_headers = dynamic_module->getFunctionPointer<OnHttpFilterResponseHeadersType>(
      "envoy_dynamic_module_on_http_filter_response_headers");
  RETURN_IF_NOT_OK_REF(on_response_headers.status());

  auto on_response_body = dynamic_module->getFunctionPointer<OnHttpFilterResponseBodyType>(
      "envoy_dynamic_module_on_http_filter_response_body");
  RETURN_IF_NOT_OK_REF(on_response_body.status());

  auto on_response_trailers = dynamic_module->getFunctionPointer<OnHttpFilterResponseTrailersType>(
      "envoy_dynamic_module_on_http_filter_response_trailers");
  RETURN_IF_NOT_OK_REF(on_response_trailers.status());

  auto on_filter_stream_complete =
      dynamic_module->getFunctionPointer<OnHttpFilterStreamCompleteType>(
          "envoy_dynamic_module_on_http_filter_stream_complete");
  RETURN_IF_NOT_OK_REF(on_filter_stream_complete.status());

  auto on_filter_destroy = dynamic_module->getFunctionPointer<OnHttpFilterDestroyType>(
      "envoy_dynamic_module_on_http_filter_destroy");
  RETURN_IF_NOT_OK_REF(on_filter_destroy.status());

  auto on_http_callout_done = dynamic_module->getFunctionPointer<OnHttpFilterHttpCalloutDoneType>(
      "envoy_dynamic_module_on_http_filter_http_callout_done");
  RETURN_IF_NOT_OK_REF(on_http_callout_done.status());

  auto config = std::make_shared<DynamicModuleHttpFilterConfig>(filter_name, filter_config,
                                                                std::move(dynamic_module), context);

  const void* filter_config_envoy_ptr =
      (*constructor.value())(static_cast<void*>(config.get()), filter_name.data(),
                             filter_name.size(), filter_config.data(), filter_config.size());
  if (filter_config_envoy_ptr == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module");
  }

  config->in_module_config_ = filter_config_envoy_ptr;
  config->on_http_filter_config_destroy_ = on_config_destroy.value();
  config->on_http_filter_new_ = on_new_filter.value();
  config->on_http_filter_request_headers_ = on_request_headers.value();
  config->on_http_filter_request_body_ = on_request_body.value();
  config->on_http_filter_request_trailers_ = on_request_trailers.value();
  config->on_http_filter_response_headers_ = on_response_headers.value();
  config->on_http_filter_response_body_ = on_response_body.value();
  config->on_http_filter_response_trailers_ = on_response_trailers.value();
  config->on_http_filter_stream_complete_ = on_filter_stream_complete.value();
  config->on_http_filter_destroy_ = on_filter_destroy.value();
  config->on_http_filter_http_callout_done_ = on_http_callout_done.value();
  return config;
}

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
