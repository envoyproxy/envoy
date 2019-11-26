#pragma once

#include "envoy/config/filter/http/grpc_web/v2/grpc_web.pb.h"
#include "envoy/config/filter/http/grpc_web/v2/grpc_web.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcWeb {

class GrpcWebFilterConfig : public Common::FactoryBase<envoy::config::filter::http::grpc_web::v2::GrpcWeb> {
public:
  GrpcWebFilterConfig() : FactoryBase(HttpFilterNames::get().GrpcWeb) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::grpc_web::v2::GrpcWeb& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& factory_context) override;
};

} // namespace GrpcWeb
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
