#pragma once

#include "envoy/server/factory_context.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

using OnHttpConfigDestoryType = decltype(&envoy_dynamic_module_on_http_filter_config_destroy);
using OnHttpFilterNewType = decltype(&envoy_dynamic_module_on_http_filter_new);

using OnHttpPerRouteConfigDestoryType =
    decltype(&envoy_dynamic_module_on_http_filter_per_route_config_destroy);
using OnHttpFilterRequestHeadersType =
    decltype(&envoy_dynamic_module_on_http_filter_request_headers);
using OnHttpFilterRequestBodyType = decltype(&envoy_dynamic_module_on_http_filter_request_body);
using OnHttpFilterRequestTrailersType =
    decltype(&envoy_dynamic_module_on_http_filter_request_trailers);
using OnHttpFilterResponseHeadersType =
    decltype(&envoy_dynamic_module_on_http_filter_response_headers);
using OnHttpFilterResponseBodyType = decltype(&envoy_dynamic_module_on_http_filter_response_body);
using OnHttpFilterResponseTrailersType =
    decltype(&envoy_dynamic_module_on_http_filter_response_trailers);
using OnHttpFilterStreamCompleteType =
    decltype(&envoy_dynamic_module_on_http_filter_stream_complete);
using OnHttpFilterDestroyType = decltype(&envoy_dynamic_module_on_http_filter_destroy);
using OnHttpFilterHttpCalloutDoneType =
    decltype(&envoy_dynamic_module_on_http_filter_http_callout_done);

/**
 * A config to create http filters based on a dynamic module. This will be owned by multiple
 * filter instances. This resolves and holds the symbols used for the HTTP filters.
 * Each filter instance and the factory callback holds a shared pointer to this config.
 */
class DynamicModuleHttpFilterConfig {
public:
  /**
   * Constructor for the config.
   * @param filter_name the name of the filter.
   * @param filter_config the configuration for the module.
   * @param dynamic_module the dynamic module to use.
   * @param context the server factory context.
   */
  DynamicModuleHttpFilterConfig(const absl::string_view filter_name,
                                const absl::string_view filter_config,
                                DynamicModulePtr dynamic_module,
                                Server::Configuration::ServerFactoryContext& context);

  ~DynamicModuleHttpFilterConfig();

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_http_filter_config_module_ptr in_module_config_ = nullptr;

  // The function pointers for the module related to the HTTP filter. All of them are resolved
  // during the construction of the config and made sure they are not nullptr after that.

  OnHttpConfigDestoryType on_http_filter_config_destroy_ = nullptr;
  OnHttpFilterNewType on_http_filter_new_ = nullptr;
  OnHttpFilterRequestHeadersType on_http_filter_request_headers_ = nullptr;
  OnHttpFilterRequestBodyType on_http_filter_request_body_ = nullptr;
  OnHttpFilterRequestTrailersType on_http_filter_request_trailers_ = nullptr;
  OnHttpFilterResponseHeadersType on_http_filter_response_headers_ = nullptr;
  OnHttpFilterResponseBodyType on_http_filter_response_body_ = nullptr;
  OnHttpFilterResponseTrailersType on_http_filter_response_trailers_ = nullptr;
  OnHttpFilterStreamCompleteType on_http_filter_stream_complete_ = nullptr;
  OnHttpFilterDestroyType on_http_filter_destroy_ = nullptr;
  OnHttpFilterHttpCalloutDoneType on_http_filter_http_callout_done_ = nullptr;

  Envoy::Upstream::ClusterManager& cluster_manager_;

private:
  // The name of the filter passed in the constructor.
  const std::string filter_name_;

  // The configuration for the module.
  const std::string filter_config_;

  // The handle for the module.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

class DynamicModuleHttpPerRouteFilterConfig : public Router::RouteSpecificFilterConfig {
public:
  DynamicModuleHttpPerRouteFilterConfig(
      envoy_dynamic_module_type_http_filter_config_module_ptr config,
      OnHttpPerRouteConfigDestoryType destroy)
      : config_(config), destroy_(destroy) {}
  ~DynamicModuleHttpPerRouteFilterConfig() override;

  envoy_dynamic_module_type_http_filter_config_module_ptr config_;

private:
  OnHttpPerRouteConfigDestoryType destroy_;
};

using DynamicModuleHttpFilterConfigSharedPtr = std::shared_ptr<DynamicModuleHttpFilterConfig>;
using DynamicModuleHttpPerRouteFilterConfigConstSharedPtr =
    std::shared_ptr<const DynamicModuleHttpPerRouteFilterConfig>;

absl::StatusOr<DynamicModuleHttpPerRouteFilterConfigConstSharedPtr>
newDynamicModuleHttpPerRouteConfig(const absl::string_view per_route_config_name,
                                   const absl::string_view filter_config,
                                   Extensions::DynamicModules::DynamicModulePtr dynamic_module);

/**
 * Creates a new DynamicModuleHttpFilterConfig for given configuration.
 * @param filter_name the name of the filter.
 * @param filter_config the configuration for the module.
 * @param dynamic_module the dynamic module to use.
 * @param context the server factory context.
 * @return a shared pointer to the new config object or an error if the module could not be loaded.
 */
absl::StatusOr<DynamicModuleHttpFilterConfigSharedPtr>
newDynamicModuleHttpFilterConfig(const absl::string_view filter_name,
                                 const absl::string_view filter_config,
                                 Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                 Server::Configuration::ServerFactoryContext& context);

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
