#pragma once

#include "envoy/config/filter/http/grpc_http1_reverse_bridge/v2alpha1/config.pb.h"
#include "envoy/config/filter/http/grpc_http1_reverse_bridge/v2alpha1/config.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1ReverseBridge {

class Config
    : public Common::FactoryBase<
          envoy::config::filter::http::grpc_http1_reverse_bridge::v2alpha1::FilterConfig,
          envoy::config::filter::http::grpc_http1_reverse_bridge::v2alpha1::FilterConfigPerRoute> {
public:
  Config() : FactoryBase(HttpFilterNames::get().GrpcHttp1ReverseBridge) {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::grpc_http1_reverse_bridge::v2alpha1::FilterConfig& config,
      const std::string& stat_prefix,
      Envoy::Server::Configuration::FactoryContext& context) override;

private:
  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::config::filter::http::grpc_http1_reverse_bridge::v2alpha1::FilterConfigPerRoute&
          proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};
} // namespace GrpcHttp1ReverseBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
