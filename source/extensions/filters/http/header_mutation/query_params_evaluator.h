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
    envoy::config::common::mutation_rules::v3::QueryParameterValueOption;
using QueryParameterAppendActionProto =
    envoy::config::common::mutation_rules::v3::QueryParameterValueOption_QueryParameterAppendAction;
using QueryParameterMutationProto =
    envoy::config::common::mutation_rules::v3::QueryParameterMutation;

enum class AppendAction {
  AppendIfExistsOrAdd = envoy::config::common::mutation_rules::v3::
      QueryParameterValueOption_QueryParameterAppendAction_APPEND_IF_EXISTS_OR_ADD,
  AddIfAbsent = envoy::config::common::mutation_rules::v3::
      QueryParameterValueOption_QueryParameterAppendAction_ADD_IF_ABSENT,
  OverwriteIfExistsOrAdd = envoy::config::common::mutation_rules::v3::
      QueryParameterValueOption_QueryParameterAppendAction_OVERWRITE_IF_EXISTS_OR_ADD,
  OverwriteIfExists = envoy::config::common::mutation_rules::v3::
      QueryParameterValueOption_QueryParameterAppendAction_OVERWRITE_IF_EXISTS,
};

class QueryParamsEvaluator;
using QueryParamsEvaluatorPtr = std::unique_ptr<QueryParamsEvaluator>;

class QueryParamsEvaluator {
public:
  QueryParamsEvaluator(
      const Protobuf::RepeatedPtrField<QueryParameterMutationProto>& query_param_mutations);

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
  std::vector<QueryParameterMutationProto> mutations_;
  Formatter::FormatterPtr formatter_;
};

} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
