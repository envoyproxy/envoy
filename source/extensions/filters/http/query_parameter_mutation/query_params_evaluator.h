#pragma once

#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/http/query_params.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace QueryParameterMutation {

class QueryParamsEvaluator;
using QueryParamsEvaluatorPtr = std::unique_ptr<QueryParamsEvaluator>;

class QueryParamsEvaluator {
public:
  QueryParamsEvaluator(const Protobuf::RepeatedPtrField<envoy::config::core::v3::QueryParameter>&
                           query_params_to_add,
                       const Protobuf::RepeatedPtrField<std::string>& query_params_to_remove);

  /**
   * Processes headers first through query parameter removals then through query parameter
   * additions. Header is modified in-place.
   * @param headers supplies the request headers.
   */
  void evaluateQueryParams(Http::RequestHeaderMap& headers) const;

protected:
  QueryParamsEvaluator() = default;

private:
  Http::Utility::QueryParamsVector query_params_to_add_;
  std::vector<std::string> query_params_to_remove_;
};

} // namespace QueryParameterMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
