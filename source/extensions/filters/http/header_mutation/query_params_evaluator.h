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

using KeyValueAppendProto =
    envoy::config::core::v3::KeyValueAppend;
using KeyValueAppendActionProto =
    envoy::config::core::v3::KeyValueAppend_KeyValueAppendAction;
using KeyValueMutationProto =
    envoy::config::core::v3::KeyValueMutation;

enum class AppendAction {
  AppendIfExistsOrAdd = envoy::config::core::v3::
      KeyValueAppend_KeyValueAppendAction_APPEND_IF_EXISTS_OR_ADD,
  AddIfAbsent = envoy::config::core::v3::
      KeyValueAppend_KeyValueAppendAction_ADD_IF_ABSENT,
  OverwriteIfExistsOrAdd = envoy::config::core::v3::
      KeyValueAppend_KeyValueAppendAction_OVERWRITE_IF_EXISTS_OR_ADD,
  OverwriteIfExists = envoy::config::core::v3::
      KeyValueAppend_KeyValueAppendAction_OVERWRITE_IF_EXISTS,
};

class QueryParamsEvaluator;
using QueryParamsEvaluatorPtr = std::unique_ptr<QueryParamsEvaluator>;

class QueryParamsEvaluator {
public:
  QueryParamsEvaluator(
      const Protobuf::RepeatedPtrField<KeyValueMutationProto>& query_param_mutations);

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
  std::vector<KeyValueMutationProto> mutations_;
  Formatter::FormatterPtr formatter_;
};

} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
