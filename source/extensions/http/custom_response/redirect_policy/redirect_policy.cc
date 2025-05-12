#include "source/extensions/http/custom_response/redirect_policy/redirect_policy.h"

#include <string>
#include <variant>

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/custom_response/custom_response_filter.h"

namespace {
bool schemeIsHttp(const ::Envoy::Http::RequestHeaderMap& downstream_headers,
                  Envoy::OptRef<const Envoy::Network::Connection> connection) {
  if (downstream_headers.getSchemeValue() == ::Envoy::Http::Headers::get().SchemeValues.Http) {
    return true;
  }
  if (connection.has_value() && !connection->ssl()) {
    return true;
  }
  return false;
}

::Envoy::Http::Utility::RedirectConfig
createRedirectConfig(const envoy::config::route::v3::RedirectAction& redirect_action) {
  ::Envoy::Http::Utility::RedirectConfig redirect_config{
      redirect_action.scheme_redirect(),
      redirect_action.host_redirect(),
      redirect_action.port_redirect() ? ":" + std::to_string(redirect_action.port_redirect()) : "",
      redirect_action.path_redirect(),
      "",      // prefix_rewrite
      "",      // regex_rewrite_redirect_substitution
      nullptr, // regex_rewrite_redirect
      redirect_action.path_redirect().find('?') != absl::string_view::npos,
      redirect_action.https_redirect(),
      redirect_action.strip_query()};
  if (redirect_action.has_regex_rewrite()) {
    throw Envoy::EnvoyException("regex_rewrite is not supported for Custom Response");
  }
  if (redirect_action.has_prefix_rewrite()) {
    throw Envoy::EnvoyException("prefix_rewrite is not supported for Custom Response");
  }
  return redirect_config;
}

} // namespace

