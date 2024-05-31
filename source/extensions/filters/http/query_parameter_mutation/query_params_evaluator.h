#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/query_parameter_mutation/v3/config.pb.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/http/header_map.h"
#include "envoy/http/query_params.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace QueryParameterMutation {

class QueryParamsEvaluator;
using QueryParamsEvaluatorPtr = std::unique_ptr<QueryParamsEvaluator>;

class QueryParamsEvaluator {
public:
  QueryParamsEvaluator(
      const Protobuf::RepeatedPtrField<envoy::extensions::filters::http::query_parameter_mutation::
                                           v3::QueryParameterValueOption>& query_params_to_add,
      const Protobuf::RepeatedPtrField<std::string>& query_params_to_remove);

  /**
   * Processes headers first through query parameter removals then through query parameter
   * additions. Header is modified in-place.
   * @param headers supplies the request headers.
   * @param stream_info used by the substitution formatter. Can be retrieved via
   * decoder_callbacks_.streamInfo();
   */
  void evaluateQueryParams(Http::RequestHeaderMap& headers,
                           StreamInfo::StreamInfo& stream_info) const;

protected:
  QueryParamsEvaluator() = default;

private:
  std::vector<std::tuple<std::string, std::string,
                         envoy::extensions::filters::http::query_parameter_mutation::v3::
                             QueryParameterValueOption_QueryParameterAppendAction>>
      query_params_to_add_;
  std::vector<std::string> query_params_to_remove_;
  Formatter::FormatterPtr formatter_;
};

} // namespace QueryParameterMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
