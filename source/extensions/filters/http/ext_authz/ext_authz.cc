#include "extensions/filters/http/ext_authz/ext_authz.h"

#include <string>
#include <vector>

#include "envoy/http/codes.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/router/config_impl.h"

#include "extensions/filters/common/ext_authz/ext_authz_impl.h"

#include "fmt/format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

namespace {
const Http::HeaderMap* getDeniedHeader() {
  static const Http::HeaderMap* header_map = new Http::HeaderMapImpl{
      {Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::Forbidden))}};
  return header_map;
}
} // namespace

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
  initiating_call_ = true;
  client_->check(*this, check_request_, callbacks_->activeSpan());
  initiating_call_ = false;
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::HeaderMap& headers, bool) {
  request_headers_ = &headers;
  initiateCall(headers);
  return state_ == State::Calling ? Http::FilterHeadersStatus::StopIteration
                                  : Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance&, bool) {
  return state_ == State::Calling ? Http::FilterDataStatus::StopIterationAndWatermark
                                  : Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::HeaderMap&) {
  return state_ == State::Calling ? Http::FilterTrailersStatus::StopIteration
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

  const bool update_status_code = response->status_code >= enumToInt(Http::Code::Continue) &&
                                  response->status_code != enumToInt(Http::Code::Forbidden);

  switch (response->status) {
  case CheckStatus::OK:
    cluster_->statsScope().counter("ext_authz.ok").inc();
    break;
  case CheckStatus::Error:
    cluster_->statsScope().counter("ext_authz.error").inc();
    break;
  case CheckStatus::Denied:
    cluster_->statsScope().counter("ext_authz.denied").inc();
    Http::CodeUtility::ResponseStatInfo info{
        config_->scope(),
        cluster_->statsScope(),
        EMPTY_STRING,
        (update_status_code ? response->status_code : enumToInt(Http::Code::Forbidden)),
        true,
        EMPTY_STRING,
        EMPTY_STRING,
        EMPTY_STRING,
        EMPTY_STRING,
        false};
    Http::CodeUtility::chargeResponseStat(info);
    break;
  }

  // We fail open/fail close based of filter config
  // if there is an error contacting the service.
  if (response->status == CheckStatus::Denied ||
      (response->status == CheckStatus::Error && !config_->failureModeAllow())) {
    Http::HeaderMapPtr response_headers;
    if (update_status_code) {
      response_headers = std::make_unique<Http::HeaderMapImpl>(Http::HeaderMapImpl{
          {Http::Headers::get().Status, std::to_string(response->status_code)}});
    } else {
      response_headers = std::make_unique<Http::HeaderMapImpl>(*getDeniedHeader());
    }

    for (const auto& header : response->headers) {
      response_headers->setReferenceKey(header.first, header.second);
    }
    
    if (response->body) {
      callbacks_->encodeHeaders(std::move(response_headers), false);
      callbacks_->encodeData(*response->body.get(), true);
    } else {
      callbacks_->encodeHeaders(std::move(response_headers), true);
    }

    callbacks_->requestInfo().setResponseFlag(
        RequestInfo::ResponseFlag::UnauthorizedExternalService);
  } else {
    if (config_->failureModeAllow() && response->status == CheckStatus::Error) {
      // Status is Error and yet we are allowing the request. Click a counter.
      cluster_->statsScope().counter("ext_authz.failure_mode_allowed").inc();
    }

    // We can get completion inline, so only call continue if that isn't happening.
    if (!initiating_call_) {
      if (response->status == CheckStatus::OK) {
        for (const auto& header : response->headers) {
          request_headers_->setReferenceKey(header.first, header.second);
        }
      }
      callbacks_->continueDecoding();
    }
  }
}

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
