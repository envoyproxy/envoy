#include "extensions/filters/http/gzip/config.h"

#include "extensions/filters/http/gzip/gzip_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Gzip {

Http::FilterFactoryCb GzipFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::gzip::v3::Gzip& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  // The current deprecation phase is fail-by-default.
  const bool runtime_feature_default = false;
  const char runtime_key[] = "envoy.deprecated_features.allow_deprecated_gzip_http_filter";
  const std::string warn_message =
      "Using deprecated extension 'envoy.extensions.filters.http.gzip'. This "
      "extension will be removed from Envoy soon. Please use "
      "'envoy.extensions.filters.http.compressor' instead.";

  if (context.runtime().snapshot().deprecatedFeatureEnabled(runtime_key, runtime_feature_default)) {
    ENVOY_LOG_MISC(warn, "{}", warn_message);
  } else {
    throw EnvoyException(
        warn_message +
        " If continued use of this extension is absolutely necessary, see "
        "https://www.envoyproxy.io/docs/envoy/latest/configuration/operations/runtime"
        "#using-runtime-overrides-for-deprecated-features for how to apply a temporary and "
        "highly discouraged override.");
  }

  Common::Compressors::CompressorFilterConfigSharedPtr config = std::make_shared<GzipFilterConfig>(
      proto_config, stats_prefix, context.scope(), context.runtime());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Common::Compressors::CompressorFilter>(config));
  };
}

/**
 * Static registration for the gzip filter. @see NamedHttpFilterConfigFactory.
 */
REGISTER_FACTORY(GzipFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.gzip"};

} // namespace Gzip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
