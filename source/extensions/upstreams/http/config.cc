#include "source/extensions/upstreams/http/config.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
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
    ProtobufMessage::ValidationVisitor& validation_visitor)
    : http1_settings_(
          Envoy::Http::Http1::parseHttp1Settings(getHttpOptions(options), validation_visitor)),
      http2_options_(Http2::Utility::initializeAndValidateOptions(getHttp2Options(options))),
      http3_options_(getHttp3Options(options)),
      common_http_protocol_options_(options.common_http_protocol_options()),
      upstream_http_protocol_options_(
          options.has_upstream_http_protocol_options()
              ? absl::make_optional<envoy::config::core::v3::UpstreamHttpProtocolOptions>(
                    options.upstream_http_protocol_options())
              : absl::nullopt) {
  if (options.has_explicit_http_config()) {
    if (options.explicit_http_config().has_http3_protocol_options()) {
      use_http3_ = true;
    } else if (options.explicit_http_config().has_http2_protocol_options()) {
      use_http2_ = true;
    }
  }
  if (options.has_use_downstream_protocol_config()) {
    if (options.use_downstream_protocol_config().has_http3_protocol_options()) {
      use_http3_ = true;
    }
    if (options.use_downstream_protocol_config().has_http2_protocol_options()) {
      use_http2_ = true;
    }
    use_downstream_protocol_ = true;
  }
  http_filters_ = options.http_filters();
  if (options.has_auto_config()) {
    use_http2_ = true;
    use_alpn_ = true;
    use_http3_ = options.auto_config().has_http3_protocol_options();
    if (use_http3_) {
      if (!options.auto_config().has_alternate_protocols_cache_options()) {
        throw EnvoyException(fmt::format("alternate protocols cache must be configured when HTTP/3 "
                                         "is enabled with auto_config"));
      }
      alternate_protocol_cache_options_ = options.auto_config().alternate_protocols_cache_options();
    }
  }
}

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

REGISTER_FACTORY(ProtocolOptionsConfigFactory, Server::Configuration::ProtocolOptionsFactory){
    "envoy.upstreams.http.http_protocol_options"};
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
