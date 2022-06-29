#pragma once

#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"

#include "envoy/matcher/matcher.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/factory_context.h"
#include "source/common/http/matching/inputs.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/custom_response/response.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/custom_response/custom_response_filter.h"

#include "absl/strings/string_view.h"
#include "absl/container/flat_hash_map.h"

#include <memory>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

class CustomFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::custom_response::v3::CustomResponse>,
      public Logger::Loggable<Logger::Id::filter> {
public:
  CustomFilterFactory() : FactoryBase(std::string(FilterName)) {}
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::custom_response::v3::CustomResponse& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

/**
 * Container class to store filter configuration, which includes custom
 * responses, and matching tree/list to get custom response for a particular
 * upstream response.
 */
class FilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::custom_response::v3::CustomResponse& config,
               Server::Configuration::FactoryContext& context);

private:
  absl::flat_hash_map<absl::string_view, ResponseSharedPtr> responses_;
  Matcher::MatchTreePtr<Http::HttpMatchingData> matcher_;
};

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
