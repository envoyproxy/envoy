#pragma once

#include "envoy/extensions/filters/http/grpc_http1_bridge/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_http1_bridge/v3/config.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1Bridge {

/**
 * Config registration for the grpc HTTP1 bridge filter. @see NamedHttpFilterConfigFactory.
 */
class GrpcHttp1BridgeFilterConfig
    : public Common::FactoryBase<envoy::extensions::filters::http::grpc_http1_bridge::v3::Config> {
public:
  GrpcHttp1BridgeFilterConfig() : FactoryBase(HttpFilterNames::get().GrpcHttp1Bridge) {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::grpc_http1_bridge::v3::Config& proto_config,
      const std::string& stats_prefix,
      Server::Configuration::FactoryContext& factory_context) override;
};

} // namespace GrpcHttp1Bridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
