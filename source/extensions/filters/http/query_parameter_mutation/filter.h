#pragma once

#include <memory>

#include "envoy/extensions/filters/http/query_parameter_mutation/v3/config.pb.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/router/router.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/query_parameter_mutation/query_params_evaluator.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace QueryParameterMutation {

using FilterConfigProto = envoy::extensions::filters::http::query_parameter_mutation::v3::Config;

class Config : public Router::RouteSpecificFilterConfig {
public:
  Config(const FilterConfigProto& config);

  void evaluateQueryParams(Http::RequestHeaderMap& headers,
                           StreamInfo::StreamInfo& stream_info) const;

private:
  QueryParamsEvaluatorPtr query_params_evaluator_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

class Filter : public Http::PassThroughDecoderFilter {
public:
  Filter(ConfigSharedPtr config);

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;

private:
  ConfigSharedPtr config_;
};

} // namespace QueryParameterMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
