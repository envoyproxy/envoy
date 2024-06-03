#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/header_mutation/v3/header_mutation.pb.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/http/header_map.h"
#include "envoy/http/query_params.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderMutation {

using QueryParameterValueOptionProto =
    envoy::extensions::filters::http::header_mutation::v3::QueryParameterValueOption;
using QueryParameterAppendActionProto = envoy::extensions::filters::http::header_mutation::
    v3::QueryParameterValueOption_QueryParameterAppendAction;

enum class AppendAction {
  AppendIfExistsOrAdd = envoy::extensions::filters::http::header_mutation::v3::
      QueryParameterValueOption_QueryParameterAppendAction_APPEND_IF_EXISTS_OR_ADD,
  AddIfAbsent = envoy::extensions::filters::http::header_mutation::v3::
      QueryParameterValueOption_QueryParameterAppendAction_ADD_IF_ABSENT,
  OverwriteIfExistsOrAdd = envoy::extensions::filters::http::header_mutation::v3::
      QueryParameterValueOption_QueryParameterAppendAction_OVERWRITE_IF_EXISTS_OR_ADD,
  OverwriteIfExists = envoy::extensions::filters::http::header_mutation::v3::
      QueryParameterValueOption_QueryParameterAppendAction_OVERWRITE_IF_EXISTS,
};

class QueryParamsEvaluator;
using QueryParamsEvaluatorPtr = std::unique_ptr<QueryParamsEvaluator>;

class QueryParamsEvaluator {
public:
  QueryParamsEvaluator(
      const Protobuf::RepeatedPtrField<QueryParameterValueOptionProto>& query_params_to_add,
      const Protobuf::RepeatedPtrField<std::string>& query_params_to_remove);

  /**
   * Processes headers first through query parameter removals then through query parameter
   * additions. Header is modified in-place.
   * @param headers supplies the request headers.
   * @param stream_info used by the substitution formatter. Can be retrieved via
   * decoder_callbacks_.streamInfo();
   */
  void evaluateQueryParams(Http::RequestHeaderMap& headers,
                           const StreamInfo::StreamInfo& stream_info) const;

protected:
  QueryParamsEvaluator() = default;

private:
  std::vector<std::tuple<std::string, std::string, QueryParameterAppendActionProto>>
      query_params_to_add_;
  std::vector<std::string> query_params_to_remove_;
  Formatter::FormatterPtr formatter_;
};

} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
