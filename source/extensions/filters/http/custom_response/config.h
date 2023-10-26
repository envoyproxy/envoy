#pragma once

#include <memory>

#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"
#include "envoy/http/header_map.h"
#include "envoy/matcher/matcher.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/stats.h"

#include "source/common/http/matching/inputs.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/custom_response/policy.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

/**
 * Container class to store filter configuration, which includes custom
 * responses, and matching tree/list to get custom response for a particular
 * upstream response for both hcm and route specific configs.
 */
class FilterConfig : public Router::RouteSpecificFilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::custom_response::v3::CustomResponse& config,
               Envoy::Server::Configuration::ServerFactoryContext& context,
               Stats::StatName stats_prefix);

  PolicySharedPtr getPolicy(const ::Envoy::Http::ResponseHeaderMap& headers,
                            const StreamInfo::StreamInfo& stream_info) const;

  ~FilterConfig() override = default;

private:
  Stats::StatName stats_prefix_;
  const Matcher::MatchTreePtr<::Envoy::Http::HttpMatchingData> matcher_;
};

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
