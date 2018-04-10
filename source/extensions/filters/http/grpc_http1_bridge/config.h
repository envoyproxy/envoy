#pragma once

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1Bridge {

/**
 * Config registration for the grpc HTTP1 bridge filter. @see NamedHttpFilterConfigFactory.
 */
class GrpcHttp1BridgeFilterConfig : public Common::EmptyHttpFilterConfig {
public:
  Server::Configuration::HttpFilterFactoryCb
  createFilter(const std::string&, Server::Configuration::FactoryContext& context) override;

  std::string name() override { return Config::HttpFilterNames::get().GRPC_HTTP1_BRIDGE; }
};

} // namespace GrpcHttp1Bridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
