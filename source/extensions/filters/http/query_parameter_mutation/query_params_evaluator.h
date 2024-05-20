#pragma once

#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/http/query_params.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Router {

class QueryParamsEvaluator;
using QueryParamsEvaluatorPtr = std::unique_ptr<QueryParamsEvaluator>;

class QueryParamsEvaluator {
public:
  static QueryParamsEvaluatorPtr
  configure(const Protobuf::RepeatedPtrField<envoy::config::core::v3::QueryParameter>&
                query_params_to_add,
            const Protobuf::RepeatedPtrField<std::string>& query_params_to_remove);

  void evaluateQueryParams(Http::RequestHeaderMap& headers) const;

protected:
  QueryParamsEvaluator() = default;

private:
  Http::Utility::QueryParamsVector query_params_to_add_;
  std::vector<std::string> query_params_to_remove_;
};

} // namespace Router
} // namespace Envoy
