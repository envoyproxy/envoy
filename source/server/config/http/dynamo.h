#pragma once

#include "server/config/http/empty_http_filter_config.h"

#include <string>

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for http dynamodb filter.
 */
class DynamoFilterConfig : public EmptyHttpFilterConfig {
public:
  HttpFilterFactoryCb createFilter(const std::string& stat_prefix,
                                   FactoryContext& context) override;

  std::string name() override { return Config::HttpFilterNames::get().DYNAMO; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
