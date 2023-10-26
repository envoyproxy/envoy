#include "source/extensions/upstreams/http/config.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.h"
#include "envoy/http/header_validator_factory.h"
#include "envoy/upstream/upstream.h"

#include "source/common/config/utility.h"
#include "source/common/http/http1/settings.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace {

const envoy::config::core::v3::Http1ProtocolOptions&
getHttpOptions(const envoy::extensions::upstreams::http::v3::HttpProtocolOptions& options) {
  if (options.has_use_downstream_protocol_config()) {
    return options.use_downstream_protocol_config().http_protocol_options();
  }
  if (options.has_auto_config()) {
    return options.auto_config().http_protocol_options();
  }
  return options.explicit_http_config().http_protocol_options();
}

const envoy::config::core::v3::Http2ProtocolOptions&
getHttp2Options(const envoy::extensions::upstreams::http::v3::HttpProtocolOptions& options) {
  if (options.has_use_downstream_protocol_config()) {
    return options.use_downstream_protocol_config().http2_protocol_options();
  }
  if (options.has_auto_config()) {
    return options.auto_config().http2_protocol_options();
  }
  return options.explicit_http_config().http2_protocol_options();
}

const envoy::config::core::v3::Http3ProtocolOptions&
getHttp3Options(const envoy::extensions::upstreams::http::v3::HttpProtocolOptions& options) {
  if (options.has_use_downstream_protocol_config() &&
      options.use_downstream_protocol_config().has_http3_protocol_options()) {
    return options.use_downstream_protocol_config().http3_protocol_options();
  }
  if (options.has_auto_config()) {
    return options.auto_config().http3_protocol_options();
  }
  return options.explicit_http_config().http3_protocol_options();
}

bool useHttp2(const envoy::extensions::upstreams::http::v3::HttpProtocolOptions& options) {
  if (options.has_explicit_http_config() &&
      options.explicit_http_config().has_http2_protocol_options()) {
    return true;
  } else if (options.has_use_downstream_protocol_config() &&
             options.use_downstream_protocol_config().has_http2_protocol_options()) {
    return true;
  } else if (options.has_auto_config()) {
    return true;
  }
  return false;
}

bool useHttp3(const envoy::extensions::upstreams::http::v3::HttpProtocolOptions& options) {
  if (options.has_explicit_http_config() &&
      options.explicit_http_config().has_http3_protocol_options()) {
    return true;
  } else if (options.has_use_downstream_protocol_config() &&
             options.use_downstream_protocol_config().has_http3_protocol_options()) {
    return true;
  } else if (options.has_auto_config() && options.auto_config().has_http3_protocol_options()) {
    return true;
  }
  return false;
}

absl::optional<const envoy::config::core::v3::AlternateProtocolsCacheOptions>
getAlternateProtocolsCacheOptions(
    const envoy::extensions::upstreams::http::v3::HttpProtocolOptions& options,
    Server::Configuration::ServerFactoryContext& server_context) {
  if (options.has_auto_config() && options.auto_config().has_http3_protocol_options()) {
    if (!options.auto_config().has_alternate_protocols_cache_options()) {
      throwEnvoyExceptionOrPanic(
          fmt::format("alternate protocols cache must be configured when HTTP/3 "
                      "is enabled with auto_config"));
    }
    auto cache_options = options.auto_config().alternate_protocols_cache_options();
    if (cache_options.has_key_value_store_config() && server_context.options().concurrency() != 1) {
      throwEnvoyExceptionOrPanic(
          fmt::format("options has key value store but Envoy has concurrency = {} : {}",
                      server_context.options().concurrency(), cache_options.DebugString()));
    }

    return cache_options;
  }
  return absl::nullopt;
}

