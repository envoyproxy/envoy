#include "source/extensions/filters/http/query_parameter_mutation/query_params_evaluator.h"

#include <memory>
#include <string>
#include <utility>

#include "envoy/http/query_params.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/formatter/substitution_formatter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace QueryParameterMutation {

QueryParamsEvaluator::QueryParamsEvaluator(
    const Protobuf::RepeatedPtrField<
        envoy::extensions::filters::http::query_parameter_mutation::v3::QueryParameterValueOption>&
        query_params_to_add,
    const Protobuf::RepeatedPtrField<std::string>& query_params_to_remove) {

  formatter_ = std::make_unique<Formatter::FormatterImpl>("", true);
  for (const auto& query_param : query_params_to_add) {
    query_params_to_add_.emplace_back(std::make_tuple(query_param.query_parameter().key(),
                                                      query_param.query_parameter().value(),
                                                      query_param.append_action()));
  }

  for (const auto& val : query_params_to_remove) {
    query_params_to_remove_.emplace_back(val);
  }
}

void QueryParamsEvaluator::evaluateQueryParams(Http::RequestHeaderMap& headers,
                                               StreamInfo::StreamInfo& stream_info) const {
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
  Formatter::HttpFormatterContext ctx{&headers};
  for (const auto& [key, val, append_action] : query_params_to_add_) {
    const auto formatter = std::make_unique<Formatter::FormatterImpl>(val, true);
    switch (append_action) {
    case envoy::extensions::filters::http::query_parameter_mutation::v3::
        QueryParameterValueOption_QueryParameterAppendAction_APPEND_IF_EXISTS_OR_ADD:
      query_params.add(key, formatter->formatWithContext(ctx, stream_info));
      break;
    case envoy::extensions::filters::http::query_parameter_mutation::v3::
        QueryParameterValueOption_QueryParameterAppendAction_ADD_IF_ABSENT:
      if (!query_params.getFirstValue(key).has_value()) {
        query_params.add(key, formatter->formatWithContext(ctx, stream_info));
      }
      break;
    case envoy::extensions::filters::http::query_parameter_mutation::v3::
        QueryParameterValueOption_QueryParameterAppendAction_OVERWRITE_IF_EXISTS_OR_ADD:
      query_params.overwrite(key, val);
      break;
    case envoy::extensions::filters::http::query_parameter_mutation::v3::
        QueryParameterValueOption_QueryParameterAppendAction_OVERWRITE_IF_EXISTS:
      if (query_params.getFirstValue(key).has_value()) {
        query_params.overwrite(key, val);
      }
      break;
    default:
      // should be unreachable.
      break;
    }
  }

  const auto new_path = query_params.replaceQueryString(headers.Path()->value());
  headers.setPath(new_path);
}

} // namespace QueryParameterMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
