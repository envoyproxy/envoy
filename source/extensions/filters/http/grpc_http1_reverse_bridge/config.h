#pragma once

#include "envoy/extensions/filters/http/grpc_http1_reverse_bridge/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_http1_reverse_bridge/v3/config.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1ReverseBridge {

class Config
    : public Common::FactoryBase<
          envoy::extensions::filters::http::grpc_http1_reverse_bridge::v3::FilterConfig,
          envoy::extensions::filters::http::grpc_http1_reverse_bridge::v3::FilterConfigPerRoute> {
public:
  Config() : FactoryBase("envoy.filters.http.grpc_http1_reverse_bridge") {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::grpc_http1_reverse_bridge::v3::FilterConfig& config,
      const std::string& stat_prefix,
      Envoy::Server::Configuration::FactoryContext& context) override;

private:
  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::grpc_http1_reverse_bridge::v3::FilterConfigPerRoute&
          proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};
} // namespace GrpcHttp1ReverseBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
