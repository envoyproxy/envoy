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
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::QueryParameter>& query_params_to_add,
    const Protobuf::RepeatedPtrField<std::string>& query_params_to_remove) {

  formatter_ = std::make_unique<Formatter::FormatterImpl>("", true);
  for (const auto& query_param : query_params_to_add) {
    query_params_to_add_.emplace_back(std::make_pair(query_param.key(), query_param.value()));
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
  for (const auto& [key, val] : query_params_to_add_) {
    const auto formatter = std::make_unique<Formatter::FormatterImpl>(val, true);
    query_params.add(key, formatter->formatWithContext(ctx, stream_info));
  }

  const auto new_path = query_params.replaceQueryString(headers.Path()->value());
  headers.setPath(new_path);
}

} // namespace QueryParameterMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
