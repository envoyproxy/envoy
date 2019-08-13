#pragma once

#include "envoy/config/filter/http/grpc_http1_reverse_bridge/v2alpha1/config.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1ReverseBridge {

class Config : public Common::FactoryBase<
                   envoy::config::filter::http::grpc_http1_reverse_bridge::v2alpha1::FilterConfig> {
public:
  Config() : FactoryBase(HttpFilterNames::get().GrpcHttp1ReverseBridge) {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::grpc_http1_reverse_bridge::v2alpha1::FilterConfig& config,
      const std::string& stat_prefix,
      Envoy::Server::Configuration::FactoryContext& context) override;
};
} // namespace GrpcHttp1ReverseBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
