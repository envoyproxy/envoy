#pragma once

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

using OnHttpConfigDestoryType = decltype(&envoy_dynamic_module_on_http_filter_config_destroy);
using OnHttpFilterNewType = decltype(&envoy_dynamic_module_on_http_filter_new);
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
using OnHttpFilterDestroyType = decltype(&envoy_dynamic_module_on_http_filter_destroy);

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
   */
  DynamicModuleHttpFilterConfig(const absl::string_view filter_name,
                                const absl::string_view filter_config,
                                DynamicModulePtr dynamic_module);

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
  OnHttpFilterDestroyType on_http_filter_destroy_ = nullptr;

private:
  // The name of the filter passed in the constructor.
  const std::string filter_name_;

  // The configuration for the module.
  const std::string filter_config_;

  // The handle for the module.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleHttpFilterConfigSharedPtr = std::shared_ptr<DynamicModuleHttpFilterConfig>;

/**
 * Creates a new DynamicModuleHttpFilterConfig for given configuration.
 * @param filter_name the name of the filter.
 * @param filter_config the configuration for the module.
 * @param dynamic_module the dynamic module to use.
 * @return a shared pointer to the new config object or an error if the module could not be loaded.
 */
absl::StatusOr<DynamicModuleHttpFilterConfigSharedPtr>
newDynamicModuleHttpFilterConfig(const absl::string_view filter_name,
                                 const absl::string_view filter_config,
                                 Extensions::DynamicModules::DynamicModulePtr dynamic_module);

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
