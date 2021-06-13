#include "source/common/router/query_params_evaluator.h"

#include <string>

#include "envoy/http/query_params.h"

namespace Envoy {
namespace Router {

QueryParamsEvaluatorPtr QueryParamsEvaluator::configure(const Protobuf::Map<std::string, std::string>& query_params_to_add, const Protobuf::RepeatedPtrField<std::string>& query_params_to_remove) {
  QueryParamsEvaluatorPtr query_params_evaluator(new QueryParamsEvaluator());

  for (const auto& [key, val] : query_params_to_add) {
    query_params_evaluator->query_params_to_add_[key] = val;
  }

  for (const auto& val : query_params_to_remove) {
    query_params_evaluator->query_params_to_remove_.emplace_back(val);
  }

  return query_params_evaluator;
}

const QueryParamsEvaluator& QueryParamsEvaluator::defaultEvaluator() {
    std::cout << "default evaluator called" << std::endl;
    static QueryParamsEvaluator* instance = new QueryParamsEvaluator();
    return *instance;
  }

void QueryParamsEvaluator::evaluateQueryParams(Http::RequestHeaderMap& headers) const {
  absl::string_view path = headers.getPathValue();
  Http::Utility::QueryParamsMulti query_params = Http::Utility::QueryParamsMulti::parseAndDecodeQueryString(path);

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
