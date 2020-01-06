#pragma once

#include "envoy/extensions/filters/http/grpc_http1_reverse_bridge/v3alpha/config.pb.h"
#include "envoy/extensions/filters/http/grpc_http1_reverse_bridge/v3alpha/config.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1ReverseBridge {

class Config
    : public Common::FactoryBase<
          envoy::extensions::filters::http::grpc_http1_reverse_bridge::v3alpha::FilterConfig,
          envoy::extensions::filters::http::grpc_http1_reverse_bridge::v3alpha::
              FilterConfigPerRoute> {
public:
  Config() : FactoryBase(HttpFilterNames::get().GrpcHttp1ReverseBridge) {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::grpc_http1_reverse_bridge::v3alpha::FilterConfig&
          config,
      const std::string& stat_prefix,
      Envoy::Server::Configuration::FactoryContext& context) override;

private:
  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::grpc_http1_reverse_bridge::v3alpha::
          FilterConfigPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};
} // namespace GrpcHttp1ReverseBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
