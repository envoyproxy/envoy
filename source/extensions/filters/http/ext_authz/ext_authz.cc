#include "extensions/filters/http/ext_authz/ext_authz.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/router/config_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

void Filter::initiateCall(const Http::HeaderMap& headers) {
  Router::RouteConstSharedPtr route = callbacks_->route();
  if (route == nullptr || route->routeEntry() == nullptr) {
    return;
  }

  const Router::RouteEntry* route_entry = route->routeEntry();
  Upstream::ThreadLocalCluster* cluster = config_->cm().get(route_entry->clusterName());
  if (cluster == nullptr) {
    return;
  }
  cluster_ = cluster->info();

  Filters::Common::ExtAuthz::CheckRequestUtils::createHttpCheck(callbacks_, headers,
                                                                check_request_);

  state_ = State::Calling;
  // Don't let the filter chain continue as we are going to invoke check call.
  filter_return_ = FilterReturn::StopDecoding;
  initiating_call_ = true;
  ENVOY_STREAM_LOG(trace, "Ext_authz filter calling authorization server", *callbacks_);
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

  ENVOY_STREAM_LOG(trace, "Ext_authz received status code {}", *callbacks_,
                   enumToInt(response->status_code));

  // We fail open/fail close based of filter config
  // if there is an error contacting the service.
  if (response->status == CheckStatus::Denied ||
      (response->status == CheckStatus::Error && !config_->failureModeAllow())) {
    ENVOY_STREAM_LOG(debug, "Ext_authz rejected the request", *callbacks_);
    ENVOY_STREAM_LOG(trace, "Ext_authz downstream header(s):", *callbacks_);
    callbacks_->sendLocalReply(response->status_code, response->body,
                               [& headers = response->headers_to_add,
                                &callbacks = *callbacks_ ](Http::HeaderMap & response_headers)
                                   ->void {
                                     for (const auto& header : headers) {
                                       response_headers.addCopy(header.first, header.second);
                                       ENVOY_STREAM_LOG(trace, " '{}':'{}'", callbacks,
                                                        header.first.get(), header.second);
                                     }
                                   });
    callbacks_->requestInfo().setResponseFlag(
        RequestInfo::ResponseFlag::UnauthorizedExternalService);
  } else {
    ENVOY_STREAM_LOG(debug, "Ext_authz accepted the request", *callbacks_);
    // Let the filter chain continue.
    filter_return_ = FilterReturn::ContinueDecoding;
    if (config_->failureModeAllow() && response->status == CheckStatus::Error) {
      // Status is Error and yet we are allowing the request. Click a counter.
      cluster_->statsScope().counter("ext_authz.failure_mode_allowed").inc();
    }
    // Only send headers if the response is ok.
    if (response->status == CheckStatus::OK) {
      ENVOY_STREAM_LOG(trace, "Ext_authz upstream header(s):", *callbacks_);
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
