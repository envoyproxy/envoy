#pragma once

#include "envoy/extensions/filters/http/grpc_web/v3/grpc_web.pb.h"
#include "envoy/extensions/filters/http/grpc_web/v3/grpc_web.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcWeb {

class GrpcWebFilterConfig
    : public Common::UnifiedFactoryBase<envoy::extensions::filters::http::grpc_web::v3::GrpcWeb> {
public:
  GrpcWebFilterConfig() : UnifiedFactoryBase("envoy.filters.http.grpc_web") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::grpc_web::v3::GrpcWeb& proto_config,
      Server::Configuration::ServerFactoryContext& factory_context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

} // namespace GrpcWeb
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
