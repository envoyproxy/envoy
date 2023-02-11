#include "source/extensions/http/custom_response/redirect_policy/redirect_policy.h"

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
} // namespace

namespace Envoy {
namespace Extensions {
namespace Http {
namespace CustomResponse {

RedirectPolicy::RedirectPolicy(
    const envoy::extensions::http::custom_response::redirect_policy::v3::RedirectPolicy& config,
    Stats::StatName stats_prefix, Envoy::Server::Configuration::ServerFactoryContext& context)
    : stat_names_(context.scope().symbolTable()),
      stats_(stat_names_, context.scope(), stats_prefix), host_(config.host()),
      path_(config.path()), status_code_{config.has_status_code()
                                             ? absl::optional<::Envoy::Http::Code>(
                                                   static_cast<::Envoy::Http::Code>(
                                                       config.status_code().value()))
                                             : absl::optional<::Envoy::Http::Code>{}},
      response_header_parser_(
          Envoy::Router::HeaderParser::configure(config.response_headers_to_add())),
      request_header_parser_(
          Envoy::Router::HeaderParser::configure(config.request_headers_to_add())),
      modify_request_headers_action_(createModifyRequestHeadersAction(config, context)) {
  ::Envoy::Http::Utility::Url absolute_url;
  const std::string uri(absl::StrCat(host_, path_));
  if (!absolute_url.initialize(uri, false)) {
    throw EnvoyException(
        absl::StrCat("Invalid uri specified for redirection for custom response: ", uri));
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
    action = factory.createAction(*action_config, context);
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
  const Policy* policy = encoder_callbacks->streamInfo().filterState()->getDataReadOnly<Policy>(
      "envoy.filters.http.custom_response");
  if (policy) {
    ENVOY_BUG(policy == this, "Policy filter state should be this policy.");
    //  Apply mutations if this is not an error or redirect response. Else leave be.
    auto const cr_code = ::Envoy::Http::Utility::getResponseStatusOrNullopt(headers);
    if (!cr_code.has_value() || (*cr_code < 100 || *cr_code > 299)) {
      return ::Envoy::Http::FilterHeadersStatus::Continue;
    }
    // Apply header mutations.
    response_header_parser_->evaluateHeaders(headers, encoder_callbacks->streamInfo());
    // Modify response status code.
    if (status_code_.has_value()) {
      auto const code = *status_code_;
      headers.setStatus(std::to_string(enumToInt(code)));
      encoder_callbacks->streamInfo().setResponseCode(static_cast<uint32_t>(code));
    }
    return ::Envoy::Http::FilterHeadersStatus::Continue;
  }

  // Modify the request headers & recreate stream.
  if (custom_response_filter.onLocalReplyCalled()) {
    // This condition is true if send local reply is called at any point before
    // the encode call for the custom response filter.
    // TODO(pradeepcrao): Currently redirect policy is not compatible with send local reply as we
    // cannot call recreateStream once sendLocalReply has been called.
    return ::Envoy::Http::FilterHeadersStatus::Continue;
  }
  auto downstream_headers = custom_response_filter.downstreamHeaders();
  RELEASE_ASSERT(downstream_headers != nullptr, "downstream_headers cannot be nullptr");

  ::Envoy::Http::Utility::Url absolute_url;
  const std::string uri(absl::StrCat(host_, path_));
  RELEASE_ASSERT(absolute_url.initialize(uri, false),
                 "uri should not be invalid as this was already validated during config load");
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

  downstream_headers->setScheme(absolute_url.scheme());
  downstream_headers->setHost(absolute_url.hostAndPort());

  auto path_and_query = path_;
  // Strip the #fragment from Location URI if it is present. Envoy treats
  // internal redirect as a new request and will reject it if the URI path
  // contains #fragment.
  const auto fragment_pos = path_.find('#');
  path_and_query = path_.substr(0, fragment_pos);

  downstream_headers->setPath(path_);

  if (decoder_callbacks->downstreamCallbacks()) {
    decoder_callbacks->downstreamCallbacks()->clearRouteCache();
  }
  // Apply header mutations before recalculating the route.
  request_header_parser_->evaluateHeaders(*downstream_headers, decoder_callbacks->streamInfo());
  if (modify_request_headers_action_) {
    modify_request_headers_action_->modifyRequestHeaders(*downstream_headers,
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
  encoder_callbacks->streamInfo().filterState()->setData(
      // TODO(pradeepcrao): Currently we don't have a mechanism to add readonly
      // objects to FilterState, even if they're immutable.
      "envoy.filters.http.custom_response", const_cast<RedirectPolicy*>(this)->shared_from_this(),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Request);
  restore_original_headers.cancel();
  decoder_callbacks->recreateStream(nullptr);

  return ::Envoy::Http::FilterHeadersStatus::StopIteration;
}

} // namespace CustomResponse
} // namespace Http
} // namespace Extensions
} // namespace Envoy