Envoy::Http::HeaderValidatorFactoryPtr createHeaderValidatorFactory(
    [[maybe_unused]] const envoy::extensions::upstreams::http::v3::HttpProtocolOptions& options,
    [[maybe_unused]] Server::Configuration::ServerFactoryContext& server_context) {

  Envoy::Http::HeaderValidatorFactoryPtr header_validator_factory;
#ifdef ENVOY_ENABLE_UHV
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.enable_universal_header_validator")) {
    // This will cause codecs to use legacy header validation and path normalization
    return nullptr;
  }
  ::envoy::config::core::v3::TypedExtensionConfig legacy_header_validator_config;
  if (!options.has_header_validation_config()) {
    // If header validator is not configured ensure that the defaults match Envoy's original
    // behavior.
    ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
        uhv_config;
    uhv_config.mutable_http1_protocol_options()->set_allow_chunked_length(
        getHttpOptions(options).allow_chunked_length());
    legacy_header_validator_config.set_name("default_envoy_uhv_from_legacy_settings");
    legacy_header_validator_config.mutable_typed_config()->PackFrom(uhv_config);
  }

  const ::envoy::config::core::v3::TypedExtensionConfig& header_validator_config =
      options.has_header_validation_config() ? options.header_validation_config()
                                             : legacy_header_validator_config;

  auto* factory = Envoy::Config::Utility::getFactory<Envoy::Http::HeaderValidatorFactoryConfig>(
      header_validator_config);
  if (!factory) {
    throwEnvoyExceptionOrPanic(
        fmt::format("Header validator extension not found: '{}'", header_validator_config.name()));
  }

  header_validator_factory =
      factory->createFromProto(header_validator_config.typed_config(), server_context);
  if (!header_validator_factory) {
    throwEnvoyExceptionOrPanic(fmt::format("Header validator extension could not be created: '{}'",
                                           header_validator_config.name()));
  }
#else
  if (options.has_header_validation_config()) {
    throwEnvoyExceptionOrPanic(
        fmt::format("This Envoy binary does not support header validator extensions: '{}'",
                    options.header_validation_config().name()));
  }

  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.enable_universal_header_validator")) {
    throwEnvoyExceptionOrPanic(
        "Header validator can not be enabled since this Envoy binary does not support it.");
  }
#endif
  return header_validator_factory;
}

} // namespace

uint64_t ProtocolOptionsConfigImpl::parseFeatures(const envoy::config::cluster::v3::Cluster& config,
                                                  const ProtocolOptionsConfigImpl& options) {
  uint64_t features = 0;

  if (options.use_http2_) {
    features |= Upstream::ClusterInfo::Features::HTTP2;
  }
  if (options.use_http3_) {
    features |= Upstream::ClusterInfo::Features::HTTP3;
  }
  if (options.use_downstream_protocol_) {
    features |= Upstream::ClusterInfo::Features::USE_DOWNSTREAM_PROTOCOL;
  }
  if (options.use_alpn_) {
    features |= Upstream::ClusterInfo::Features::USE_ALPN;
  }
  if (config.close_connections_on_host_health_failure()) {
    features |= Upstream::ClusterInfo::Features::CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE;
  }
  return features;
}

ProtocolOptionsConfigImpl::ProtocolOptionsConfigImpl(
    const envoy::extensions::upstreams::http::v3::HttpProtocolOptions& options,
    Server::Configuration::ServerFactoryContext& server_context)
    : http1_settings_(Envoy::Http::Http1::parseHttp1Settings(
          getHttpOptions(options), server_context.messageValidationVisitor())),
      http2_options_(Http2::Utility::initializeAndValidateOptions(getHttp2Options(options))),
      http3_options_(getHttp3Options(options)),
      common_http_protocol_options_(options.common_http_protocol_options()),
      upstream_http_protocol_options_(
          options.has_upstream_http_protocol_options()
              ? absl::make_optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>(
                    options.upstream_http_protocol_options())
              : absl::nullopt),
      http_filters_(options.http_filters()),
      alternate_protocol_cache_options_(getAlternateProtocolsCacheOptions(options, server_context)),
      header_validator_factory_(createHeaderValidatorFactory(options, server_context)),
      use_downstream_protocol_(options.has_use_downstream_protocol_config()),
      use_http2_(useHttp2(options)), use_http3_(useHttp3(options)),
      use_alpn_(options.has_auto_config()) {}

ProtocolOptionsConfigImpl::ProtocolOptionsConfigImpl(
    const envoy::config::core::v3::Http1ProtocolOptions& http1_settings,
    const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
    const envoy::config::core::v3::HttpProtocolOptions& common_options,
    const absl::optional<envoy::config::core::v3::UpstreamHttpProtocolOptions> upstream_options,
    bool use_downstream_protocol, bool use_http2,
    ProtobufMessage::ValidationVisitor& validation_visitor)
    : http1_settings_(Envoy::Http::Http1::parseHttp1Settings(http1_settings, validation_visitor)),
      http2_options_(Http2::Utility::initializeAndValidateOptions(http2_options)),
      common_http_protocol_options_(common_options),
      upstream_http_protocol_options_(upstream_options),
      use_downstream_protocol_(use_downstream_protocol), use_http2_(use_http2) {}

LEGACY_REGISTER_FACTORY(ProtocolOptionsConfigFactory, Server::Configuration::ProtocolOptionsFactory,
                        "envoy.upstreams.http.http_protocol_options");
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
