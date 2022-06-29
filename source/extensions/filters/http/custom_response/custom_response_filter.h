#pragma once

#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

inline constexpr absl::string_view FilterName = "envoy.filters.http.custom_response";

/**
 * All stats for the custom response filter. @see stats_macros.h
 */
#define ALL_CUSTOM_RESPONSE_FILTER_STATS(COUNTER) COUNTER(get_remote_response_failed)

/**
 * Wrapper struct for stats. @see stats_macros.h
 */
struct CustomResponseFilterStats {
  ALL_CUSTOM_RESPONSE_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

class CustomResponseFilter : public Http::PassThroughFilter,
                             public Logger::Loggable<Logger::Id::filter> {
public:
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  void onDestroy() override;
  // void onComplete(const Http::ResponseMessage* response_ptr) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  CustomResponseFilterStats& stats() { return stats_; }

  ~CustomResponseFilter() override = default;

  CustomResponseFilter(
      std::shared_ptr<envoy::extensions::filters::http::custom_response::v3::CustomResponse> config,
      Server::Configuration::FactoryContext& context, const std::string& stats_prefix)
      : stats_(generateStats(stats_prefix, context.scope())), config_{std::move(config)},
        factory_context_(context) {}

private:
  CustomResponseFilterStats generateStats(const std::string& stats_prefix, Stats::Scope& scope) {
    return {ALL_CUSTOM_RESPONSE_FILTER_STATS(POOL_COUNTER_PREFIX(scope, stats_prefix))};
  }

  CustomResponseFilterStats stats_;
  // Shared ptr to filter config for RCU like behaviour.
  std::shared_ptr<envoy::extensions::filters::http::custom_response::v3::CustomResponse> config_;
  Server::Configuration::FactoryContext& factory_context_;
};

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
