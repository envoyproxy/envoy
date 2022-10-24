#pragma once

#include <memory>

#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "source/common/router/header_parser.h"
#include "source/extensions/filters/http/custom_response/policy.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

class CustomResponseFilter;

/**
 * All stats for the custom response filter. @see stats_macros.h
 */
#define ALL_CUSTOM_RESPONSE_REDIRECT_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)      \
  COUNTER(custom_response_redirect_no_route)

/**
 * Wrapper struct for stats. @see stats_macros.h
 */
MAKE_STAT_NAMES_STRUCT(CustomResponseRedirectStatNames, ALL_CUSTOM_RESPONSE_REDIRECT_STATS);
MAKE_STATS_STRUCT(CustomResponseRedirectStats, CustomResponseRedirectStatNames,
                  ALL_CUSTOM_RESPONSE_REDIRECT_STATS);

class RedirectPolicy : public Policy, public Logger::Loggable<Logger::Id::filter> {

public:
  explicit RedirectPolicy(
      const envoy::extensions::filters::http::custom_response::v3::CustomResponse::RedirectPolicy&
          config,
      Stats::StatName stats_prefix, Envoy::Server::Configuration::ServerFactoryContext& context);

  Envoy::Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool,
                                                 CustomResponseFilter&) const override;

  const std::string& host() const { return host_; }
  const std::string& path() const { return path_; }

private:
  CustomResponseRedirectStatNames stat_names_;
  CustomResponseRedirectStats stats_;

  // Remote source the request should be redirected to.
  const std::string host_;
  const std::string path_;

  absl::optional<Http::Code> status_code_;
  std::unique_ptr<Envoy::Router::HeaderParser> header_parser_;
};
} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
