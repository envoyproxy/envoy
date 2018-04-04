#pragma once

#include <string>

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

/**
 * Config registration for http dynamodb filter.
 */
class DynamoFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  Server::Configuration::HttpFilterFactoryCb
  createFilter(const std::string& stat_prefix,
               Server::Configuration::FactoryContext& context) override;

  std::string name() override { return Config::HttpFilterNames::get().DYNAMO; }
};

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
