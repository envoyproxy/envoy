#pragma once

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcWeb {

class GrpcWebFilterConfig : public Common::EmptyHttpFilterConfig {
public:
  Server::Configuration::HttpFilterFactoryCb
  createFilter(const std::string&, Server::Configuration::FactoryContext& context) override;

  std::string name() override { return Config::HttpFilterNames::get().GRPC_WEB; }
};

} // namespace GrpcWeb
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
