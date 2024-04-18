#pragma once

#include <string>

#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/factory_base.h"

#include "contrib/envoy/extensions/filters/http/dynamo/v3/dynamo.pb.h"
#include "contrib/envoy/extensions/filters/http/dynamo/v3/dynamo.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

/**
 * Config registration for http dynamodb filter.
 */
class DynamoFilterConfig
    : public Common::FactoryBase<envoy::extensions::filters::http::dynamo::v3::Dynamo> {
public:
  DynamoFilterConfig() : FactoryBase("envoy.filters.http.dynamo") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::dynamo::v3::Dynamo& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
