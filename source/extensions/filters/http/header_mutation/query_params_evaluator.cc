#include "source/extensions/filters/http/header_mutation/query_params_evaluator.h"

#include <memory>
#include <string>
#include <utility>

#include "envoy/http/query_params.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/formatter/substitution_formatter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderMutation {

using Http::Utility::QueryParamsMulti;

QueryParamsEvaluator::QueryParamsEvaluator(
    const Protobuf::RepeatedPtrField<QueryParameterMutationProto>& query_param_mutations)
    : formatter_(std::make_unique<Formatter::FormatterImpl>("", true)) {

  for (const auto& query_param : query_param_mutations) {
    mutations_.emplace_back(query_param);
  }
}

void QueryParamsEvaluator::evaluateQueryParams(Http::RequestHeaderMap& headers,
                                               const StreamInfo::StreamInfo& stream_info) const {
  if (mutations_.empty()) {
    return;
  }

  absl::string_view path = headers.getPathValue();
  QueryParamsMulti query_params = QueryParamsMulti::parseAndDecodeQueryString(path);

  Formatter::HttpFormatterContext ctx{&headers};
  for (const auto& mutation : mutations_) {
    if (!mutation.remove().empty()) {
      query_params.remove(mutation.remove());
    } else {
      const auto value_option = mutation.append();
      const auto key = value_option.query_parameter().key();
      const auto value = value_option.query_parameter().value();
      const auto formatter = std::make_unique<Formatter::FormatterImpl>(value, true);
      switch (AppendAction(value_option.append_action())) {
      case AppendAction::AppendIfExistsOrAdd:
        query_params.add(key, formatter->formatWithContext(ctx, stream_info));
        break;
      case AppendAction::AddIfAbsent:
        if (!query_params.getFirstValue(key).has_value()) {
          query_params.add(key, formatter->formatWithContext(ctx, stream_info));
        }
        break;
      case AppendAction::OverwriteIfExistsOrAdd:
        query_params.overwrite(key, value);
        break;
      case AppendAction::OverwriteIfExists:
        if (query_params.getFirstValue(key).has_value()) {
          query_params.overwrite(key, value);
        }
        break;
      default:
        PANIC("unreachable");
      }
    }
  }

  const auto new_path = query_params.replaceQueryString(headers.Path()->value());
  headers.setPath(new_path);
}

} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
