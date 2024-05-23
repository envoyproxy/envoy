#include "source/extensions/filters/http/query_parameter_mutation/filter.h"

#include "envoy/extensions/filters/http/query_parameter_mutation/v3/config.pb.h"

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"

#include "source/common/http/utility.h"
#include "source/extensions/filters/http/query_parameter_mutation/query_params_evaluator.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace QueryParameterMutation {

Config::Config(const FilterConfigProto& config)
    : query_params_evaluator_(std::make_unique<QueryParamsEvaluator>(
          config.query_parameters_to_add(), config.query_parameters_to_remove())) {}

void Config::evaluateQueryParams(Http::RequestHeaderMap& headers) const {
  query_params_evaluator_->evaluateQueryParams(headers);
}

Filter::Filter(ConfigSharedPtr config) : config_(config) {}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  route_configs_ = Http::Utility::getAllPerFilterConfig<Config>(decoder_callbacks_);

  config_->evaluateQueryParams(headers);

  const auto& virtual_host = decoder_callbacks_->route()->routeEntry()->virtualHost();
  if (virtual_host.routeConfig().mostSpecificHeaderMutationsWins()) {
    for (auto config : route_configs_) {
      config->evaluateQueryParams(headers);
    }
  } else {
    for (auto it = route_configs_.rbegin(); it != route_configs_.rend(); ++it) {
      (*it)->evaluateQueryParams(headers);
    }
  }

  return Http::FilterHeadersStatus::Continue;
}

} // namespace QueryParameterMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
