#include "extensions/filters/http/ext_authz/ext_authz.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/router/config_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

FilterConfig::FilterConfig(const envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz& config,
                           const LocalInfo::LocalInfo& local_info, const Runtime::Loader& runtime,
                           Stats::Scope& scope, Upstream::ClusterManager& cm)
    : local_info_(local_info), runtime_(runtime),
      allowed_authorization_headers_(
          toAuthorizationHeaders(config.http_service().allowed_authorization_headers())),
      allowed_request_headers_(toRequestHeaders(config.http_service().allowed_request_headers())),
      authorization_headers_to_add_(
          toAuthorizationHeadersToAdd(config.http_service().authorization_headers_to_add())),
      cluster_name_(config.grpc_service().envoy_grpc().cluster_name()), scope_(scope), cm_(cm),
      failure_mode_allow_(config.failure_mode_allow()) {}

Http::LowerCaseStrUnorderedSet FilterConfig::toRequestHeaders(
    const Protobuf::RepeatedPtrField<Envoy::ProtobufTypes::String>& request_headers) {
  Http::LowerCaseStrUnorderedSet headers;
  headers.reserve(request_headers.size() + 10);
  headers.emplace(Http::Headers::get().Path);
  headers.emplace(Http::Headers::get().Method);
  headers.emplace(Http::Headers::get().Host);
  headers.emplace(Http::Headers::get().Authorization);
  headers.emplace(Http::Headers::get().ProxyAuthorization);
  headers.emplace(Http::Headers::get().UserAgent);
  headers.emplace(Http::Headers::get().Cookie);
  headers.emplace(Http::Headers::get().ForwardedFor);
  headers.emplace(Http::Headers::get().ForwardedHost);
  headers.emplace(Http::Headers::get().ForwardedProto);
  for (const auto& header : request_headers) {
    headers.emplace(header);
  }
  return headers;
}

Http::LowerCaseStrUnorderedSet FilterConfig::toAuthorizationHeaders(
    const Protobuf::RepeatedPtrField<Envoy::ProtobufTypes::String>& response_headers) {
  Http::LowerCaseStrUnorderedSet headers;
  headers.reserve(response_headers.size() + 3);
  headers.emplace(Http::Headers::get().WWWAuthenticate);
  headers.emplace(Http::Headers::get().ProxyAuthenticate);
  headers.emplace(Http::Headers::get().Location);
  for (const auto& header : response_headers) {
    headers.emplace(header);
  }
  return headers;
}

Http::LowerCaseStringPairVec FilterConfig::toAuthorizationHeadersToAdd(
    const Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValue>& to_add_headers) {
  Http::LowerCaseStringPairVec headers;
  headers.reserve(to_add_headers.size());
  for (const auto& header : to_add_headers) {
    headers.emplace_back(Http::LowerCaseString(header.key()), header.value());
  }
  return headers;
}

// const LocalInfo::LocalInfo&  FilterConfig::localInfo() const { return local_info_; }

// Runtime::Loader&  FilterConfig::runtime() {
//   return runtime_;
// }

// Stats::Scope&  FilterConfig::scope() {
//   return scope_;
// }

// std::string FilterConfig::cluster() {
//   return cluster_name_;
// }

// Upstream::ClusterManager& FilterConfig::cm() {
//   return cm_;
// }

// const Http::LowerCaseStrUnorderedSet&  FilterConfig::allowedAuthorizationHeaders() {
//     return allowed_authorization_headers_;
// }

// const Http::LowerCaseStrUnorderedSet& FilterConfig::allowedRequestHeaders() {
//   return allowed_request_headers_;
// }

// bool failureModeAllow() const { return failure_mode_allow_; }

// const Http::LowerCaseStringPairVec& FilterConfig::authorizationHeadersToAdd() const {
//   return authorization_headers_to_add_;
// }

void Filter::initiateCall(const Http::HeaderMap& headers) {
  Router::RouteConstSharedPtr route = callbacks_->route();
  if (route == nullptr || route->routeEntry() == nullptr) {
    return;
  }
  cluster_ = callbacks_->clusterInfo();
  if (!cluster_) {
    return;
  }

  Filters::Common::ExtAuthz::CheckRequestUtils::createHttpCheck(callbacks_, headers,
                                                                check_request_);

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
                               });
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
