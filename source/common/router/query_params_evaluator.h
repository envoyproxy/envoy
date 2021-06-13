#pragma once

#include <map>
#include <string>
#include <vector>

#include "envoy/http/header_map.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Router {

class QueryParamsEvaluator;
using QueryParamsEvaluatorPtr = std::unique_ptr<QueryParamsEvaluator>;

class QueryParamsEvaluator {
public:
  static QueryParamsEvaluatorPtr configure(const Protobuf::Map<std::string, std::string>& query_params_to_add, const Protobuf::RepeatedPtrField<std::string>& query_params_to_remove);

  /**
   * Returns the same static instance for uses where query params are not configured to save memory.
   **/
  static const QueryParamsEvaluator& defaultEvaluator();

  void evaluateQueryParams(Http::RequestHeaderMap& headers) const;

protected:
  QueryParamsEvaluator() = default;

private:
  std::map<std::string, std::string> query_params_to_add_;
  std::vector<std::string> query_params_to_remove_;
};

} // namespace Router
} // namespace Envoy
