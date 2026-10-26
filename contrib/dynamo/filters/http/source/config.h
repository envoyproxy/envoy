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
    : public Common::UnifiedFactoryBase<envoy::extensions::filters::http::dynamo::v3::Dynamo> {
public:
  DynamoFilterConfig() : UnifiedFactoryBase("envoy.filters.http.dynamo") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::dynamo::v3::Dynamo& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
