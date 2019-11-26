#pragma once

#include <string>

#include "envoy/config/filter/http/dynamo/v2/dynamo.pb.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

/**
 * Config registration for http dynamodb filter.
 */
class DynamoFilterConfig : public Common::FactoryBase<envoy::config::filter::http::dynamo::v2::Dynamo> {
public:
  DynamoFilterConfig() : FactoryBase(HttpFilterNames::get().Dynamo) {}

private:
  Http::FilterFactoryCb  createFilterFactoryFromProtoTyped(const envoy::config::filter::http::dynamo::v2::Dynamo& proto_config, const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
