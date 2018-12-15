#pragma once

#include "envoy/config/filter/http/grpc_shim/v2alpha1/grpc_shim.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcShim {

class GrpcShimConfig
    : public Common::FactoryBase<envoy::extensions::filter::http::grpc_shim::v2alpha1::GrpcShim> {
public:
  GrpcShimConfig() : FactoryBase(HttpFilterNames::get().GrpcShim) {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filter::http::grpc_shim::v2alpha1::GrpcShim& config,
      const std::string& stat_prefix,
      Envoy::Server::Configuration::FactoryContext& context) override;
};
} // namespace GrpcShim
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
