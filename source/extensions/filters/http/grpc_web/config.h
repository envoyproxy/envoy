#pragma once

#include "envoy/extensions/filters/http/grpc_web/v3/grpc_web.pb.h"
#include "envoy/extensions/filters/http/grpc_web/v3/grpc_web.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcWeb {

class GrpcWebFilterConfig
    : public Common::FactoryBase<envoy::extensions::filters::http::grpc_web::v3::GrpcWeb> {
public:
  GrpcWebFilterConfig() : FactoryBase("envoy.filters.http.grpc_web") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::grpc_web::v3::GrpcWeb& proto_config,
      const std::string& stats_prefix,
      Server::Configuration::FactoryContext& factory_context) override;
};

} // namespace GrpcWeb
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
