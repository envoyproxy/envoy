#pragma once

#include <memory>
#include <optional>

#include "envoy/config/typed_config.h"
#include "envoy/extensions/http/custom_response/redirect_policy/v3/redirect_policy.pb.h"
#include "envoy/extensions/http/custom_response/redirect_policy/v3/redirect_policy.pb.validate.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "source/common/config/utility.h"
#include "source/common/http/utility.h"
#include "source/common/router/header_parser.h"
#include "source/extensions/filters/http/custom_response/policy.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {
class CustomResponseFilter;
} // namespace CustomResponse
} // namespace HttpFilters

namespace Http {
namespace CustomResponse {
class ModifyRequestHeadersAction;

/**
 * All stats for the custom response filter. @see stats_macros.h
 */
#define ALL_CUSTOM_RESPONSE_REDIRECT_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)      \
  COUNTER(custom_response_redirect_no_route)                                                       \
  COUNTER(custom_response_invalid_uri)

/**
 * Wrapper struct for stats. @see stats_macros.h
 */
MAKE_STAT_NAMES_STRUCT(CustomResponseRedirectStatNames, ALL_CUSTOM_RESPONSE_REDIRECT_STATS);
MAKE_STATS_STRUCT(CustomResponseRedirectStats, CustomResponseRedirectStatNames,
                  ALL_CUSTOM_RESPONSE_REDIRECT_STATS);

class RedirectPolicy : public Extensions::HttpFilters::CustomResponse::Policy,
                       public Logger::Loggable<Logger::Id::filter> {

public:
  RedirectPolicy(
      const envoy::extensions::http::custom_response::redirect_policy::v3::RedirectPolicy& config,
      Stats::StatName stats_prefix, Envoy::Server::Configuration::ServerFactoryContext& context);

  ::Envoy::Http::FilterHeadersStatus
  encodeHeaders(::Envoy::Http::ResponseHeaderMap&, bool,
                Extensions::HttpFilters::CustomResponse::CustomResponseFilter&) const override;

  ::Envoy::OptRef<const std::string> uri() const { return ::Envoy::makeOptRefFromPtr(uri_.get()); }

  ::Envoy::OptRef<const ::Envoy::Http::Utility::RedirectConfig> redirectAction() const {
    return redirect_action_ ? ::Envoy::makeOptRef(*redirect_action_)
                            : ::Envoy::OptRef<const ::Envoy::Http::Utility::RedirectConfig>();
  }

private:
  std::unique_ptr<ModifyRequestHeadersAction> createModifyRequestHeadersAction(
      const envoy::extensions::http::custom_response::redirect_policy::v3::RedirectPolicy& config,
      Envoy::Server::Configuration::ServerFactoryContext& context);

  const CustomResponseRedirectStatNames stat_names_;
  const CustomResponseRedirectStats stats_;

  // Remote source the request should be redirected to.
  const std::unique_ptr<const std::string> uri_;
  const std::unique_ptr<const ::Envoy::Http::Utility::RedirectConfig> redirect_action_;

  const absl::optional<::Envoy::Http::Code> status_code_;
  const std::unique_ptr<Envoy::Router::HeaderParser> response_header_parser_;
  const std::unique_ptr<Envoy::Router::HeaderParser> request_header_parser_;
  const std::unique_ptr<ModifyRequestHeadersAction> modify_request_headers_action_;
};

class ModifyRequestHeadersAction {
public:
  virtual ~ModifyRequestHeadersAction() = default;
  virtual void modifyRequestHeaders(::Envoy::Http::RequestHeaderMap& request_headers,
                                    const ::Envoy::Http::ResponseHeaderMap& response_headers,
                                    Envoy::StreamInfo::StreamInfo& stream_info,
                                    const RedirectPolicy&) PURE;
};

class ModifyRequestHeadersActionFactory : public Config::TypedFactory {
public:
  ~ModifyRequestHeadersActionFactory() override = default;

  virtual std::unique_ptr<ModifyRequestHeadersAction>
  createAction(const Protobuf::Message& config,
               const envoy::extensions::http::custom_response::redirect_policy::v3::RedirectPolicy&
                   redirect_policy,
               Envoy::Server::Configuration::ServerFactoryContext& context) PURE;

  std::string category() const override {
    return "envoy.http.custom_response.redirect_policy.modify_request_headers_action";
  }
};

} // namespace CustomResponse
} // namespace Http
} // namespace Extensions
} // namespace Envoy
