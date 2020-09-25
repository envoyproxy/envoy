#pragma once

#include <string>

#include "envoy/extensions/filters/http/mutation/v3/mutation.pb.h"
#include "envoy/extensions/filters/http/mutation/v3/mutation.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mutation {

class MutationFilterConfig
    : public Common::FactoryBase<envoy::extensions::filters::http::mutation::v3::Mutation> {

public:
  MutationFilterConfig() : FactoryBase(HttpFilterNames::get().Mutation) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::mutation::v3::Mutation& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Mutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy