#include "extensions/filters/http/ext_authz/ext_authz.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/router/config_impl.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

FilterConfig::FilterConfig(const envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz& config,
                           const LocalInfo::LocalInfo& local_info, const Runtime::Loader& runtime,
                           Stats::Scope& scope, Upstream::ClusterManager& cm)
    : allowed_request_headers_(
          toRequestHeader(config.http_service().authorization_request().allowed_headers())),
      allowed_client_headers_(
          toClientHeader(config.http_service().authorization_response().allowed_client_headers())),
      allowed_upstream_headers_(toUpstreamHeader(
          config.http_service().authorization_response().allowed_upstream_headers())),
      authorization_headers_to_add_(toAuthorizationHeaderToAdd(
          config.http_service().authorization_request().headers_to_add())),
      local_info_(local_info), runtime_(runtime),
      cluster_name_(config.grpc_service().envoy_grpc().cluster_name()), scope_(scope), cm_(cm),
      failure_mode_allow_(config.failure_mode_allow()) {}

std::vector<Matchers::StringMatcher>
FilterConfig::toRequestHeader(const envoy::type::matcher::ListStringMatcher& matchers) {
  std::vector<Http::LowerCaseString> default_keys{
      Http::Headers::get().Authorization, Http::Headers::get().Method, Http::Headers::get().Path,
      Http::Headers::get().Host};
  std::vector<Matchers::StringMatcher> list_matcher;
  list_matcher.reserve(matchers.patterns().size() + default_keys.size());
  for (const auto& key : default_keys) {
    envoy::type::matcher::StringMatcher matcher;
    matcher.set_exact(key.get());
    list_matcher.push_back(matcher);
  }
  for (const auto& matcher : matchers.patterns()) {
    list_matcher.push_back(Matchers::StringMatcher(matcher));
  }
  return list_matcher;
}

std::vector<Matchers::StringMatcher>
FilterConfig::toClientHeader(const envoy::type::matcher::ListStringMatcher& matchers) {
  std::vector<Matchers::StringMatcher> list_matcher;

  // If the list is empty, all authorization response headers, except Host, should be included to
  // the client response. If not empty, user input matchers and a default list of exact matches will
  // be used.
  if (matchers.patterns().empty()) {
    envoy::type::matcher::StringMatcher matcher;
    matcher.set_regex("^((?!(:authority)).)*$");
    list_matcher.push_back(matcher);
  } else {
    std::vector<Http::LowerCaseString> default_keys{Http::Headers::get().Status,
                                                    Http::Headers::get().ContentLength,
                                                    Http::Headers::get().Path,
                                                    Http::Headers::get().Host,
                                                    Http::Headers::get().WWWAuthenticate,
                                                    Http::Headers::get().Location};
    list_matcher.reserve(matchers.patterns().size() + default_keys.size());
    for (const auto& key : default_keys) {
      envoy::type::matcher::StringMatcher matcher;
      matcher.set_exact(key.get());
      list_matcher.push_back(matcher);
    }
    for (const auto& matcher : matchers.patterns()) {
      list_matcher.push_back(Matchers::StringMatcher(matcher));
    }
  }

  return list_matcher;
}

std::vector<Matchers::StringMatcher>
FilterConfig::toUpstreamHeader(const envoy::type::matcher::ListStringMatcher& matchers) {
  std::vector<Matchers::StringMatcher> list_matcher;
  list_matcher.reserve(matchers.patterns().size());
  for (const auto& matcher : matchers.patterns()) {
    list_matcher.push_back(Matchers::StringMatcher(matcher));
  }
  return list_matcher;
}

Http::LowerCaseStrPairVector FilterConfig::toAuthorizationHeaderToAdd(
    const Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValue>& headers) {
  Http::LowerCaseStrPairVector header_vec;
  header_vec.reserve(headers.size());
  for (const auto& header : headers) {
    header_vec.emplace_back(Http::LowerCaseString(header.key()), header.value());
  }
  return header_vec;
}

const LocalInfo::LocalInfo& FilterConfig::localInfo() { return local_info_; }

const Runtime::Loader& FilterConfig::runtime() { return runtime_; }

const std::string& FilterConfig::cluster() { return cluster_name_; }

const std::vector<Matchers::StringMatcher>& FilterConfig::allowedRequestHeaders() {
  return allowed_request_headers_;
}

const std::vector<Matchers::StringMatcher>& FilterConfig::allowedClientHeaders() {
  return allowed_client_headers_;
}

const std::vector<Matchers::StringMatcher>& FilterConfig::allowedUpstreamHeaders() {
  return allowed_upstream_headers_;
}

const Http::LowerCaseStrPairVector& FilterConfig::authorizationHeadersToAdd() {
  return authorization_headers_to_add_;
}

Stats::Scope& FilterConfig::scope() const { return scope_; }

Upstream::ClusterManager& FilterConfig::cm() const { return cm_; }

bool FilterConfig::failureModeAllow() const { return failure_mode_allow_; }

void FilterConfigPerRoute::merge(const FilterConfigPerRoute& other) {
  disabled_ = other.disabled_;
  auto begin_it = other.context_extensions_.begin();
  auto end_it = other.context_extensions_.end();
  for (auto it = begin_it; it != end_it; ++it) {
    context_extensions_[it->first] = it->second;
  }
}