namespace Envoy {
namespace Extensions {
namespace Http {
namespace CustomResponse {

RedirectPolicy::RedirectPolicy(
    const envoy::extensions::http::custom_response::redirect_policy::v3::RedirectPolicy& config,
    Stats::StatName stats_prefix, Envoy::Server::Configuration::ServerFactoryContext& context)
    : stat_names_(context.scope().symbolTable()),
      stats_(stat_names_, context.scope(), stats_prefix),
      uri_{config.has_uri() ? std::make_unique<std::string>(config.uri()) : nullptr},
      redirect_action_{config.has_redirect_action()
                           ? std::make_unique<::Envoy::Http::Utility::RedirectConfig>(
                                 createRedirectConfig(config.redirect_action()))
                           : nullptr},
      status_code_{config.has_status_code()
                       ? absl::optional<::Envoy::Http::Code>(
                             static_cast<::Envoy::Http::Code>(config.status_code().value()))
                       : absl::optional<::Envoy::Http::Code>{}},
      response_header_parser_(THROW_OR_RETURN_VALUE(
          Envoy::Router::HeaderParser::configure(config.response_headers_to_add()),
          Router::HeaderParserPtr)),
      request_header_parser_(THROW_OR_RETURN_VALUE(
          Envoy::Router::HeaderParser::configure(config.request_headers_to_add()),
          Router::HeaderParserPtr)),
      modify_request_headers_action_(createModifyRequestHeadersAction(config, context)) {
  // Ensure that exactly one of uri_ or redirect_action_ is specified.
  ASSERT((uri_ || redirect_action_) && !(uri_ && redirect_action_));

  if (uri_) {
    ::Envoy::Http::Utility::Url absolute_url;
    if (!absolute_url.initialize(*uri_, false)) {
      throw EnvoyException(
          absl::StrCat("Invalid uri specified for redirection for custom response: ", *uri_));
    }
  }
  if (redirect_action_ && redirect_action_->path_redirect_.find('#') != std::string::npos) {
    throw EnvoyException(
        absl::StrCat("#fragment is not supported for custom response. Specified path_redirect is ",
                     redirect_action_->path_redirect_));
  }
}

std::unique_ptr<ModifyRequestHeadersAction> RedirectPolicy::createModifyRequestHeadersAction(
    const envoy::extensions::http::custom_response::redirect_policy::v3::RedirectPolicy& config,
    Envoy::Server::Configuration::ServerFactoryContext& context) {
  std::unique_ptr<ModifyRequestHeadersAction> action;
  if (config.has_modify_request_headers_action()) {
    auto& factory = Envoy::Config::Utility::getAndCheckFactory<ModifyRequestHeadersActionFactory>(
        config.modify_request_headers_action());
    const auto action_config = Envoy::Config::Utility::translateAnyToFactoryConfig(
        config.modify_request_headers_action().typed_config(), context.messageValidationVisitor(),
        factory);
    action = factory.createAction(*action_config, config, context);
  }
  return action;
}

::Envoy::Http::FilterHeadersStatus RedirectPolicy::encodeHeaders(
    ::Envoy::Http::ResponseHeaderMap& headers, bool,
    Extensions::HttpFilters::CustomResponse::CustomResponseFilter& custom_response_filter) const {
  // If filter state for custom response exists, it means this response is a
  // custom response. Apply the custom response mutations to the response from
  // the remote source and return.
  auto encoder_callbacks = custom_response_filter.encoderCallbacks();
  auto decoder_callbacks = custom_response_filter.decoderCallbacks();
  const ::Envoy::Extensions::HttpFilters::CustomResponse::CustomResponseFilterState* filter_state =
      encoder_callbacks->streamInfo()
          .filterState()
          ->getDataReadOnly<
              ::Envoy::Extensions::HttpFilters::CustomResponse::CustomResponseFilterState>(
              "envoy.filters.http.custom_response");
  if (filter_state) {
    ENVOY_BUG(filter_state->policy.get() == this, "Policy filter state should be this policy.");
    // Apply header mutations.
    response_header_parser_->evaluateHeaders(headers, encoder_callbacks->streamInfo());
    const absl::optional<::Envoy::Http::Code> status_code_to_use =
        status_code_.has_value() ? status_code_
                                 : (filter_state->original_response_code.has_value()
                                        ? filter_state->original_response_code
                                        : absl::nullopt);
    // Modify response status code.
    if (status_code_to_use.has_value()) {
      auto const code = *status_code_to_use;
      headers.setStatus(std::to_string(enumToInt(code)));
      encoder_callbacks->streamInfo().setResponseCode(static_cast<uint32_t>(code));
    }
    return ::Envoy::Http::FilterHeadersStatus::Continue;
  }

  auto downstream_headers = custom_response_filter.downstreamHeaders();
  // Modify the request headers & recreate stream.
  if (custom_response_filter.onLocalReplyCalled() && downstream_headers == nullptr) {
    // This condition is true if send local reply is called at any point before
    // the decodeHeaders call for the custom response filter.
    return ::Envoy::Http::FilterHeadersStatus::Continue;
  }
  RELEASE_ASSERT(downstream_headers != nullptr, "downstream_headers cannot be nullptr");

  // Don't change the scheme from the original request
  const bool scheme_is_http = schemeIsHttp(*downstream_headers, decoder_callbacks->connection());

  // Cache original host and path
  const std::string original_host(downstream_headers->getHostValue());
  const std::string original_path(downstream_headers->getPathValue());
  const bool scheme_is_set = (downstream_headers->Scheme() != nullptr);
  // Replace the original host, scheme and path.
  Cleanup restore_original_headers(
      [original_host, original_path, scheme_is_set, scheme_is_http, downstream_headers]() {
        downstream_headers->setHost(original_host);
        downstream_headers->setPath(original_path);
        if (scheme_is_set) {
          downstream_headers->setScheme(scheme_is_http
                                            ? ::Envoy::Http::Headers::get().SchemeValues.Http
                                            : ::Envoy::Http::Headers::get().SchemeValues.Https);
        }
      });

  ::Envoy::Http::Utility::Url absolute_url;
  std::string uri(uri_ ? *uri_
                       : ::Envoy::Http::Utility::newUri(*redirect_action_, *downstream_headers));
  if (!absolute_url.initialize(uri, false)) {
    stats_.custom_response_invalid_uri_.inc();
    // We could potentially get an invalid url only if redirect_action_ was specified instead
    // of uri_. Hence, assert that uri_ is not set.
    ENVOY_BUG(!static_cast<bool>(uri_),
              "uri should not be invalid as this was already validated during config load");
    return ::Envoy::Http::FilterHeadersStatus::Continue;
  }
  downstream_headers->setScheme(absolute_url.scheme());
  downstream_headers->setHost(absolute_url.hostAndPort());

  auto path_and_query = absolute_url.pathAndQueryParams();
  // Strip the #fragment from Location URI if it is present. Envoy treats
  // internal redirect as a new request and will reject it if the URI path
  // contains #fragment.
  const auto fragment_pos = path_and_query.find('#');
  path_and_query = path_and_query.substr(0, fragment_pos);

  downstream_headers->setPath(path_and_query);
  if (decoder_callbacks->downstreamCallbacks()) {
    decoder_callbacks->downstreamCallbacks()->clearRouteCache();
  }
  // Apply header mutations before recalculating the route.
  request_header_parser_->evaluateHeaders(*downstream_headers, decoder_callbacks->streamInfo());
  if (modify_request_headers_action_) {
    modify_request_headers_action_->modifyRequestHeaders(*downstream_headers, headers,
                                                         decoder_callbacks->streamInfo(), *this);
  }
  const auto route = decoder_callbacks->route();
  // Don't allow a redirect to a non existing route.
  if (!route) {
    stats_.custom_response_redirect_no_route_.inc();
    ENVOY_LOG(trace, "Redirect for custom response failed: no route found");
    // Note that at this point the header mutations have already taken into affect
    // and access logs will log the mutated headers, even though no internal
    // redirect will take place.
    return ::Envoy::Http::FilterHeadersStatus::Continue;
  }
  downstream_headers->setMethod(::Envoy::Http::Headers::get().MethodValues.Get);
  downstream_headers->remove(::Envoy::Http::Headers::get().ContentLength);
  // Cache the original response code.
  absl::optional<::Envoy::Http::Code> original_response_code;
  absl::optional<uint64_t> current_code =
      ::Envoy::Http::Utility::getResponseStatusOrNullopt(headers);
  if (current_code.has_value()) {
    original_response_code = static_cast<::Envoy::Http::Code>(*current_code);
  }
  encoder_callbacks->streamInfo().filterState()->setData(
      // TODO(pradeepcrao): Currently we don't have a mechanism to add readonly
      // objects to FilterState, even if they're immutable.
      "envoy.filters.http.custom_response",
      std::make_shared<::Envoy::Extensions::HttpFilters::CustomResponse::CustomResponseFilterState>(
          const_cast<RedirectPolicy*>(this)->shared_from_this(), original_response_code),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Request);
  restore_original_headers.cancel();
  decoder_callbacks->recreateStream(nullptr);

  return ::Envoy::Http::FilterHeadersStatus::StopIteration;
}

} // namespace CustomResponse
} // namespace Http
} // namespace Extensions
} // namespace Envoy
