#include "source/common/router/query_params_evaluator.h"

#include <string>
#include <utility>

#include "envoy/http/query_params.h"

namespace Envoy {
namespace Router {

QueryParamsEvaluatorPtr QueryParamsEvaluator::configure(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::QueryParameter>& query_params_to_add,
    const Protobuf::RepeatedPtrField<std::string>& query_params_to_remove) {
  QueryParamsEvaluatorPtr query_params_evaluator(new QueryParamsEvaluator());

  for (const auto& query_param : query_params_to_add) {
    const auto& pair = std::make_pair(query_param.key(), query_param.value());
    query_params_evaluator->query_params_to_add_.emplace_back(pair);
  }

  for (const auto& val : query_params_to_remove) {
    query_params_evaluator->query_params_to_remove_.emplace_back(val);
  }

  return query_params_evaluator;
}

const QueryParamsEvaluator& QueryParamsEvaluator::defaultEvaluator() {
  static QueryParamsEvaluator* instance = new QueryParamsEvaluator();
  return *instance;
}

void QueryParamsEvaluator::evaluateQueryParams(Http::RequestHeaderMap& headers) const {
  if (query_params_to_remove_.empty() && query_params_to_add_.empty()) {
    return;
  }

  absl::string_view path = headers.getPathValue();
  Http::Utility::QueryParamsMulti query_params =
      Http::Utility::QueryParamsMulti::parseAndDecodeQueryString(path);

  // Remove desired query parameters.
  for (const auto& val : query_params_to_remove_) {
    query_params.remove(val);
  }

  // Add new desired query parameters.
  for (const auto& [key, val] : query_params_to_add_) {
    query_params.add(key, val);
  }

  const auto new_path = query_params.replaceQueryString(headers.Path()->value());
  headers.setPath(new_path);
}

} // namespace Router
} // namespace Envoy
