#include "source/extensions/filters/http/custom_response/config.h"

#include <memory>

#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

namespace {

class CustomResponseMatchActionValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<::Envoy::Http::HttpMatchingData> {
public:
  absl::Status
  performDataInputValidation(const Matcher::DataInputFactory<::Envoy::Http::HttpMatchingData>&,
                             absl::string_view) override {
    return absl::OkStatus();
  }
};

Matcher::MatchTreePtr<::Envoy::Http::HttpMatchingData>
createMatcher(const envoy::extensions::filters::http::custom_response::v3::CustomResponse& config,
              Server::Configuration::ServerFactoryContext& context, Stats::StatName stats_prefix) {
  if (config.has_custom_response_matcher()) {
    CustomResponseMatchActionValidationVisitor validation_visitor;
    CustomResponseActionFactoryContext action_factory_context{context, stats_prefix};
    Matcher::MatchTreeFactory<::Envoy::Http::HttpMatchingData, CustomResponseActionFactoryContext>
        factory(action_factory_context, context, validation_visitor);
    return factory.create(config.custom_response_matcher())();
  }
  // Allow matcher to not be set, to allow for cases where we only have route or
  // virtual host specific configurations.
  return {};
}

} // namespace

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::custom_response::v3::CustomResponse& config,
    Server::Configuration::ServerFactoryContext& context, Stats::StatName stats_prefix)
    : stats_prefix_(stats_prefix), matcher_{createMatcher(config, context, stats_prefix)} {}

PolicySharedPtr FilterConfig::getPolicy(const ::Envoy::Http::ResponseHeaderMap& headers,
                                        const StreamInfo::StreamInfo& stream_info) const {
  if (!matcher_) {
    return PolicySharedPtr{};
  }

  ::Envoy::Http::Matching::HttpMatchingDataImpl data(stream_info);
  data.onResponseHeaders(headers);
  auto match = Matcher::evaluateMatch<::Envoy::Http::HttpMatchingData>(*matcher_, data);
  if (!match.result_) {
    return PolicySharedPtr{};
  }
  return match.result_()->getTyped<CustomResponseMatchAction>().policy_;
}

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
