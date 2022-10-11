#include "source/extensions/filters/http/custom_response/redirect_policy.h"

#include "envoy/stream_info/filter_state.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/custom_response/custom_response_filter.h"

namespace {
bool schemeIsHttp(const Envoy::Http::RequestHeaderMap& downstream_headers,
                  Envoy::OptRef<const Envoy::Network::Connection> connection) {
  if (downstream_headers.getSchemeValue() == Envoy::Http::Headers::get().SchemeValues.Http) {
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
namespace HttpFilters {
namespace CustomResponse {

RedirectPolicy::RedirectPolicy(
    const envoy::extensions::filters::http::custom_response::v3::CustomResponse::RedirectPolicy&
        config,
    Stats::StatName stats_prefix, Envoy::Server::Configuration::ServerFactoryContext& context)
    : stat_names_(context.scope().symbolTable()),
      stats_(stat_names_, context.scope(), stats_prefix), http_uri_(config.uri()) {

  Http::Utility::Url absolute_url;
  if (!absolute_url.initialize(http_uri_, false)) {
    throw EnvoyException(
        absl::StrCat("Invalid uri specified for redirection for custom response: ", http_uri_));
  }

  if (config.has_status_code()) {
    status_code_ = static_cast<Http::Code>(config.status_code().value());
  }
  header_parser_ = Envoy::Router::HeaderParser::configure(config.response_headers_to_add());
}

Http::FilterHeadersStatus
RedirectPolicy::encodeHeaders(Http::ResponseHeaderMap& headers, bool,
                              CustomResponseFilter& custom_response_filter) const {
  // If filter state for custom response exists, it means this response is a
  // custom response. Apply the custom response mutations to the response from
  // the remote source and return.
  auto encoder_callbacks = custom_response_filter.encoderCallbacks();
  auto decoder_callbacks = custom_response_filter.decoderCallbacks();
  auto filter_state = encoder_callbacks->streamInfo().filterState()->getDataReadOnly<Policy>(
      "envoy.filters.http.custom_response");
  if (filter_state) {
    ENVOY_BUG(filter_state == this, "Policy filter state should be this policy.");
    //  Apply mutations if this is a non-error response. Else leave be.
    auto const cr_code = Http::Utility::getResponseStatusOrNullopt(headers);
    if (!cr_code.has_value() || (*cr_code < 100 || *cr_code > 299)) {
      return Http::FilterHeadersStatus::Continue;
    }
    // Apply header mutations.
    header_parser_->evaluateHeaders(headers, encoder_callbacks->streamInfo());
    // Modify response status code.
    if (status_code_.has_value()) {
      auto const code = *status_code_;
      headers.setStatus(std::to_string(enumToInt(code)));
      encoder_callbacks->streamInfo().setResponseCode(static_cast<uint32_t>(code));
    }
    return Http::FilterHeadersStatus::Continue;
  }

  // Modify the request headers & recreate stream.

  // downstream_headers can be null if sendLocalReply was called during
  // decodeHeaders of a previous filter. In that case just continue, as Custom
  // Response is not compatible with sendLocalReply.
  auto downstream_headers = custom_response_filter.downstreamHeaders();
  if (downstream_headers == nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }

  Http::Utility::Url absolute_url;
  if (!absolute_url.initialize(http_uri_, false)) {
    ENVOY_LOG(error, "Redirect for custom response failed: invalid location {}", http_uri_);
    stats_.custom_response_redirect_invalid_uri_.inc();
    return Http::FilterHeadersStatus::Continue;
  }
  // Don't change the scheme from the original request
  const bool scheme_is_http = schemeIsHttp(*downstream_headers, decoder_callbacks->connection());

  // Cache original host and path
  const std::string original_host(downstream_headers->getHostValue());
  const std::string original_path(downstream_headers->getPathValue());
  const bool scheme_is_set = (downstream_headers->Scheme() != nullptr);
  Cleanup restore_original_headers(
      [original_host, original_path, scheme_is_set, scheme_is_http, downstream_headers]() {
        downstream_headers->setHost(original_host);
        downstream_headers->setPath(original_path);
        if (scheme_is_set) {
          downstream_headers->setScheme(scheme_is_http ? Http::Headers::get().SchemeValues.Http
                                                       : Http::Headers::get().SchemeValues.Https);
        }
      });

  // Replace the original host, scheme and path.
  downstream_headers->setScheme(absolute_url.scheme());
  downstream_headers->setHost(absolute_url.hostAndPort());

  auto path_and_query = absolute_url.pathAndQueryParams();
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http_reject_path_with_fragment")) {
    // Envoy treats internal redirect as a new request and will reject it if URI path
    // contains #fragment. However the Location header is allowed to have #fragment in URI path.
    // To prevent Envoy from rejecting internal redirect, strip the #fragment from Location URI if
    // it is present.
    auto fragment_pos = path_and_query.find('#');
    path_and_query = path_and_query.substr(0, fragment_pos);
  }
  downstream_headers->setPath(path_and_query);

  if (decoder_callbacks->downstreamCallbacks()) {
    decoder_callbacks->downstreamCallbacks()->clearRouteCache();
  }
  const auto route = decoder_callbacks->route();
  // Don't allow a redirect to a non existing route.
  if (!route) {
    stats_.custom_response_redirect_no_route_.inc();
    ENVOY_LOG(trace, "Redirect for custom response failed: no route found");
    return Http::FilterHeadersStatus::Continue;
  }
  downstream_headers->setMethod(Http::Headers::get().MethodValues.Get);
  downstream_headers->remove(Http::Headers::get().ContentLength);
  encoder_callbacks->streamInfo().filterState()->setData(
      // TODO(pradeepcrao): Currently we don't have a mechanism to add readonly
      // objects to FilterState, even if they're immutable.
      "envoy.filters.http.custom_response", const_cast<RedirectPolicy*>(this)->shared_from_this(),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Request);
  restore_original_headers.cancel();
  decoder_callbacks->recreateStream(nullptr);

  return Http::FilterHeadersStatus::StopIteration;
}

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
