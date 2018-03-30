#pragma once

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class GrpcWebFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  HttpFilterFactoryCb createFilter(const std::string&, FactoryContext& context) override;

  std::string name() override { return Config::HttpFilterNames::get().GRPC_WEB; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