void Filter::initiateCall(const Http::HeaderMap& headers) {
  Router::RouteConstSharedPtr route = callbacks_->route();
  if (route == nullptr || route->routeEntry() == nullptr) {
    return;
  }
  cluster_ = callbacks_->clusterInfo();
  if (!cluster_) {
    return;
  }

  // Fast route - if we are disabled, no need to merge.
  const FilterConfigPerRoute* specific_per_route_config =
      Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfigPerRoute>(
          HttpFilterNames::get().ExtAuthorization, route);
  if (specific_per_route_config != nullptr) {
    if (specific_per_route_config->disabled()) {
      return;
    }
  }

  // We are not disabled - get a merged view of the config:
  auto&& maybe_merged_per_route_config =
      Http::Utility::getMergedPerFilterConfig<FilterConfigPerRoute>(
          HttpFilterNames::get().ExtAuthorization, route,
          [](FilterConfigPerRoute& cfg_base, const FilterConfigPerRoute& cfg) {
            cfg_base.merge(cfg);
          });

  Protobuf::Map<ProtobufTypes::String, ProtobufTypes::String> context_extensions;
  if (maybe_merged_per_route_config) {
    context_extensions = maybe_merged_per_route_config.value().takeContextExtensions();
  }
  Filters::Common::ExtAuthz::CheckRequestUtils::createHttpCheck(
      callbacks_, headers, std::move(context_extensions), check_request_);

  state_ = State::Calling;
  // Don't let the filter chain continue as we are going to invoke check call.
  filter_return_ = FilterReturn::StopDecoding;
  initiating_call_ = true;
  ENVOY_STREAM_LOG(trace, "ext_authz filter calling authorization server", *callbacks_);
  client_->check(*this, check_request_, callbacks_->activeSpan());
  initiating_call_ = false;
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::HeaderMap& headers, bool) {
  request_headers_ = &headers;
  initiateCall(headers);
  return filter_return_ == FilterReturn::StopDecoding ? Http::FilterHeadersStatus::StopIteration
                                                      : Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance&, bool) {
  return filter_return_ == FilterReturn::StopDecoding
             ? Http::FilterDataStatus::StopIterationAndWatermark
             : Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::HeaderMap&) {
  return filter_return_ == FilterReturn::StopDecoding ? Http::FilterTrailersStatus::StopIteration
                                                      : Http::FilterTrailersStatus::Continue;
}

void Filter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

void Filter::onDestroy() {
  if (state_ == State::Calling) {
    state_ = State::Complete;
    client_->cancel();
  }
}

void Filter::onComplete(Filters::Common::ExtAuthz::ResponsePtr&& response) {
  ASSERT(cluster_);
  state_ = State::Complete;
  using Filters::Common::ExtAuthz::CheckStatus;

  switch (response->status) {
  case CheckStatus::OK:
    cluster_->statsScope().counter("ext_authz.ok").inc();
    break;
  case CheckStatus::Error:
    cluster_->statsScope().counter("ext_authz.error").inc();
    break;
  case CheckStatus::Denied:
    cluster_->statsScope().counter("ext_authz.denied").inc();
    Http::CodeUtility::ResponseStatInfo info{config_->scope(),
                                             cluster_->statsScope(),
                                             EMPTY_STRING,
                                             enumToInt(response->status_code),
                                             true,
                                             EMPTY_STRING,
                                             EMPTY_STRING,
                                             EMPTY_STRING,
                                             EMPTY_STRING,
                                             false};
    Http::CodeUtility::chargeResponseStat(info);
    break;
  }

  ENVOY_STREAM_LOG(trace, "ext_authz received status code {}", *callbacks_,
                   enumToInt(response->status_code));

  // We fail open/fail close based of filter config
  // if there is an error contacting the service.
  if (response->status == CheckStatus::Denied ||
      (response->status == CheckStatus::Error && !config_->failureModeAllow())) {
    ENVOY_STREAM_LOG(debug, "ext_authz rejected the request", *callbacks_);
    ENVOY_STREAM_LOG(trace, "ext_authz downstream header(s):", *callbacks_);
    callbacks_->sendLocalReply(response->status_code, response->body,
                               [& headers = response->headers_to_add, &callbacks = *callbacks_](
                                   Http::HeaderMap& response_headers) -> void {
                                 for (const auto& header : headers) {
                                   response_headers.remove(header.first);
                                   response_headers.addCopy(header.first, header.second);
                                   ENVOY_STREAM_LOG(trace, " '{}':'{}'", callbacks,
                                                    header.first.get(), header.second);
                                 }
                               },
                               absl::nullopt);
    callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::UnauthorizedExternalService);
  } else {
    ENVOY_STREAM_LOG(debug, "ext_authz accepted the request", *callbacks_);
    // Let the filter chain continue.
    filter_return_ = FilterReturn::ContinueDecoding;
    if (config_->failureModeAllow() && response->status == CheckStatus::Error) {
      // Status is Error and yet we are allowing the request. Click a counter.
      cluster_->statsScope().counter("ext_authz.failure_mode_allowed").inc();
    }
    // Only send headers if the response is ok.
    if (response->status == CheckStatus::OK) {
      ENVOY_STREAM_LOG(trace, "ext_authz upstream header(s):", *callbacks_);
      for (const auto& header : response->headers_to_add) {
        Http::HeaderEntry* header_to_modify = request_headers_->get(header.first);
        if (header_to_modify) {
          header_to_modify->value(header.second.c_str(), header.second.size());
        } else {
          request_headers_->addCopy(header.first, header.second);
        }
        ENVOY_STREAM_LOG(trace, " '{}':'{}'", *callbacks_, header.first.get(), header.second);
      }
      for (const auto& header : response->headers_to_append) {
        Http::HeaderEntry* header_to_modify = request_headers_->get(header.first);
        if (header_to_modify) {
          Http::HeaderMapImpl::appendToHeader(header_to_modify->value(), header.second);
          ENVOY_STREAM_LOG(trace, " '{}':'{}'", *callbacks_, header.first.get(), header.second);
        }
      }
    }

    if (!initiating_call_) {
      // We got completion async. Let the filter chain continue.
      callbacks_->continueDecoding();
    }
  }
}

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
